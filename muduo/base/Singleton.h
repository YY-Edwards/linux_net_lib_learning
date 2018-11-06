// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_SINGLETON_H
#define MUDUO_BASE_SINGLETON_H

#include <boost/noncopyable.hpp>
#include <assert.h>
#include <stdlib.h> // atexit
#include <pthread.h>

namespace muduo
{

namespace detail
{
// This doesn't detect inherited member functions!
// http://stackoverflow.com/questions/1966362/sfinae-to-check-for-inherited-member-functions
template<typename T>
struct has_no_destroy
{
	//类/结构体的成员模板
#ifdef __GXX_EXPERIMENTAL_CXX0X__
  template <typename C> static char test(decltype(&C::no_destroy));
#else
  template <typename C> static char test(typeof(&C::no_destroy));
#endif
  template <typename C> static int32_t test(...);
  const static bool value = sizeof(test<T>(0)) == 1;
};
}
//模板如果要让别的文件引用的话，全部实现都应该放在h里面
template<typename T>
class Singleton : boost::noncopyable
{
 public:
  static T& instance()
  {
    pthread_once(&ponce_, &Singleton::init);
    assert(value_ != NULL);
    return *value_;
  }

 private:
  Singleton();
  ~Singleton();

  static void init()
  {
    value_ = new T();
	// //保证支持销毁方法才会注册atexit
    if (!detail::has_no_destroy<T>::value)//据此，判断是否注册资源释放接口
    {
		/*
		
		如果需要正确的释放资源的话，
		可以在init函数里面使用glibc提供的atexit函数来注册相关的资源释放函数，
		从而达到了只在进程退出时才释放资源的这一目的。
		
		*/
      ::atexit(destroy);
    }
  }

  static void destroy()
  {
    typedef char T_must_be_complete_type[sizeof(T) == 0 ? -1 : 1];
    T_must_be_complete_type dummy; (void) dummy;
	//现在A只是前向声明，是不完全类型，那么delete p会出问题，但在编译时只是报警告。

    delete value_;
    value_ = NULL;
  }

 private:
  static pthread_once_t ponce_;
  static T*             value_;
};

//定义模板里的静态变量，因为所有的实现都在h文件里了。
template<typename T>
//类模板的每个实例都有一个独有的static对象。
//因此，与定义模板的成员函数类似，我们将static数据成员也定义为模板，并初始化。
pthread_once_t Singleton<T>::ponce_ = PTHREAD_ONCE_INIT;

template<typename T>
T* Singleton<T>::value_ = NULL;

}
#endif

