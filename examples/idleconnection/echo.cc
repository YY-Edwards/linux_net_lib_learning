#include "echo.h"


#include <muduo/net/EventLoop.h>

#include <boost/bind.hpp>

#include <assert.h>
#include <stdio.h>

using namespace muduo;
using namespace muduo::net;


EchoServer::EchoServer(EventLoop* loop,
                       const InetAddress& listenAddr,
                       int idleSeconds)
  : server_(loop, listenAddr, "EchoServer"),
    connectionBuckets_(idleSeconds)
{
  server_.setConnectionCallback(
      boost::bind(&EchoServer::onConnection, this, _1));
  server_.setMessageCallback(
      boost::bind(&EchoServer::onMessage, this, _1, _2, _3));
	  //注册定时（1s）回调
  loop->runEvery(1.0, boost::bind(&EchoServer::onTimer, this));
  //为什么这里还要重置循环buff的大小
  connectionBuckets_.resize(idleSeconds);
  dumpConnectionBuckets();
}

void EchoServer::start()
{
  server_.start();
}

void EchoServer::onConnection(const TcpConnectionPtr& conn)
{
  LOG_INFO << "EchoServer - " << conn->peerAddress().toIpPort() << " -> "
           << conn->localAddress().toIpPort() << " is "
           << (conn->connected() ? "UP" : "DOWN");

  if (conn->connected())
  {
	  {
		EntryPtr entry(new Entry(conn));//构造一个新的Entry对象指针
		//hash set 会自动去重
		connectionBuckets_.back().insert(entry);//在队尾的hash-set里插入对象
		dumpConnectionBuckets();//entry此时还并未回收，因此计数递增1
		WeakEntryPtr weakEntry(entry);//弱引用
		conn->setContext(weakEntry);//将弱引用保存到context,更新数据的需要用？
	  }
		
  }
  else
  {
    assert(!conn->getContext().empty());
    WeakEntryPtr weakEntry(boost::any_cast<WeakEntryPtr>(conn->getContext()));
    LOG_DEBUG << "Entry use_count = " << weakEntry.use_count();
  }
}

void EchoServer::onMessage(const TcpConnectionPtr& conn,
                           Buffer* buf,
                           Timestamp time)
{
  string msg(buf->retrieveAllAsString());
  LOG_INFO << conn->name() << " echo " << msg.size()
           << " bytes at " << time.toString();
  conn->send(msg);

  assert(!conn->getContext().empty());
  //any类本身不提供对内部元素的访问接口，而是使用一个友元函数any_cast()，
  //类似转型操作符，可以取出any内部持有的对象。但是需要提前知道内部值的确切类型。
  WeakEntryPtr weakEntry(boost::any_cast<WeakEntryPtr>(conn->getContext()));
  EntryPtr entry(weakEntry.lock());
  if (entry)
  {
	  //hash set 会自动去重
	  //注意：为了简单，在更新时，不会把一个已连接从一个格子移动到另一个，
	  //而是采用引用计数，用shared_ptr来管理。
	  //从连接收到数据，就把对应的EntryPtr放到格子里，这样引用计数就增加了，
	  //当计数到0时，当超时是，则会自动析构并断开。
    connectionBuckets_.back().insert(entry);
    dumpConnectionBuckets();
  }
}

void EchoServer::onTimer()
{
//往队尾添加一个空的 Bucket，
//这样 circular_buffer 会自动弹出队首的 Bucket，并析构之.
//在析构的时候，会在容器里依次析构其中的EntryPtr对象
//buff满的情况下，这样才会弹出队首吧。待验证
//每一个Bucket即是一个set容器，容器里的数量不定。
//那么新放入的连接总是在队尾的set容器里。
  connectionBuckets_.push_back(Bucket());
  dumpConnectionBuckets();
}

void EchoServer::dumpConnectionBuckets() const
{
  LOG_INFO << "size = " << connectionBuckets_.size();
  int idx = 0;
  for (WeakConnectionList::const_iterator bucketI = connectionBuckets_.begin();
      bucketI != connectionBuckets_.end();
      ++bucketI, ++idx)
  {
    const Bucket& bucket = *bucketI;
    printf("[%d] len = %zd : ", idx, bucket.size());
    for (Bucket::const_iterator it = bucket.begin();
        it != bucket.end();
        ++it)
    {
      bool connectionDead = (*it)->weakConn_.expired();
      printf("%p(%ld)%s, ", get_pointer(*it), it->use_count(),
          connectionDead ? " DEAD" : "");
    }
    puts("");
  }
}

