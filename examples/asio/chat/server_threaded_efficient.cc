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
	//注意如果要从多个线程写同一个shared_ptr对象，那么需要加锁。
	//多个线程读是安全的；写则不是。
	 /*此处需要加一下锁，但是仅仅是在写入是加锁，减少了读时锁的使用*/
	 
	 
	//根据COW的准则，当你是唯一拥有者（对应对象的引用计数是1）时，
	//那么你直接修改数据，这样没有问题，当你不是唯一拥有者，
	//则需要拷贝数据再去修改，这就需要用到一些shared_ptr的编程技法了：
    MutexLockGuard lock(mutex_);
    if (!connections_.unique())//不唯一时，说明别的线程正在读此指针
    {
	//注意一个迭代器失效问题：
	// 当定义一个vector的迭代器后，如果在这之后发生了插入新的数据，
	// 那么这个迭代器将失效，因为迭代器是通过指针实现的，
	// 内存地址都发生了改变，迭代器当然会失效。

	//此处的技法类似：
		//ConnectionList newConns(new ConnectionList(*connections_));//拷贝构造,引用计数为1，只是用connections_的内容新构造的
		//交换智能指针:内容，计数都交换。此时newConns计数变为2，connections_计数变为1
		//connections_.swap(newConns);
		
		
		
	/*
	shared_ptr通过reset操作后会使原先connections_引用计数减1，且connections_已经指向了新的对象（即是拷贝构造）
	另外此时原对象有两种操作：
	1.如果原引用计数减1后为0，那么原对象资源释放；
	2.如果原引用计数减1后不为0，原先的数据在read结束离开作用域后引用计数会减为0，进而被系统释放。
	
	因为connections_已经是一个新的对象，且原对象计数减1的同时并未对其进行写操作，
	那么任意修改新对象对原对象均不构成任何影响。
	如果别的reader线程已经刚刚通过getConnectionList()拿到了ConnectionListPtr，
	他会读到稍旧的数据，那么，在下一次读取的时候即会更新的了。
	*/
	
	
		
	//new ConnectionList(*connections_) 这段代码拷贝了一份ConnectionList
    //connections_原来的引用计数减1，而connections_现在的引用计数等于1	
	
      connections_.reset(new ConnectionList(*connections_));
    }
    assert(connections_.unique());

	
	/*在复本上修改,不会影响读者,所以读者在遍历列表的时候,不需要mutex保护*/
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
														//而且可以保证拿到的对象不会在其他地方被写操作
    for (ConnectionList::iterator it = connections->begin();
        it != connections->end();
        ++it)
    {
	//先要调用TcpConnection的send,是当前线程调用则直接发送，
	//若不是则先要使用queueInLoop()注册到其所属线程的用户任务，等待唤醒执行。
      codec_.send(get_pointer(*it), message);
    }
  }//回收connections

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

