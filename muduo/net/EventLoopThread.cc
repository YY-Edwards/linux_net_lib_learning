// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include <muduo/net/EventLoopThread.h>

#include <muduo/net/EventLoop.h>

#include <boost/bind.hpp>

using namespace muduo;
using namespace muduo::net;


EventLoopThread::EventLoopThread(const ThreadInitCallback& cb,
                                 const string& name)
  : loop_(NULL),
    exiting_(false),
    thread_(boost::bind(&EventLoopThread::threadFunc, this), name),
    mutex_(),
    cond_(mutex_),
    callback_(cb)
{
}

EventLoopThread::~EventLoopThread()
{
  exiting_ = true;
  if (loop_ != NULL) // not 100% race-free, eg. threadFunc could be running callback_.
  {
    // still a tiny chance to call destructed object, if threadFunc exits just now.
    // but when EventLoopThread destructs, usually programming is exiting anyway.
    loop_->quit();
    thread_.join();
  }
}

EventLoop* EventLoopThread::startLoop()
{
  assert(!thread_.started());
  //thread_(boost::bind(&EventLoopThread::threadFunc, this), name)
  //构造的时候将函数接口传递进去
  thread_.start();//调用pthread_create
//LOG_TRACE <<"create and start EventLoopThread::threadFunc";
//启动线程，此时有两个线程在运行，
//一个是调用EventLoopThread::startLoop()的线程，
//一个是执行EventLoopThread::threadFunc()的线程（IO线程）.
  {
    MutexLockGuard lock(mutex_);
    while (loop_ == NULL)
    {
      cond_.wait();
    }
  }

  return loop_;
}

void EventLoopThread::threadFunc()
{
  EventLoop loop;

  if (callback_)//如果有回调，则先执行回调
  {
    callback_(&loop);
  }

  {
    MutexLockGuard lock(mutex_);
   // 一般情况是EventLoopThread对象先析构，析构函数调用loop_->quit() 使得loop.loop() 退出循环        
   // 这样threadFunc 退出，loop栈上对象析构，loop_ 指针失效，但此时已经不会再通过loop_ 访问loop，        
   // 故不会有问题。

    loop_ = &loop;
    cond_.notify();
  }

  loop.loop();//若不退出，则loop资源一直存在不会释放
  //assert(exiting_);
  loop_ = NULL;
}

