// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include <muduo/net/TcpServer.h>

#include <muduo/base/Logging.h>
#include <muduo/net/Acceptor.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/EventLoopThreadPool.h>
#include <muduo/net/SocketsOps.h>

#include <boost/bind.hpp>

#include <stdio.h>  // snprintf

using namespace muduo;
using namespace muduo::net;


TcpServer::TcpServer(EventLoop* loop,
                     const InetAddress& listenAddr,
                     const string& nameArg,
                     Option option)
  : loop_(CHECK_NOTNULL(loop)),
    ipPort_(listenAddr.toIpPort()),
    name_(nameArg),
	//构造一个scoped_ptr对象
	//相比较于unique_ptr,更专注。
    acceptor_(new Acceptor(loop, listenAddr, option == kReusePort)),
    threadPool_(new EventLoopThreadPool(loop, name_)),
    connectionCallback_(defaultConnectionCallback),
    messageCallback_(defaultMessageCallback),
    nextConnId_(1)
{
	//bind成员函数的基本用法：
	/*
	在成员函数前加上取地址操作符&，表明这是一个成员函数指针，
	否则无法通过编译，与绑定普通函数的不一样。
	*/
	//这里链接acceptor，在调用readcallback后再tcpserver层再创建连接。
  acceptor_->setNewConnectionCallback(
      boost::bind(&TcpServer::newConnection, this, _1, _2));
}

TcpServer::~TcpServer()
{
  loop_->assertInLoopThread();
  LOG_TRACE << "TcpServer::~TcpServer [" << name_ << "] destructing";

  for (ConnectionMap::iterator it(connections_.begin());
      it != connections_.end(); ++it)
  {
    TcpConnectionPtr conn(it->second);
    it->second.reset();
    conn->getLoop()->runInLoop(
      boost::bind(&TcpConnection::connectDestroyed, conn));
  }
}

void TcpServer::setThreadNum(int numThreads)
{
  assert(0 <= numThreads);
  //根据参数决定是否启用多线程模式？
  threadPool_->setThreadNum(numThreads);
}

void TcpServer::start()
{
  if (started_.getAndSet(1) == 0)
  {
	//启动创建线程池
    threadPool_->start(threadInitCallback_);

    assert(!acceptor_->listenning());
    loop_->runInLoop(
        boost::bind(&Acceptor::listen, get_pointer(acceptor_)));//执行acceptor::listen
  }
}

void TcpServer::newConnection(int sockfd, const InetAddress& peerAddr)
{
  loop_->assertInLoopThread();
  
  /*
  
	用one loop per thread 的思想实现多线程TcpServer的关键步骤是在这里：
	在新建TcpConnection时从event loop pool里挑选一个loop给TcpConnection。
	即是，TcpServer的eventloop只用来Accepte新连接，而新连接会用其他eventloop
	来执行IO。 
  
  */
  
  
  EventLoop* ioLoop = threadPool_->getNextLoop();//从event loop pool中挑选一个loop给新连接使用。
  char buf[64];
  snprintf(buf, sizeof buf, "-%s#%d", ipPort_.c_str(), nextConnId_);
  ++nextConnId_;
  string connName = name_ + buf;

  LOG_INFO << "TcpServer::newConnection [" << name_
           << "] - new connection [" << connName
           << "] from " << peerAddr.toIpPort();
  InetAddress localAddr(sockets::getLocalAddr(sockfd));
  // FIXME poll with zero timeout to double confirm the new connection
  // FIXME use make_shared if necessary
  TcpConnectionPtr conn(new TcpConnection(ioLoop,//获得的loop,传递下去
                                          connName,
                                          sockfd,
                                          localAddr,
                                          peerAddr));
  connections_[connName] = conn;
  conn->setConnectionCallback(connectionCallback_);
  conn->setMessageCallback(messageCallback_);
  conn->setWriteCompleteCallback(writeCompleteCallback_);
  conn->setCloseCallback(
      boost::bind(&TcpServer::removeConnection, this, _1)); // FIXME: unsafe
	  //但是为什么一定要在runInLoop()中执行，没明白,是下面这个原因？
	  //因为如果是多线程模式的话，新的TcpConnection已经分配了新的IO线程，
	  //如果此时不调用ioLoop->runInLoop()接口，那么将是跨线程调用（不安全）。
	  /*
	  注意此处每一个连接绑定一个通道（成员变量），每一个通道绑定一个eventloop(构造函数初始化的时候已传递的参数).
	  首先更新channel需要关心的事件，随即调用eventloop中的update，
	  然后再调用poller的updateChannel(每一个loop绑定一个poller),将描述符注册到指定的loop的poller中
	  */
  ioLoop->runInLoop(boost::bind(&TcpConnection::connectEstablished, conn));
}

void TcpServer::removeConnection(const TcpConnectionPtr& conn)
{
  // FIXME: unsafe
  //因为在TcpConnection里会在自己的ioloop线程里调用removeConnection()
  //此接口会回调cpServer::removeConnection(),那么就是跨线程了。因此需要转移到
  //TcpServer的ioloop线程（无锁的）中执行。
  loop_->runInLoop(boost::bind(&TcpServer::removeConnectionInLoop, this, conn));
}

void TcpServer::removeConnectionInLoop(const TcpConnectionPtr& conn)
{
  loop_->assertInLoopThread();
  LOG_INFO << "TcpServer::removeConnectionInLoop [" << name_
           << "] - connection " << conn->name();
  size_t n = connections_.erase(conn->name());
  (void)n;
  assert(n == 1);
  EventLoop* ioLoop = conn->getLoop();
  //这里也是跨线程调用，先获取其所在的IO线程，然后再转移到其中进行操作。
  //说是为保证TcpConnection的ConnectionCallback始终在其ioloop中回调？为方便客户端的代码编写？
  //话说一定用下面的接口，否则会出现对象生命期管理问题？
  ioLoop->queueInLoop(
      boost::bind(&TcpConnection::connectDestroyed, conn));
}

