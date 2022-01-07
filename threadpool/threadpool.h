#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"


// 测试2
template <typename T>
class threadpool
{
public:
    enum IOState {
        READ = 0,
        WRITE
    };


    /*thread_number是线程池中线程的数量，
    max_requests是请求队列中最多允许的、等待处理的请求的数量*/
    threadpool(int actor_model, sql_connection_pool* connPool,
               int thread_number = 8, int max_request = 10000);
    ~threadpool();
    bool append(T *request, IOState state);     // Reactor模式的append
    bool append_p(T *request);                  // Proactor模式的append

private:
    /*工作线程运行的函数，它不断从工作队列中取出任务并执行之*/
    static void* worker(void *arg);
    void run();

private:
    int                  m_actor_model;   // 模型切换（reactor/proactor）
    sql_connection_pool* m_sqlConnPool;      // 数据库

    // 线程池相关
    pthread_t*           m_threads;       // 描述线程池的数组，其大小为m_thread_number
    int                  m_thread_number; // 线程池中的线程数

    // 工作队列相关
    std::list<T*>        m_workqueue;     // 工作队列（存放待处理的客户连接）
    int                  m_max_requests;  // 工作队列中允许的最大连接数
    locker               m_queuelocker;   // 保护请求队列的互斥锁
    sem                  m_queuestat;     // 消费者等待生产者的
};

template <typename T>
threadpool<T>::threadpool( int actor_model, sql_connection_pool *connPool, int thread_number, int max_requests)
    : m_actor_model(actor_model),m_thread_number(thread_number),
    m_max_requests(max_requests), m_sqlConnPool(connPool), m_threads(NULL)
{
    if (thread_number <= 0 || max_requests <= 0)
        throw std::exception();

    m_threads = new pthread_t[m_thread_number];
    if (!m_threads)
        throw std::exception();

    //! 创建thread_number个线程，并将它们都设置为脱离线程
    for (int i = 0; i < thread_number; ++i)
    {
        if (pthread_create(m_threads + i, NULL, worker, this) != 0) {
            delete[] m_threads;
            throw std::exception();
        }
        if (pthread_detach(m_threads[i])) {
            delete[] m_threads;
            throw std::exception();
        }
    }
}

template <typename T>
threadpool<T>::~threadpool()
{
    delete[] m_threads;
}

template <typename T>
bool threadpool<T>::append(T *request, IOState state)
{
    //! 操作工作队列需加锁，因为它被所有线程共享
    m_queuelocker.lock();
        // 将新工作加入工作队列
        if (m_workqueue.size() >= m_max_requests)
        {
            m_queuelocker.unlock();
            return false;
        }
        request->m_state = state;
        m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post(); // 有新的工作加入工作队列，通知消费者
    return true;
}

template <typename T>
bool threadpool<T>::append_p(T *request)
{
    m_queuelocker.lock();
        // 将新工作加入工作队列
        if (m_workqueue.size() >= m_max_requests)
        {
            m_queuelocker.unlock();
            return false;
        }
        m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

//! C++中使用pthread_create函数时，第三个参数必须是一个static函数
//! 而在static函数调用non-static函数有两个办法：
//!     1、在单例模式中，使用类成员中的实例成员来访问non-static成员函数
//!     2、将当前对象this传递给static函数，然后在static函数中使用这个this指针来访问non-static成员函数
//!     而这里是使用第二种，使用pthread_create函数时，传递static函数worker，其参数为this指针，worker内部调用non-static函数run
template <typename T>
void *threadpool<T>::worker(void *arg)
{
    threadpool *pool = (threadpool *)arg;
    pool->run();
    return pool;
}

// 被worker调用的run函数
template <typename T>
void threadpool<T>::run()
{
    while (true)
    {
        // 工作队列为空时，线程等待队列加入新的连接
        m_queuestat.wait();   // 等待生产者(append函数)将待处理的连接加入工作队列

        // 线程获取互斥锁，获取队首的待处理的连接，处理该连接的工作
        m_queuelocker.lock(); // 工作队列被多个线程共享，故加锁
            if (m_workqueue.empty()) {
                m_queuelocker.unlock();
                continue;
            }
        	// 线程取出工作队列队首的待处理连接
            T *request = m_workqueue.front();
            m_workqueue.pop_front();
        m_queuelocker.unlock();
        if (!request)
            continue;

        // Reactor
        if (1 == m_actor_model)
        {
            // 读
            if (0 == request->m_state)
            {
                if (request->read_once())
                {
                    request->improv = 1;
                    connectionRAII mysqlcon(&request->mysql, m_sqlConnPool);
                    request->process();
                }
                else
                {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
            // 写
            else
            {
                if (request->write())
                {
                    request->improv = 1;
                }
                else
                {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
        }
        // Proactor
        else
        {
            connectionRAII mysqlcon(&request->mysql, m_sqlConnPool);
            request->process();
        }
    }
}
#endif
