#ifndef MUDUO_BASE_LOGFILE_H
#define MUDUO_BASE_LOGFILE_H

#include <muduo/base/Mutex.h>
#include <muduo/base/Types.h>

#include <boost/noncopyable.hpp>
#include <boost/scoped_ptr.hpp>

namespace muduo
{

namespace FileUtil
{
class AppendFile;
}

class LogFile : boost::noncopyable
{
 public:
  LogFile(const string& basename,
          off_t rollSize,
          bool threadSafe = true,
          int flushInterval = 3,
          int checkEveryN = 1024); //默认的分割行数为1024
  ~LogFile();

  void append(const char* logline, int len);
  void flush();
  bool rollFile();

 private:
  void append_unlocked(const char* logline, int len);

  static string getLogFileName(const string& basename, time_t* now);

  const string basename_;
  const off_t rollSize_;
  const int flushInterval_;
  const int checkEveryN_;

  int count_;

  boost::scoped_ptr<MutexLock> mutex_;
  time_t startOfPeriod_;//开始记录日志时间（调整至零点时间？)
  time_t lastRoll_;
  time_t lastFlush_;
  boost::scoped_ptr<FileUtil::AppendFile> file_;

  const static int kRollPerSeconds_ = 60*60*24;//一天的秒数
};

}
#endif  // MUDUO_BASE_LOGFILE_H
