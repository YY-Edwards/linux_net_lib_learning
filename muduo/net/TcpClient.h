// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is a public header file, it must only include public header files.

#ifndef MUDUO_NET_TCPCLIENT_H
#define MUDUO_NET_TCPCLIENT_H

#include <boost/noncopyable.hpp>

#include <muduo/base/Mutex.h>
#include <muduo/net/TcpConnection.h>

namespace muduo
{
namespace net
{

class Connector;
typedef boost::shared_ptr<Connector> ConnectorPtr;

class TcpClient : boost::noncopyable
{
 public:
  // TcpClient(EventLoop* loop);
  // TcpClient(EventLoop* loop, const string& host, uint16_t port);
  TcpClient(EventLoop* loop,
            const InetAddress& serverAddr,
            const string& nameArg);
  ~TcpClient();  // force out-line dtor, for scoped_ptr members.

  void connect();
  void disconnect();
  void stop();

  TcpConnectionPtr connection() const
  {
    MutexLockGuard lock(mutex_);
    return connection_;
  }

  EventLoop* getLoop() const { return loop_; }
  bool retry() const { return retry_; }
  void enableRetry() { retry_ = true; }

  const string& name() const
  { return name_; }

  /// Set connection callback.
  /// Not thread safe.
  void setConnectionCallback(const ConnectionCallback& cb)
  { connectionCallback_ = cb; }

  /// Set message callback.
  /// Not thread safe.
  void setMessageCallback(const MessageCallback& cb)
  { messageCallback_ = cb; }

  /// Set write complete callback.
  /// Not thread safe.
  /*
	在 C++11 之前，右值是不能被引用的，最大限度就是用常量引用绑定一个右值，如 : 
	const int &a = 1; 
  
	左值和右值的语法符号 
	左值的声明符号为”&”， 为了和左值区分，右值的声明符号为”&&”。
	
	临时对象是作为右值处理的。
	
	右值引用是用来支持转移语义的。
	
	转移语义可以将资源 ( 堆，系统对象等 ) 从一个对象转移到另一个对象，
	这样能够减少不必要的临时对象的创建、拷贝以及销毁，能够大幅度提高 C++ 应用程序的性能
    
	转移语义是和拷贝语义相对的，可以类比文件的剪切与拷贝，
	当我们将文件从一个目录拷贝到另一个目录时，速度比剪切慢很多。
	通过转移语义，临时对象中的资源能够转移其它的对象里
	
	
	有了右值引用和转移语义，我们在设计和实现类时，对于需要动态申请大量资源的类，
	应该设计转移构造函数和转移赋值函数，以提高应用程序的效率。 
	
	C++11 中定义的 T&& 的推导规则为：右值实参为右值引用，左值实参仍然为左值引用。 
    一句话，就是参数的属性不变。这样也就完美的实现了参数的完整传递。 
	
  */
  void setWriteCompleteCallback(const WriteCompleteCallback& cb)
  { writeCompleteCallback_ = cb; }

#ifdef __GXX_EXPERIMENTAL_CXX0X__
  void setConnectionCallback(ConnectionCallback&& cb)
  { connectionCallback_ = std::move(cb); }
  void setMessageCallback(MessageCallback&& cb)
  { messageCallback_ = std::move(cb); }
  void setWriteCompleteCallback(WriteCompleteCallback&& cb)
  { writeCompleteCallback_ = std::move(cb); }
#endif

 private:
  /// Not thread safe, but in loop
  void newConnection(int sockfd);
  /// Not thread safe, but in loop
  void removeConnection(const TcpConnectionPtr& conn);

  EventLoop* loop_;
  ConnectorPtr connector_; // avoid revealing Connector
  const string name_;
  ConnectionCallback connectionCallback_;
  MessageCallback messageCallback_;
  WriteCompleteCallback writeCompleteCallback_;
  bool retry_;   // atomic
  bool connect_; // atomic
  // always in loop thread
  int nextConnId_;
  mutable MutexLock mutex_;
  TcpConnectionPtr connection_; // @GuardedBy mutex_
};

}
}

#endif  // MUDUO_NET_TCPCLIENT_H
