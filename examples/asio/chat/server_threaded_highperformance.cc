#include "codec.h"

#include <muduo/base/Logging.h>
#include <muduo/base/Mutex.h>
#include <muduo/base/ThreadLocalSingleton.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/TcpServer.h>

#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>

#include <set>
#include <stdio.h>
#include <unistd.h>

using namespace muduo;
using namespace muduo::net;

class ChatServer : boost::noncopyable
{
 public:
  ChatServer(EventLoop* loop,
             const InetAddress& listenAddr)
  : server_(loop, listenAddr, "ChatServer"),
    codec_(boost::bind(&ChatServer::onStringMessage, this, _1, _2, _3))
  {
    server_.setConnectionCallback(
        boost::bind(&ChatServer::onConnection, this, _1));
    server_.setMessageCallback(
        boost::bind(&LengthHeaderCodec::onMessage, &codec_, _1, _2, _3));
  }

  void setThreadNum(int numThreads)
  {
    server_.setThreadNum(numThreads);
  }

  void start()
  {
    server_.setThreadInitCallback(boost::bind(&ChatServer::threadInit, this, _1));
    server_.start();
  }

 private:
  void onConnection(const TcpConnectionPtr& conn)
  {
    LOG_INFO << conn->localAddress().toIpPort() << " -> "
             << conn->peerAddress().toIpPort() << " is "
             << (conn->connected() ? "UP" : "DOWN");
	//与其他两个多线的比，这里是无锁的。
    if (conn->connected())
    {
      LocalConnections::instance().insert(conn);
    }
    else
    {
	//distributeMessage是注册在所属ioloop中执行，因此不会存在竞争。
      LocalConnections::instance().erase(conn);
    }
	
	LOG_DEBUG << "tid=" << muduo::CurrentThread::tid()
	<<", addr:"<<&LocalConnections::instance();
  }

  void onStringMessage(const TcpConnectionPtr&,
                       const string& message,
                       Timestamp)
  {
	 /*把消息"转发"作为IO线程的任务来处理*/  
    EventLoop::Functor f = boost::bind(&ChatServer::distributeMessage, this, message);
    LOG_DEBUG;
	/*

	 转发消息给所有客户端，高效率(多线程来转发),转发到不同的IO线程,

	 */
    MutexLockGuard lock(mutex_);
	/*for 循环和f达到异步进行*/
    for (std::set<EventLoop*>::iterator it = loops_.begin();
        it != loops_.end();
        ++it)
    {
		/*

		1.让对应的IO线程来执行distributeMessage 

		2.distributeMessage放到IO线程队列中执行，因此，这里的mutex_锁竞争大大减小

		3.distributeMesssge 不受mutex_保护

        */
	//如果是跨线程的调用，那么会启用wakeup来唤醒相对应的ioloop来执行f
	//看作事件触发的话，即是先注册再唤醒，再执行。
      (*it)->queueInLoop(f);
    }
    LOG_DEBUG;
  }

  typedef std::set<TcpConnectionPtr> ConnectionList;

  void distributeMessage(const string& message)
  {
    LOG_DEBUG << "begin";
	
    for (ConnectionList::iterator it = LocalConnections::instance().begin();
        it != LocalConnections::instance().end();
        ++it)
    {
      codec_.send(get_pointer(*it), message);
    }
    LOG_DEBUG << "end";
  }

  /*IO线程（EventLoopThread::threadFunc()）执行前时的前回调函数*/
  void threadInit(EventLoop* loop)
  {
	//第一次进入分配内存后，则不再执行此函数
    assert(LocalConnections::pointer() == NULL);
    LocalConnections::instance();
	 /*实例化一个对象*/
    assert(LocalConnections::pointer() != NULL);
    MutexLockGuard lock(mutex_);
    loops_.insert(loop);//累积每一个loop（thread）
	
	LOG_DEBUG << "tid=" << muduo::CurrentThread::tid()
	<<", addr:"<<&LocalConnections::instance()
	<<", loops_.size: "<< loops_.size();
	
  }

  TcpServer server_;
  LengthHeaderCodec codec_;
  
    /*线程局部单例变量，每个线程都有一个connections_(连接列表)实例*/
  typedef ThreadLocalSingleton<ConnectionList> LocalConnections;

  MutexLock mutex_;
  std::set<EventLoop*> loops_;
};

int main(int argc, char* argv[])
{
  muduo::Logger::setLogLevel(Logger::TRACE);	
  LOG_INFO << "pid = " << getpid();
  if (argc > 1)
  {
    EventLoop loop;
	//多个IO线程可以用IO线程池来管理，对应的类是EventLoopThreadPool 
    uint16_t port = static_cast<uint16_t>(atoi(argv[1]));
    InetAddress serverAddr(port);
    ChatServer server(&loop, serverAddr);
    if (argc > 2)
    {
      server.setThreadNum(atoi(argv[2]));
    }
    server.start();
    loop.loop();
  }
  else
  {
    printf("Usage: %s port [thread_num]\n", argv[0]);
  }
}


