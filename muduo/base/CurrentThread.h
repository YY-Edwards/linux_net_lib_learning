// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_CURRENTTHREAD_H
#define MUDUO_BASE_CURRENTTHREAD_H

#include <stdint.h>

namespace muduo
{
namespace CurrentThread
{
  // internal
  
  /*
	
	__thread变量每一个线程有一份独立实体，各个线程的值互不干扰。
	可以用来修饰那些带有全局性且值可能变，但是又不值得用全局变量保护的变量。
  
  */
  extern __thread int t_cachedTid;//线程私有资源，tid
  extern __thread char t_tidString[32];
  extern __thread int t_tidStringLength;
  extern __thread const char* t_threadName;
  void cacheTid();

  
  /*
	
	内联函数最重要的使用地方是用于类的存取函数。

	在c++中，为了解决一些频繁调用的小函数大量消耗栈空间或者是叫栈内存的问题，特别的引入了inline修饰符，表示为内联函数。 

　　在系统下，栈空间是有限的，如果频繁大量的使用就会造成因栈空间不足所造成的程序出错的问题，函数的死循环递归调用的最终结果就是导致栈内存空间枯竭。

	说到这里很多人可能会问，既然inline这么好，还不如把所谓的函数都声明成inline，
	嗯，这个问题是要注意的，inline的使用是有所限制的，inline只适合函数体内代码简单的函数使用，
	不能包含复杂的结构控制语句例如while switch，并且不能内联函数本身不能是直接递归函数(自己内部还调用自己的函数)。

　　说到这里我们不得不说一下在c语言中广泛被使用的#define语句，是的define的确也可以做到inline的这些工作，但是define是会产生副作用的，尤其是不同类型参数所导致的错误，由此可见inline有更强的约束性和能够让编译器检查出更多错误的特性，在c++中是不推荐使用define的。

  
  */
  
  inline int tid()
  {
    if (__builtin_expect(t_cachedTid == 0, 0))
    {
      cacheTid();
    }
    return t_cachedTid;
  }

  inline const char* tidString() // for logging
  {
    return t_tidString;
  }

  inline int tidStringLength() // for logging
  {
    return t_tidStringLength;
  }

  inline const char* name()
  {
    return t_threadName;
  }

  bool isMainThread();

  void sleepUsec(int64_t usec);
}
}

#endif
