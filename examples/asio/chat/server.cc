#include "codec.h"

#include <muduo/base/Logging.h>
#include <muduo/base/Mutex.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/TcpServer.h>

#include <boost/bind.hpp>

#include <set>
#include <stdio.h>
#include <unistd.h>

using namespace muduo;
using namespace muduo::net;

class ChatServer : boost::noncopyable
{
 public:
  ChatServer(EventLoop* loop,
             const InetAddress& listenAddr)
	//构造数据成员是按数据成员在类中声明的顺序进行构造。		 
	//在构造函数体内的所谓初始化，只是给已经生成的对象重新赋值罢了，
	//并没有进行对象的构造。就是说对象构造必须在初始化列表里。
	//这意味着其他类对象的构造先于本类的构造。
	/*
	就是说在没有默认构造函数的时候，如果一个类对象是另一个类的数据成员，
	那么初始化这个数据成员，就应该放到冒号后面。这样可以带参数。
	*/
  : server_(loop, listenAddr, "ChatServer"),
    codec_(boost::bind(&ChatServer::onStringMessage, this, _1, _2, _3))
  {
    server_.setConnectionCallback(
        boost::bind(&ChatServer::onConnection, this, _1));
	//将onMessage注册到相应的Channel;	
	//function可以配合bind使用，并存储bind表达式的结果。
	//另外，bind()是一个函数模板，它的原理是根据已有的模板，生成一个函数，
	//但是由于bind()不知道生成的函数执行的时候，传递进来的参数是否还有效。
	//所以它选择参数值传递而不是引用传递。如果想引用传递，std::ref和std::cref就派上用场了。
	
    //还有，bind采用的是拷贝的方式存储，这意味着如果函数对象或值参数很大、拷贝代价会很高，
	//或者无法拷贝，那么使用就会受限制。因此搭配ref包装对象的引用，从而就变相的变成存储
	//对象引用的拷贝，从而降低拷贝的代价。然后这也有隐患，会导致调用时可能延时。那么调用的时候
	//对象不存在或者销毁了，那么就会发生未定义行为。
	server_.setMessageCallback(
        boost::bind(&LengthHeaderCodec::onMessage, &codec_, _1, _2, _3));
  }

  void start()
  {
    server_.start();//调用TcpServer的start()
  }

 private:
  void onConnection(const TcpConnectionPtr& conn)
  {
    LOG_INFO << conn->localAddress().toIpPort() << " -> "
             << conn->peerAddress().toIpPort() << " is "
             << (conn->connected() ? "UP" : "DOWN");

    if (conn->connected())
    {
      connections_.insert(conn);
    }
    else
    {
      connections_.erase(conn);
    }
  }

  void onStringMessage(const TcpConnectionPtr&,
                       const string& message,
                       Timestamp)
  {
    for (ConnectionList::iterator it = connections_.begin();
        it != connections_.end();
        ++it)
    {
      codec_.send(get_pointer(*it), message);
    }
  }

  typedef std::set<TcpConnectionPtr> ConnectionList;
  TcpServer server_;
  LengthHeaderCodec codec_;
  ConnectionList connections_;
};

int main(int argc, char* argv[])
{
  muduo::Logger::setLogLevel(Logger::TRACE);
  LOG_INFO << "pid = " << getpid();
  if (argc > 1)
  {
    EventLoop loop;
    uint16_t port = static_cast<uint16_t>(atoi(argv[1]));
    InetAddress serverAddr(port);
    ChatServer server(&loop, serverAddr);
    server.start();
    loop.loop();
  }
  else
  {
    printf("Usage: %s port\n", argv[0]);
  }
}

