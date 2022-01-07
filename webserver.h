#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>

#include "./threadpool/threadpool.h"
#include "./http/http_conn.h"

const int MAX_FD = 65536;           //最大文件描述符
const int MAX_EVENT_NUMBER = 10000; //最大事件数
const int TIMESLOT = 5;             //最小超时单位

class WebServer
{
public:
    WebServer();
    ~WebServer();

    void init(int port , string user, string passWord, string databaseName,
              int log_write , int opt_linger, int trigmode, int sql_num,
              int thread_num, int close_log, int actor_model);

    void thread_pool();
    void sql_pool();
    void log_write();
    void trig_mode();

    void eventListen();
    void eventLoop();

    void run();

private:
// 供deal函数调用的操作timer的私有函数
    // 调整定时器的相关函数
    void add_timer(int connfd, struct sockaddr_in client_address);
    void adjust_timer(util_timer *timer);
    void del_timer(util_timer *timer, int sockfd);
//
    bool deal_newclient();
    bool dealwith_signal(bool& timeout, bool& stop_server);
    void dealwith_read(int sockfd);
    void dealwith_write(int sockfd);

public:
    //基础
    int   m_port;
    char* m_root;
    int   m_log_write;
    int   m_close_log;
    int   m_actormodel;

    int        m_pipefd[2];
    int        m_epollfd;
    http_conn* users;       // 存放http_conn对象的数组，内部对象会被append进线程池m_pool中

    //数据库相关
    sql_connection_pool* m_sqlConnPool;
    string               m_user;         //登陆数据库用户名
    string               m_passWord;     //登陆数据库密码
    string               m_databaseName; //使用数据库名
    int                  m_sql_num;

    //线程池相关
    threadpool<http_conn>* m_threadPool;
    int                    m_thread_num;

    //epoll_event相关
    epoll_event events[MAX_EVENT_NUMBER];

    int m_listenfd;
    int m_OPT_LINGER;
    int m_TRIGMode;
    int m_LISTENTrigmode;
    int m_CONNTrigmode;

    //定时器相关(定时器用来处理非活动链接)
    client_data* users_timer;
    Utils        utils; // 这应放入一个命名空间
};
#endif
