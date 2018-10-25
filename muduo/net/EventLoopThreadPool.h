// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is an internal header file, you should not include this.

#ifndef MUDUO_NET_EVENTLOOPTHREADPOOL_H
#define MUDUO_NET_EVENTLOOPTHREADPOOL_H

#include <muduo/base/Types.h>

#include <vector>
#include <boost/function.hpp>
#include <boost/noncopyable.hpp>
#include <boost/ptr_container/ptr_vector.hpp>

namespace muduo
{

namespace net
{

class EventLoop;
class EventLoopThread;

class EventLoopThreadPool : boost::noncopyable
{
 public:
  typedef boost::function<void(EventLoop*)> ThreadInitCallback;

  EventLoopThreadPool(EventLoop* baseLoop, const string& nameArg);
  ~EventLoopThreadPool();
  void setThreadNum(int numThreads) { numThreads_ = numThreads; }
  void start(const ThreadInitCallback& cb = ThreadInitCallback());

  // valid after calling start()
  /// round-robin
  EventLoop* getNextLoop();

  /// with the same hash code, it will always return the same EventLoop
  // 如果loops_为空，则loop指向baseLoop_
  // 如果不为空，按照round-robin（RR，轮叫）的调度方式选择一个EventLoop
  EventLoop* getLoopForHash(size_t hashCode);

  std::vector<EventLoop*> getAllLoops();

  bool started() const
  { return started_; }

  const string& name() const
  { return name_; }

 private:

  EventLoop* baseLoop_;// 与Acceptor所属EventLoop相同
  string name_;
  bool started_;
  int numThreads_;		//线程数，除去mainReactor
  int next_;			// 新连接到来，所选择的EventLoop对象下标
  /*
  //boost::ptr_vector 专门用于动态分配的对象
  //boost::ptr_vector保存的是“own”的对象;
  //std::vector<boost::shared_ptr<>>保存的对象可以被别人own
  //然后，从效率上来说，ptr_vector显然要更好一点，因为创建shared_ptr还是有开销的。
  */
  /*
  
  在合适的语义下，ptr_vector最好（好吧，我更习惯用boost…）
  能体会到，c++0x在标准库下有着更好的效率，至于是不是适合项目使用，看项目情况吧
  在不用考虑效率的时候（个人觉得，开发中不需要在执行效率上太抠，把优化留到实际使用发现性能之后），
  vector<shared_ptr>最万能。
  
  */
  boost::ptr_vector<EventLoopThread> threads_;   // IO线程列表
  std::vector<EventLoop*> loops_;				  // EventLoop列表
};

}
}

#endif  // MUDUO_NET_EVENTLOOPTHREADPOOL_H
