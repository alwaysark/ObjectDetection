#pragma once
#include "EventLoop.h"
#include "ThreadPool.h"

class TcpServer
{
public:
    TcpServer(unsigned short port, int threadNum);
    // 初始化监听
    void setListen();
    // 启动服务器
    void run();
    static int acceptConnection(void* arg);

private:
    //子线程个数
    int m_threadNum;
    //主线程反应堆实例
    EventLoop* m_mainLoop;
    //线程池
    ThreadPool* m_threadPool;
    //主线程监听文件描述符
    int m_lfd;
    //端口号
    unsigned short m_port;
};

