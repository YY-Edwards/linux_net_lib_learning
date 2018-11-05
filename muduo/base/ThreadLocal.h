// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_THREADLOCAL_H
#define MUDUO_BASE_THREADLOCAL_H

#include <muduo/base/Mutex.h>  // MCHECK

#include <boost/noncopyable.hpp>
#include <pthread.h>

namespace muduo
{
//更高效的是使用< __thread > 类型
/*
1、在单线程程序中，我们经常要用到"全局变量"以实现多个函数间共享数据。

2、在多线程环境下，由于数据空间是共享的，因此全局变量也为所有线程所共有。

3、但有时应用程序设计中有必要提供线程私有的全局变量，仅在某个线程中有效，但却可以跨多个函数访问。

4、POSIX线程库通过维护一定的数据结构来解决这个问题，这些数据称为（Thread-specific Data，或 TSD）。

5、线程特定数据也称为线程本地存储TLS（Thread-local storage）

6、对于POD类型的线程本地存储，可以用__thread关键字
*/





/*

函数 pthread_key_create() 用来创建线程私有数据。该函数从 TSD 池中分配一项，将其地址值赋给 key 供以后访问使用。

第 2 个参数是一个销毁函数，它是可选的，可以为 NULL，为 NULL 时，则系统调用默认的销毁函数进行相关的数据注销。

如果不为空，则在线程退出时(调用 pthread_exit() 函数)时将以 key 锁关联的数据作为参数调用它，以释放分配的缓冲区，或是关闭文件流等。

不论哪个线程调用了 pthread_key_create()，所创建的 key 都是所有线程可以访问的，但各个线程可以根据自己的需要往 key 中填入不同的值，相当于提供了一个同名而不同值的全局变量(这个全局变量相对于拥有这个变量的线程来说)。

注销一个 TSD 使用 pthread_key_delete() 函数。该函数并不检查当前是否有线程正在使用该 TSD，也不会调用清理函数(destructor function)，而只是将 TSD 释放以供下一次调用 pthread_key_create() 使用。

在 LinuxThread 中，它还会将与之相关的线程数据项设置为 NULL。


*/
template<typename T>
class ThreadLocal : boost::noncopyable
{
 public:
  ThreadLocal()
  {
    MCHECK(pthread_key_create(&pkey_, &ThreadLocal::destructor));
  }

  ~ThreadLocal()
  {
    MCHECK(pthread_key_delete(pkey_));
  }

  T& value()
  {
    T* perThreadValue = static_cast<T*>(pthread_getspecific(pkey_));
    if (!perThreadValue)
    {
      T* newObj = new T();
      MCHECK(pthread_setspecific(pkey_, newObj));
      perThreadValue = newObj;
    }
    return *perThreadValue;
  }

 private:

  static void destructor(void *x)
  {
    T* obj = static_cast<T*>(x);
    typedef char T_must_be_complete_type[sizeof(T) == 0 ? -1 : 1];
    T_must_be_complete_type dummy; (void) dummy;
    delete obj;
  }

 private:
  pthread_key_t pkey_;
};

}
#endif
