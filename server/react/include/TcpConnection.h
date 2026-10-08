#pragma once
#include "EventLoop.h"
#include "Buffer.h"
#include "Channel.h"
#include "HttpRequest.h"
#include "HttpResponse.h"

//#define MSG_SEND_AUTO

class TcpConnection
{
public:
    TcpConnection(int fd, EventLoop* evloop);
    ~TcpConnection();

    static int processRead(void* arg);
    static int processWrite(void* arg);
    static int destroy(void* arg);
private:
    string m_name;
    //从子线程哪里拿来的reactor
    EventLoop* m_evLoop;
    //回调函数TcpServer::acceptConnection中accept出的新的通信fd
    Channel* m_channel;

    Buffer* m_readBuf;
    Buffer* m_writeBuf;
    // http 协议
    HttpRequest* m_request;
    HttpResponse* m_response;
};