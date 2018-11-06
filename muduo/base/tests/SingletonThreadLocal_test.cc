#include <muduo/base/Singleton.h>
#include <muduo/base/CurrentThread.h>
#include <muduo/base/ThreadLocal.h>
#include <muduo/base/Thread.h>

#include <boost/bind.hpp>
#include <boost/noncopyable.hpp>
#include <stdio.h>
#include <unistd.h>

class Test : boost::noncopyable
{
 public:
  Test()
  {
    printf("tid=%d, constructing %p\n", muduo::CurrentThread::tid(), this);
  }

  ~Test()
  {
    printf("tid=%d, destructing %p %s\n", muduo::CurrentThread::tid(), this, name_.c_str());
  }

  const muduo::string& name() const { return name_; }
  void setName(const muduo::string& n) { name_ = n; }

 private:
  muduo::string name_;
};


/*

muduo::Singleton<muduo::ThreadLocal<Test> >::instance() 
保证不同线程调用返回的都是同一个ThreadLocal<Test> 对象，

而不同线程调用ThreadLocal<Test>.value(); 返回的是不同的Test对象，
即在不同线程中各有一份实际数据。

*/
#define STL muduo::Singleton<muduo::ThreadLocal<Test> >::instance().value()
//不同的线程中，具有相同的ThreadLocal<Test>对象；不同的Test对象。从外层到里

void print()
{
  printf("tid=%d, %p name=%s\n",
         muduo::CurrentThread::tid(),
         &STL,
         STL.name().c_str());
}

void threadFunc(const char* changeTo)
{
  print();
  STL.setName(changeTo);
  sleep(1);
  print();
}


int main()
{
  STL.setName("main one");
  muduo::Thread t1(boost::bind(threadFunc, "thread1"));
  muduo::Thread t2(boost::bind(threadFunc, "thread2"));
  t1.start();
  t2.start();
  t1.join();
  print();
  t2.join();
  pthread_exit(0);
}
