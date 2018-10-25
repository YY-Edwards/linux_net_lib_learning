// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef __STDC_LIMIT_MACROS
#define __STDC_LIMIT_MACROS
#endif

#include <muduo/net/TimerQueue.h>

#include <muduo/base/Logging.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/Timer.h>
#include <muduo/net/TimerId.h>

#include <boost/bind.hpp>

#include <sys/timerfd.h>
#include <unistd.h>

namespace muduo
{
namespace net
{
namespace detail
{

int createTimerfd()
{
/*
与CLOCK_REALTIME相反，它是以绝对时间为准，获取的时间为系统最近一次重启到现在的时间，
更该系统时间对其没影响。
*/
  int timerfd = ::timerfd_create(CLOCK_MONOTONIC,
                                 TFD_NONBLOCK | TFD_CLOEXEC);
  if (timerfd < 0)
  {
    LOG_SYSFATAL << "Failed in timerfd_create";
  }
  return timerfd;
}

struct timespec howMuchTimeFromNow(Timestamp when)
{
  int64_t microseconds = when.microSecondsSinceEpoch()
                         - Timestamp::now().microSecondsSinceEpoch();
  if (microseconds < 100)
  {
    microseconds = 100;
  }
  struct timespec ts;
  ts.tv_sec = static_cast<time_t>(
      microseconds / Timestamp::kMicroSecondsPerSecond);
  ts.tv_nsec = static_cast<long>(
      (microseconds % Timestamp::kMicroSecondsPerSecond) * 1000);
  return ts;
}

void readTimerfd(int timerfd, Timestamp now)
{
  uint64_t howmany;
  ssize_t n = ::read(timerfd, &howmany, sizeof howmany);
  LOG_TRACE << "TimerQueue::handleRead() " << howmany << " at " << now.toString();
  if (n != sizeof howmany)
  {
    LOG_ERROR << "TimerQueue::handleRead() reads " << n << " bytes instead of 8";
  }
}

void resetTimerfd(int timerfd, Timestamp expiration)
{
  // wake up loop by timerfd_settime()
  struct itimerspec newValue;
  struct itimerspec oldValue;
  bzero(&newValue, sizeof newValue);
  bzero(&oldValue, sizeof oldValue);
  newValue.it_value = howMuchTimeFromNow(expiration);
  /*
	该函数的功能为启动和停止定时器。
	第一个参数fd为上面的timerfd_create()函数返回的定时器文件描述符，
	第二个参数flags为0表示相对定时器，为TFD_TIMER_ABSTIME表示绝对定时器，
	第三个参数new_value用来设置超时时间，为0表示停止定时器，
	第四个参数为原来的超时时间，一般设为NULL。
	
	需要注意的是我们可以通过clock_gettime获取当前时间，
	如果是绝对定时器，那么我们得获取1970.1.1到当前时间(CLOCK_REALTIME),再加上我们自己定的定时时间。
	若是相对定时，则要获取我们系统本次开机到目前的时间加我们要定的时常(即获取CLOCK_MONOTONIC时间)
	
	struct itimerspec {
               struct timespec it_interval;  // Interval for periodic timer 
               struct timespec it_value;     // Initial expiration 
           };

  */
  int ret = ::timerfd_settime(timerfd, 0, &newValue, &oldValue);
  if (ret)
  {
    LOG_SYSERR << "timerfd_settime()";
  }
}

}
}
}

using namespace muduo;
using namespace muduo::net;
using namespace muduo::net::detail;

TimerQueue::TimerQueue(EventLoop* loop)
  : loop_(loop),
    timerfd_(createTimerfd()),
    timerfdChannel_(loop, timerfd_),
    timers_(),
    callingExpiredTimers_(false)
{
  timerfdChannel_.setReadCallback(
      boost::bind(&TimerQueue::handleRead, this));
  // we are always reading the timerfd, we disarm it with timerfd_settime.
  timerfdChannel_.enableReading();
}

TimerQueue::~TimerQueue()
{
  timerfdChannel_.disableAll();
  timerfdChannel_.remove();
  ::close(timerfd_);
  // do not remove channel, since we're in EventLoop::dtor();
  for (TimerList::iterator it = timers_.begin();
      it != timers_.end(); ++it)
  {
    delete it->second;
  }
}

TimerId TimerQueue::addTimer(const TimerCallback& cb,//非本线程调用
                             Timestamp when,
                             double interval)
{
  Timer* timer = new Timer(cb, when, interval);
   //添加到IO线程中等待被调用，线程安全的不需要锁
  loop_->runInLoop(
      boost::bind(&TimerQueue::addTimerInLoop, this, timer));
  return TimerId(timer, timer->sequence());
}

#ifdef __GXX_EXPERIMENTAL_CXX0X__
TimerId TimerQueue::addTimer(TimerCallback&& cb,
                             Timestamp when,
                             double interval)
{
	//std::move语句可以将左值变为右值而避免拷贝构造
	/*
	通过std::move，可以避免不必要的拷贝操作。
	std::move是为性能而生。
	std::move是将对象的状态或者所有权从一个对象转移到另一个对象，
	只是转移，没有内存的搬迁或者内存拷贝。
	*/
  Timer* timer = new Timer(std::move(cb), when, interval);//构建新的timer对象
  loop_->runInLoop(
      boost::bind(&TimerQueue::addTimerInLoop, this, timer));
  return TimerId(timer, timer->sequence());
}
#endif

void TimerQueue::cancel(TimerId timerId)
{
  loop_->runInLoop(
      boost::bind(&TimerQueue::cancelInLoop, this, timerId));
}

/*

涉及到定时器的操作添加删除都是由创建定时器的那个IO线程进行，
而其他线程需要添加定时器时，
需要将其设置为事件使得IO线程进行添加删除操作

*/
void TimerQueue::addTimerInLoop(Timer* timer)
{
  loop_->assertInLoopThread();
  //增加到定时器集合和活跃的定时器集合中，
  //如果时间比之前已有定时器短则返回True。
  bool earliestChanged = insert(timer);

  if (earliestChanged)
  {
	//当前时间需要修改时，更改定时器IO的时间 
    resetTimerfd(timerfd_, timer->expiration());
  }
}

void TimerQueue::cancelInLoop(TimerId timerId)
{
  loop_->assertInLoopThread();
  assert(timers_.size() == activeTimers_.size());
  ActiveTimer timer(timerId.timer_, timerId.sequence_);//转换为Timer
  ActiveTimerSet::iterator it = activeTimers_.find(timer);//查找到活跃定时器指针
  if (it != activeTimers_.end())
  {
    size_t n = timers_.erase(Entry(it->first->expiration(), it->first));//删除定时器集合中的Timer
    assert(n == 1); (void)n;
    delete it->first; // FIXME: no delete please释放Timer
    activeTimers_.erase(it);//删除活跃定时器中Timer
  }
  else if (callingExpiredTimers_)//正在调用过期定时器时，添加进cancelingTimers
  {
    cancelingTimers_.insert(timer);
  }
  assert(timers_.size() == activeTimers_.size());
}

void TimerQueue::handleRead()
{
  loop_->assertInLoopThread();
  Timestamp now(Timestamp::now());
  readTimerfd(timerfd_, now);//读取事件，如果不读取由于Epoll水平触发会导致一直有IO事件产生

  std::vector<Entry> expired = getExpired(now);//找到到期的定时器并移除

  callingExpiredTimers_ = true;//设置当前状态为调用定时器函数中
  cancelingTimers_.clear();//清空删除的定时器
  // safe to callback outside critical section
  for (std::vector<Entry>::iterator it = expired.begin();
      it != expired.end(); ++it)
  {
    it->second->run();//执行定时器绑定的函数
  }
  callingExpiredTimers_ = false;

  reset(expired, now);//如果有时间间隔的定时器需要重新添加进定时器
}

std::vector<TimerQueue::Entry> TimerQueue::getExpired(Timestamp now)
{
  assert(timers_.size() == activeTimers_.size());
  std::vector<Entry> expired;
  
  Entry sentry(now, reinterpret_cast<Timer*>(UINTPTR_MAX));
  //新建一个当前时间定时器,
  //用UINTPTR_MAX原因是默认排序
  //如果时间相等则地址大小排序，取最大防止漏掉定时器
  
  //返回第一个>=sentry的定时器,没有等于的，那会返回第一个大于sentry的值
  //使用指针找到当前时间最后一个定时器(第一个>=sentry的定时器)
  TimerList::iterator end = timers_.lower_bound(sentry);
  //这里断言判断，保证当前时间时小于找到的第一个>=sentry的定时器。
  //因为逻辑上，到期定时器先发生，然后生成一个当前时间定时器(sentry)，时间肯定大于之前到期的定时器，
  //然后查询定时器列表后，返回第一个>=sentry的定时器。如果找到，先去要滤掉刚好等于的那个定时器，理论上这应该还未发生。
  assert(end == timers_.end() || now < end->first);//找到指定定时器，并检查当前时间小于队列里的定时器定时时间
  /*
  
  这里，start和end是输入序列（假设有N各元素）的迭代器（iterator），container是一个容器，
  该容器的接口包含函数push_back。假设container开始是空的，那么copy完毕后它就包含N个元素，
  并且顺序与原来队列中的元素顺序一样。标准库提供的back_inserter模板函数很方便，
  因为它为container返回一个back_insert_iterator迭代器，
  这样，复制的元素都被追加到container的末尾了。 
  现在假设container开始非空（例如：container必须在循环中反复被使用好几次）。
  那么，要达到原来的目标，必须先调用clear函数然后才能插入新序列。
  这会导致旧的元素对象被析构，新添加进来的被构造。
  不仅如此，container自身使用的动态内存也会被释放然后又创建，就像list，map，set的节点。
  某些vector的实现在调用clear的时候甚至会释放所有内存。
  copy只负责复制，不负责申请空间，所以复制前必须有足够的空间。
  */
  
  LOG_TRACE << "find the timer. " ;
  
  //将开始到现在的定时器复制到expired返回，end之前不包括end。
  std::copy(timers_.begin(), end, back_inserter(expired));
  timers_.erase(timers_.begin(), end);//删除这一范围定时器

  for (std::vector<Entry>::iterator it = expired.begin();
      it != expired.end(); ++it)
  {
	//组装成定时器
    ActiveTimer timer(it->second, it->second->sequence());
    size_t n = activeTimers_.erase(timer);//删除活动定时器列表中的定时器
    assert(n == 1); (void)n;
  }

  assert(timers_.size() == activeTimers_.size());
  return expired;//所有到期的定时器返回
}

void TimerQueue::reset(const std::vector<Entry>& expired, Timestamp now)
{
  Timestamp nextExpire;

  for (std::vector<Entry>::const_iterator it = expired.begin();
      it != expired.end(); ++it)
  {
    ActiveTimer timer(it->second, it->second->sequence());
    if (it->second->repeat()
        && cancelingTimers_.find(timer) == cancelingTimers_.end())
    {
      it->second->restart(now);
      insert(it->second);
    }
    else
    {
      // FIXME move to a free list
      delete it->second; // FIXME: no delete please
    }
  }

  if (!timers_.empty())
  {
    nextExpire = timers_.begin()->second->expiration();
  }

  if (nextExpire.valid())
  {
    resetTimerfd(timerfd_, nextExpire);
  }
}

bool TimerQueue::insert(Timer* timer)
{
  loop_->assertInLoopThread();
  assert(timers_.size() == activeTimers_.size());
  bool earliestChanged = false;
  Timestamp when = timer->expiration();
  TimerList::iterator it = timers_.begin();
   //如果时间比之前已有定时器短则返回True。
  if (it == timers_.end() || when < it->first)
  {
    earliestChanged = true;
  }
  {
    std::pair<TimerList::iterator, bool> result
      = timers_.insert(Entry(when, timer));
    assert(result.second); (void)result;
  }
  {
    std::pair<ActiveTimerSet::iterator, bool> result
      = activeTimers_.insert(ActiveTimer(timer, timer->sequence()));
    assert(result.second); (void)result;
  }

  assert(timers_.size() == activeTimers_.size());
  return earliestChanged;
}

