#ifndef MUDUO_EXAMPLES_IDLECONNECTION_ECHO_H
#define MUDUO_EXAMPLES_IDLECONNECTION_ECHO_H

#include <muduo/net/TcpServer.h>
#include <muduo/base/Logging.h>
//#include <muduo/base/Types.h>

#include <boost/circular_buffer.hpp>
#include <boost/unordered_set.hpp>
#include <boost/version.hpp>

#if BOOST_VERSION < 104700
namespace boost
{
template <typename T>
inline size_t hash_value(const boost::shared_ptr<T>& x)
{
  return boost::hash_value(x.get());
}
}
#endif

// RFC 862
class EchoServer
{
 public:
  EchoServer(muduo::net::EventLoop* loop,
             const muduo::net::InetAddress& listenAddr,
             int idleSeconds);

  void start();

 private:
  void onConnection(const muduo::net::TcpConnectionPtr& conn);

  void onMessage(const muduo::net::TcpConnectionPtr& conn,
                 muduo::net::Buffer* buf,
                 muduo::Timestamp time);

  void onTimer();

  void dumpConnectionBuckets() const;

  typedef boost::weak_ptr<muduo::net::TcpConnection> WeakTcpConnectionPtr;

  //特制的结构体
  struct Entry : public muduo::copyable
  {
	  //explicit意为必须显示的调用单参数构造函数，例如：Entry t(tcpcpnnect);传参的方式固定。
	  //普通的构造函数可以隐式调用，但是多参数的也只能显示调用，而且不用添加关键字explicit。
    explicit Entry(const WeakTcpConnectionPtr& weakConn)
      : weakConn_(weakConn)
    {
    }

    ~Entry()//析构的时候将保存的conn弱指针提升为强指针，并操作此对象
    {
      muduo::net::TcpConnectionPtr conn = weakConn_.lock();
      if (conn)
      {
		LOG_TRACE << "shutdown conn"; 
		//先关闭本地写，然后等待对方关闭，接收HUP事件，然后读到0则彻底关闭
        conn->shutdown();
      }
    }

    WeakTcpConnectionPtr weakConn_;
  };
  typedef boost::shared_ptr<Entry> EntryPtr;
  typedef boost::weak_ptr<Entry> WeakEntryPtr;
  //无序，哈希存储的set容器，类型为EntryPtr
  typedef boost::unordered_set<EntryPtr> Bucket;
  //根据用户设置buff大小，即容器set的数量。
  typedef boost::circular_buffer<Bucket> WeakConnectionList;

  muduo::net::TcpServer server_;
  WeakConnectionList connectionBuckets_;
};

#endif  // MUDUO_EXAMPLES_IDLECONNECTION_ECHO_H
