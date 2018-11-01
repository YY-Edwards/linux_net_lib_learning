#include "codec.h"

#include <muduo/base/Logging.h>
#include <muduo/base/Mutex.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/TcpServer.h>

#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>

#include <set>
#include <stdio.h>
#include <unistd.h>

using namespace muduo;
using namespace muduo::net;
/*

shared_ptr管理共享数据：
<1>.对于write端，如果发现引用计数为1，可以安全的修改共享对象
<2>.对于read端，在度之前把引用计数＋1，读完后-1，这样保证在读期间
其引用计数大于1，可以阻止并发的写。
<3>比较难的是，对于write端，如果发现计数>1，怎么办

*/

class ChatServer : boost::noncopyable
{
 public:
  ChatServer(EventLoop* loop,
             const InetAddress& listenAddr)
  : server_(loop, listenAddr, "ChatServer"),
    codec_(boost::bind(&ChatServer::onStringMessage, this, _1, _2, _3)),
    connections_(new ConnectionList)
  {
    server_.setConnectionCallback(
        boost::bind(&ChatServer::onConnection, this, _1));
    server_.setMessageCallback(
        boost::bind(&LengthHeaderCodec::onMessage, &codec_, _1, _2, _3));
  }

  void setThreadNum(int numThreads)
  {
    server_.setThreadNum(numThreads);
  }

  void start()
  {
    server_.start();
  }

 private:
  void onConnection(const TcpConnectionPtr& conn)
  {
    LOG_INFO << conn->localAddress().toIpPort() << " -> "
        << conn->peerAddress().toIpPort() << " is "
        << (conn->connected() ? "UP" : "DOWN");
	
	 /*此处需要加一下锁，但是仅仅是在写入是加锁，减少了读时锁的使用*/
    MutexLockGuard lock(mutex_);
    if (!connections_.unique())//不唯一时，说明别的线程正在读此指针
    {
	//注意一个迭代器失效问题：
	// 当定义一个vector的迭代器后，如果在这之后发生了插入新的数据，
	// 那么这个迭代器将失效，因为迭代器是通过指针实现的，
	// 内存地址都发生了改变，迭代器当然会失效。
	//“copy on write”	
	// 那么复制，在副本上操作。
	// 首先生成新对象，然后原引用计数减1，若引用计数为0，则析构原对象
	// 最后将新对象的指针交给智能指针
	/*
	如果引用计数大于1，则创建一个副本并在副本上修改，
	shared_ptr通过reset操作后会使引用计数减1，
	原先的数据在read结束后引用计数会减为0，
	进而被系统释放
	*/
      connections_.reset(new ConnectionList(*connections_));
    }
    assert(connections_.unique());

    if (conn->connected())
    {
      connections_->insert(conn);
    }
    else
    {
      connections_->erase(conn);
    }
  }

  typedef std::set<TcpConnectionPtr> ConnectionList;
  typedef boost::shared_ptr<ConnectionList> ConnectionListPtr;

  void onStringMessage(const TcpConnectionPtr&,
                       const string& message,
                       Timestamp)
  {
    ConnectionListPtr connections = getConnectionList();//缩短临界区的做法,读的时候引用计数+1
    for (ConnectionList::iterator it = connections->begin();
        it != connections->end();
        ++it)
    {
      codec_.send(get_pointer(*it), message);
    }
  }

  ConnectionListPtr getConnectionList()
  {
    MutexLockGuard lock(mutex_);
    return connections_;
  }

  TcpServer server_;
  LengthHeaderCodec codec_;
  MutexLock mutex_;
  ConnectionListPtr connections_;
};

int main(int argc, char* argv[])
{
  LOG_INFO << "pid = " << getpid();
  if (argc > 1)
  {
    EventLoop loop;
    uint16_t port = static_cast<uint16_t>(atoi(argv[1]));
    InetAddress serverAddr(port);
    ChatServer server(&loop, serverAddr);
    if (argc > 2)
    {
      server.setThreadNum(atoi(argv[2]));
    }
    server.start();
    loop.loop();
  }
  else
  {
    printf("Usage: %s port [thread_num]\n", argv[0]);
  }
}

