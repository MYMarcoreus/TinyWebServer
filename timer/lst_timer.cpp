#include "lst_timer.h"
#include "../http/http_conn.h"

/*-------------------------------------- class sort_timer_lst ------------------------------------ */

sort_timer_lst::sort_timer_lst()
{
    head = NULL;
    tail = NULL;
}
sort_timer_lst::~sort_timer_lst()
{
    util_timer *tmp = head;
    while (tmp)
    {
        head = tmp->next;
        delete tmp;
        tmp = head;
    }
}

// 插入一个定时器结点到链表中（并保持链表的升序）
void sort_timer_lst::add_timer(util_timer *timer)
{
    if (!timer)
    {
        return;
    }
    if (!head)
    {
        head = tail = timer;
        return;
    }
    if (timer->expire < head->expire)
    {
        timer->next = head;
        head->prev = timer;
        head = timer;
        return;
    }
    __add_timer(timer, head);
}

// 当某个定时器timer的超时时间延长时，在其后寻找插入位置
void sort_timer_lst::adjust_timer(util_timer *timer)
{
    if( timer )
    {
        util_timer* tmp = timer->next;
        if( !tmp || ( timer->expire < tmp->expire ) ) {
            return;
        }

        // 断开timer结点，然后从该结点原后结点开始查找插入位置进行插入
        if( timer == head )
        {
            head = head->next;
            head->prev = NULL;
            timer->next = NULL;
            __add_timer( timer, head );
        }
        else
        {
            timer->prev->next = timer->next;
            timer->next->prev = timer->prev;
            __add_timer( timer, timer->next );
        }
    }
}

// 删除链表中的某个定时器结点
void sort_timer_lst::del_timer(util_timer *timer)
{
    if (timer) {
        if ((timer == head) && (timer == tail))
        {
            delete timer;
            head = NULL;
            tail = NULL;
            return;
        }
        if (timer == head)
        {
            head = head->next;
            head->prev = NULL;
            delete timer;
            return;
        }
        if (timer == tail)
        {
            tail = tail->prev;
            tail->next = NULL;
            delete timer;
            return;
        }
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        delete timer;
    }
}

// 核心函数，每次SIGALRM信号到来，便执行一次该函数
// 该函数执行链表中超时时间已到的任务
void sort_timer_lst::tick()
{
    if (!head)
    {
        return;
    }

    time_t cur = time(NULL);
    util_timer *tmp = head;
    // 从链表头开始执行超时任务，执行完该任务之后便将其结点从链表中删除
    for(util_timer* tmp = head; tmp && tmp->expire <= cur ;tmp = head)
    {
        // 执行超时任务
        tmp->cb_func( tmp->user_data );

        // 执行之后将该结点删除
        head = tmp->next;
        if( head )
            head->prev = NULL;
        delete tmp;
    }
}

void sort_timer_lst::__add_timer(util_timer *timer, util_timer *lst_head)
{
    util_timer *prev = lst_head;
    util_timer *tmp = prev->next;
    while (tmp)
    {
        // 找到了合适的插入位置
        if (timer->expire < tmp->expire)
        {
            prev->next = timer;
            timer->next = tmp;
            tmp->prev = timer;
            timer->prev = prev;
            break;
        }
        prev = tmp;
        tmp = tmp->next;
    }
    if (!tmp)
    {
        prev->next = timer;
        timer->prev = prev;
        timer->next = NULL;
        tail = timer;
    }
}


/*-------------------------------------- class Utils ------------------------------------ */
void Utils::init(int timeslot)
{
    m_TIMESLOT = timeslot;
}

//对文件描述符设置非阻塞
int Utils::setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

//将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
void Utils::addfd(int epollfd, int fd, bool one_shot, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;

    if (1 == TRIGMode)
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else
        event.events = EPOLLIN | EPOLLRDHUP;

    if (one_shot)
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

//信号处理函数
void Utils::sig_handler(int sig)
{
    //为保证函数的可重入性，保留原来的errno
    int save_errno = errno;
    int msg = sig;
    send(u_pipefd[1], (char *)&msg, 1, 0);
    errno = save_errno;
}

//设置信号函数
void Utils::addsig(int sig, void(handler)(int), bool restart)
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if (restart)
        sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

//定时处理任务，重新定时以不断触发SIGALRM信号
void Utils::timer_handler()
{
    m_timer_lst.tick();
    alarm(m_TIMESLOT);
}

void Utils::show_error(int connfd, const char *info)
{
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

int *Utils::u_pipefd = 0;
int Utils::u_epollfd = 0;

// class Utils;
void cb_func(client_data *user_data)
{
    _LOG_INFO("close connection by timer->cb_func");
    epoll_ctl(Utils::u_epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(user_data);
    close(user_data->sockfd);
    http_conn::m_user_count--;
}
