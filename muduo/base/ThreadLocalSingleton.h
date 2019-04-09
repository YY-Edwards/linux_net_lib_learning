// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_THREADLOCALSINGLETON_H
#define MUDUO_BASE_THREADLOCALSINGLETON_H

#include <boost/noncopyable.hpp>
#include <assert.h>
#include <pthread.h>

namespace muduo
{

template<typename T>
class ThreadLocalSingleton : boost::noncopyable
{
 public:

  static T& instance()
  {
    
	//t_value_若不声明为线程局部存储措施，
	//在单线程中是正确，但是多线程因为竞争会出现多次初始化的情况。
	/*
	但是声明为__thread 变量，则在不同的线程中都有一份独立的实例。
	*/

	if (!t_value_)
	{
      t_value_ = new T();
      deleter_.set(t_value_);
    }
    return *t_value_;
  }

  static T* pointer()
  {
    return t_value_;
  }

 private:
  ThreadLocalSingleton();
  ~ThreadLocalSingleton();

  static void destructor(void* obj)
  {
    assert(obj == t_value_);
    typedef char T_must_be_complete_type[sizeof(T) == 0 ? -1 : 1];
    T_must_be_complete_type dummy; (void) dummy;
    delete t_value_;
    t_value_ = 0;
  }

  //使用单独的类对线程局部数据进行封装，每个变量使用一个独立的pthread_key_t。
  class Deleter
  {
   public:
    Deleter()
    {
	  //不论哪个线程调用了 pthread_key_create()，所创建的 key 都是所有线程可以访问的，但各个线程可以根据自己的需要往 key 中填入不同的值，
	  //相当于提供了一个同名而不同值的全局变量(这个全局变量相对于拥有这个变量的线程来说)。
      pthread_key_create(&pkey_, &ThreadLocalSingleton::destructor);
    }

    ~Deleter()
    {
      pthread_key_delete(pkey_);
    }

    void set(T* newObj)
    {
      assert(pthread_getspecific(pkey_) == NULL);
      pthread_setspecific(pkey_, newObj);
    }

	//pkey_ 是同名且全局，但访问的内存空间并不是相同的一个
    pthread_key_t pkey_;
  };

  //deleter_ 是静态数据成员，为所有线程所共享；
  //t_value_ 虽然也是静态数据成员，但加了__thread 修饰符，故每一个线程都会有一份。
  //Deleter类是用来实现当某个线程执行完毕，执行注册的destructor函数，进而delete t_value_ 。
  //key 的删除在~Deleter() 中

  static __thread T* t_value_;
  static Deleter deleter_;
};

template<typename T>
__thread T* ThreadLocalSingleton<T>::t_value_ = 0;

template<typename T>
typename ThreadLocalSingleton<T>::Deleter ThreadLocalSingleton<T>::deleter_;

}
#endif
