#include <muduo/base/LogFile.h>

#include <muduo/base/FileUtil.h>
#include <muduo/base/ProcessInfo.h>

#include <assert.h>
#include <stdio.h>
#include <time.h>

using namespace muduo;

LogFile::LogFile(const string& basename,
                 off_t rollSize,
                 bool threadSafe,
                 int flushInterval,
                 int checkEveryN)
  : basename_(basename),
    rollSize_(rollSize),
    flushInterval_(flushInterval),
    checkEveryN_(checkEveryN),
    count_(0),
    mutex_(threadSafe ? new MutexLock : NULL),//不是线程安全就不需要构造mutex_
    startOfPeriod_(0),
    lastRoll_(0),
    lastFlush_(0)
{
  assert(basename.find('/') == string::npos);
  rollFile();
}

LogFile::~LogFile()
{
}

void LogFile::append(const char* logline, int len)
{
  if (mutex_)
  {
    MutexLockGuard lock(*mutex_);
    append_unlocked(logline, len);
  }
  else
  {
    append_unlocked(logline, len);
  }
}

void LogFile::flush()
{
  if (mutex_)
  {
    MutexLockGuard lock(*mutex_);
    file_->flush();
  }
  else
  {
    file_->flush();
  }
}

void LogFile::append_unlocked(const char* logline, int len)
{
//调用成员变量file_的方法，
//也就是FileUtil.h中的AppendFile的不加锁apeend方法
  file_->append(logline, len);

  if (file_->writtenBytes() > rollSize_)
  {
    rollFile();//转储
  }
  else
  {
    ++count_;//增加行数
    if (count_ >= checkEveryN_)
    {
      count_ = 0;
	  //取得从1970年1月1日至今的秒数。
      time_t now = ::time(NULL);
      time_t thisPeriod_ = now / kRollPerSeconds_ * kRollPerSeconds_;
      if (thisPeriod_ != startOfPeriod_)//比较事件是否相等，不等就是第二天0点，那么滚动
      {
        rollFile();
      }
      else if (now - lastFlush_ > flushInterval_)//判断是否超过flush间隔时间(默认值3s)，超过了就flush，否则什么都不做。
      {
        lastFlush_ = now;
        file_->flush();
      }
    }
  }
}

bool LogFile::rollFile()
{
  time_t now = 0;
  string filename = getLogFileName(basename_, &now);
  //注意，这里先除KRollPerSeconds然后乘KPollPerSeconds表示对齐值KRollPerSeconds的整数倍，
  //也就是事件调整到当天零点(/除法会引发取整)
  //表示start对齐到kR的整数倍，也就是时间调整到当天零时
  time_t start = now / kRollPerSeconds_ * kRollPerSeconds_;

  if (now > lastRoll_)
  {
    lastRoll_ = now;
    lastFlush_ = now;
    startOfPeriod_ = start;
    file_.reset(new FileUtil::AppendFile(filename));//新建文本
    return true;
  }
  return false;
}

string LogFile::getLogFileName(const string& basename, time_t* now)
{
  string filename;
  filename.reserve(basename.size() + 64);
  filename = basename;

  char timebuf[32];
  struct tm tm;
  *now = time(NULL);
  gmtime_r(now, &tm); // FIXME: localtime_r ?
  strftime(timebuf, sizeof timebuf, ".%Y%m%d-%H%M%S.", &tm);
  filename += timebuf;

  filename += ProcessInfo::hostname();

  char pidbuf[32];
  snprintf(pidbuf, sizeof pidbuf, ".%d", ProcessInfo::pid());
  filename += pidbuf;

  filename += ".log";

  return filename;
}

