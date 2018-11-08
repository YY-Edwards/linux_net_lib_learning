#include <muduo/base/AsyncLogging.h>
#include <muduo/base/LogFile.h>
#include <muduo/base/Timestamp.h>

#include <stdio.h>

using namespace muduo;

AsyncLogging::AsyncLogging(const string& basename,
                           off_t rollSize,
                           int flushInterval)
  : flushInterval_(flushInterval),
    running_(false),
    basename_(basename),
    rollSize_(rollSize),
    thread_(boost::bind(&AsyncLogging::threadFunc, this), "Logging"),
    latch_(1),
    mutex_(),
    cond_(mutex_),
    currentBuffer_(new Buffer),
    nextBuffer_(new Buffer),
    buffers_()
{
  currentBuffer_->bzero();
  nextBuffer_->bzero();
  buffers_.reserve(16);
}

void AsyncLogging::append(const char* logline, int len)
{
  muduo::MutexLockGuard lock(mutex_);
  if (currentBuffer_->avail() > len)//没有写满继续追加,一个至少可以存储1000以上的日志
  {
    currentBuffer_->append(logline, len);
  }
  else//写满，则推送一个当前buff地址到队列中
  {
    buffers_.push_back(currentBuffer_.release());//unique_ptr转移控制权，并清空

    if (nextBuffer_)//下一个地址有效
    {
		//这里必须使用转移操作，因为BufferPtr相当于是std::unique_ptr不准复制与拷贝。
      currentBuffer_ = boost::ptr_container::move(nextBuffer_);//将下一个地址移动到当前buff，nextbuffer=NULL并释放
    }
    else//buffer用完，下一个地址无效
    {
      currentBuffer_.reset(new Buffer); // Rarely happens，重新分配新的buff
    }
    currentBuffer_->append(logline, len);//追加剩余数据
    cond_.notify();//并通知后台写buff
  }
}

void AsyncLogging::threadFunc()
{
  assert(running_ == true);
  latch_.countDown();
  LogFile output(basename_, rollSize_, false);
  //后端备用buffer
  BufferPtr newBuffer1(new Buffer);
  BufferPtr newBuffer2(new Buffer);
  newBuffer1->bzero();
  newBuffer2->bzero();
  BufferVector buffersToWrite;
  buffersToWrite.reserve(16);
  while (running_)
  {
    assert(newBuffer1 && newBuffer1->length() == 0);
    assert(newBuffer2 && newBuffer2->length() == 0);
    assert(buffersToWrite.empty());

    {
      muduo::MutexLockGuard lock(mutex_);
      if (buffers_.empty())  // unusual usage!,如果缓冲队列为空，则睡眠3秒
      {
        cond_.waitForSeconds(flushInterval_);//条件信号已绑定mutex_，进入睡眠时会释放互斥锁
      }
	  //下面这句可能出现4种情况：
	  //1.append写满，触发通知，然后则切换到线程threadFunc(),那么此时的currentBuffer_内容为空。
	  //那么此时push_back()动作时
	  //buffers_里有两个对象（一个满，一个为空）。
	  
	  //2.append写满，触发通知，由于写入太快线程上下文不切换，继续执行append()函数（此时暂考虑在新分配的
	  //currentBuffer_里写入部分数据，未写满），然后切换线程执行threadFunc()函数，那么此时push_back()动作时
	  //buffers_里有两个对象，且都有数据（一个满，一个未满）。
	  
	  //3.append未满，3秒到，后端激活
	  //4.append未满，3秒到，
      buffers_.push_back(currentBuffer_.release());//推送一个当前buff地址到队列中，转移控制权，并清空
	  
	  //马上重新分配一个，保证append()函数永远有地可写。
      currentBuffer_ = boost::ptr_container::move(newBuffer1);//用newbuffer1重新填充currentbuffer:将newbuffer1的地址移动到当前buffer
      buffersToWrite.swap(buffers_);//临时buffer交换，一次性交换出缓冲器里的所有数据，因临时buff为空，则缓冲器里的数目亦为空。
      if (!nextBuffer_)//如果newbuffer1已经为空，则用newbuffer2重新填充nextbuffer
      {
        nextBuffer_ = boost::ptr_container::move(newBuffer2);
      }
    }

    assert(!buffersToWrite.empty());

    if (buffersToWrite.size() > 25)//如果缓冲队列数目超过25，前端日志堆积
    {
      char buf[256];
      snprintf(buf, sizeof buf, "Dropped log messages at %s, %zd larger buffers\n",
               Timestamp::now().toFormattedString().c_str(),
               buffersToWrite.size()-2);
      fputs(buf, stderr);
      output.append(buf, static_cast<int>(strlen(buf)));
      buffersToWrite.erase(buffersToWrite.begin()+2, buffersToWrite.end());//从第3个开始，一直到缓冲器末尾，释放所有对象内存
    }

    for (size_t i = 0; i < buffersToWrite.size(); ++i)//遍历缓冲器队列，并追加数据
    {
      // FIXME: use unbuffered stdio FILE ? or use ::writev ?
      output.append(buffersToWrite[i].data(), buffersToWrite[i].length());
    }

    if (buffersToWrite.size() > 2)//若缓冲器大于2，则重置缓冲器大小，新分配的是在这里释放的？
    {
      // drop non-bzero-ed buffers, avoid trashing
	  //If n is smaller than the current container size, 
	  //the content is reduced to its first n elements, removing those beyond (and destroying them).
      buffersToWrite.resize(2);//将最后的新分配的全部释放掉
    }

    if (!newBuffer1)//如果newbuffer1已经为空，从队列中取出一个地址填充newbuffer1
    {
      assert(!buffersToWrite.empty());
      newBuffer1 = buffersToWrite.pop_back();
      newBuffer1->reset();//重置buffer指针
    }

    if (!newBuffer2)//如果newbuffer2已经为空，从队列中取出一个地址填充newbuffer2，并在缓冲器里移除
    {
      assert(!buffersToWrite.empty());
      newBuffer2 = buffersToWrite.pop_back();
      newBuffer2->reset();
    }

    buffersToWrite.clear();//清空临时buff,但并不释放其所包含对象指针的内存
    output.flush();//刷新输出
  }
  output.flush();
}

