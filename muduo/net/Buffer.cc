// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//

#include <muduo/net/Buffer.h>

#include <muduo/net/SocketsOps.h>

#include <errno.h>
#include <sys/uio.h>

using namespace muduo;
using namespace muduo::net;

const char Buffer::kCRLF[] = "\r\n";

const size_t Buffer::kCheapPrepend;
const size_t Buffer::kInitialSize;

ssize_t Buffer::readFd(int fd, int* savedErrno)
{
  // saved an ioctl()/FIONREAD call to tell how much to read
  char extrabuf[65536];
  struct iovec vec[2];
  const size_t writable = writableBytes();
  vec[0].iov_base = begin()+writerIndex_;
  vec[0].iov_len = writable;
  vec[1].iov_base = extrabuf;
  vec[1].iov_len = sizeof extrabuf;
  // when there is enough space in this buffer, don't read into extrabuf.
  // when extrabuf is used, we read 128k-1 bytes at most.
  const int iovcnt = (writable < sizeof extrabuf) ? 2 : 1;
  //缓冲区够大，通常只需要一次readv调用就能读完全部数据
  const ssize_t n = sockets::readv(fd, vec, iovcnt);
  if (n < 0)//发生错误
  {
    *savedErrno = errno;
  }
  else if (implicit_cast<size_t>(n) <= writable)//长度在可写范围内
  {
    writerIndex_ += n;//写索引累加
  }
  else//超过可写范围
  {
    writerIndex_ = buffer_.size();//先将buffer数据尺寸(1024+8)赋值给写索引
    append(extrabuf, n - writable);//先扩充buff,将剩余的extrabuf里数据拷贝到buff中
  }
  //再读一次？，这种情况在一般的应用场景里不会出现。栈缓冲空间已经达到64k了，一般的应用达不到。
  // if (n == writable + sizeof extrabuf)
  // {
  //   goto line_30;
  // }
  return n;
}

