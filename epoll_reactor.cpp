#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/time.h>

#define MAX_TIMERS 1024
#define SERV_PORT 8080
#define MAX_EVENTS 1024
#define MAX_LEN 4096
#define MAX_FDS 10000
#define TIME_MS 6000
typedef void (*callback_t)(int fd, int events, void *arg);

struct myevent_s
{
    int fd;
    int events;
    void *arg;
    callback_t callback;

    int len;
    char buf[MAX_LEN];
    int status;
};

typedef struct
{
    int fd;
    struct myevent_s *ev;
    long expire_time;
} timer_node_t;

typedef struct
{
    timer_node_t heap[MAX_TIMERS];
    int size;
} min_heap_t;

min_heap_t g_timer_heap = {.size = 0};

int heap_index[MAX_FDS];

long get_current_time_ms()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

void init_timer_system()
{
    for (int i = 0; i < MAX_FDS; i++)
    {
        heap_index[i] = -1;
    }
}

void swap_node(int i, int j)
{
    timer_node_t tmp = g_timer_heap.heap[i];
    g_timer_heap.heap[i] = g_timer_heap.heap[j];
    g_timer_heap.heap[j] = tmp;

    heap_index[g_timer_heap.heap[i].fd] = i;
    heap_index[g_timer_heap.heap[j].fd] = j;
}

void sift_up(int index)
{
    while (index > 0)
    {
        int parent = (index - 1) / 2;
        if (g_timer_heap.heap[index].expire_time < g_timer_heap.heap[parent].expire_time)
        {
            swap_node(index, parent);
            index = parent;
        }
        else
        {
            break;
        }
    }
}

void sift_down(int index)
{
    while (index * 2 + 1 < g_timer_heap.size)
    {
        int left = index * 2 + 1;
        int right = index * 2 + 2;

        int min_node = left;

        if (right < g_timer_heap.size)
        {
            if (g_timer_heap.heap[right].expire_time < g_timer_heap.heap[left].expire_time)
            {
                min_node = right;
            }
        }

        if (g_timer_heap.heap[index].expire_time > g_timer_heap.heap[min_node].expire_time)
        {
            swap_node(index, min_node);
            index = min_node;
        }
        else
        {
            break;
        }
    }
}

void add_or_update_timer(int fd, long timeout_ms, struct myevent_s* ev){
    long expire = get_current_time_ms() + timeout_ms;

    int idx = heap_index[fd];
    
    if(idx == -1){
        if (g_timer_heap.size >= MAX_TIMERS){
            return;
        }

        int current_idx = g_timer_heap.size;
        g_timer_heap.heap[current_idx].fd = fd;
        g_timer_heap.heap[current_idx].ev = ev;
        g_timer_heap.heap[current_idx].expire_time = expire;
        g_timer_heap.size++;
        
        heap_index[fd] = current_idx;
        sift_up(current_idx);
    }else{
        g_timer_heap.heap[idx].expire_time = expire;
        sift_down(idx);
        sift_up(idx);
    }
}

int get_min_time(){
    if (g_timer_heap.size == 0){
        return -1;
    }

    long now = get_current_time_ms();

    long diff = g_timer_heap.heap[0].expire_time - now;
    printf("[Timer] 当前最小堆大小: %d, 距离下次超时还有: %ld 毫秒\n", g_timer_heap.size, diff);
    if (diff <= 0){
        return 0;
    }else{
        return (int)diff;
    }
}

void remove_timer(int fd){
    int idx = heap_index[fd];
    if (idx == -1){
        return;
    }

    int size = g_timer_heap.size;
    if (idx == size - 1){
        heap_index[fd] = -1;
        g_timer_heap.size--;
        return;
    }
    
    swap_node(idx, size - 1);
    heap_index[fd] = -1;
    g_timer_heap.size--;
    
    sift_down(idx);
    sift_up(idx);
}

void pop_timer()
{
    if (g_timer_heap.size == 0)
    {
        return;
    }
    remove_timer(g_timer_heap.heap[0].fd);
}

void acceptconn(int fd, int events, void *arg);
void recvdata(int fd, int events, void *arg);
void senddata(int fd, int events, void *arg);

struct myevent_s g_events[MAX_EVENTS + 1];
int g_efd;

int setnonblock(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0)
    {
        printf("fcntl error");
        return -1;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}
void eventset(struct myevent_s *ev, int fd, callback_t callback, void *arg)
{
    ev->fd = fd;
    ev->arg = arg;
    ev->callback = callback;

    ev->status = 0;
    ev->len = 0;
    memset(ev->buf, 0, sizeof(ev->buf));
}
void eventadd(int efd, int events, struct myevent_s *ev)
{
    int op;

    struct epoll_event epv = {0, {0}};
    epv.data.ptr = ev;
    epv.events = ev->events = events;

    if (ev->status != 1)
    {
        op = EPOLL_CTL_ADD;
    }
    else
    {
        op = EPOLL_CTL_MOD;
    }
    ev->status = 1;

    int ret = epoll_ctl(g_efd, op, ev->fd, &epv);
    if (ret < 0)
    {
        printf("[fd = %d] [events = %d] eventadd add/mod failed\n", ev->fd, ev->events);
        perror("eventadd error");
    }
    else
    {
        printf("[fd = %d] [events = %d] eventadd add/mod ok\n", ev->fd, ev->events);
    }
}
void eventdel(int efd, struct myevent_s *ev)
{
    int op;
    struct epoll_event epv = {0, {0}};
    epv.data.ptr = ev;

    op = EPOLL_CTL_DEL;
    ev->status = 0;

    epoll_ctl(efd, op, ev->fd, &epv);
}

void acceptconn(int lfd, int events, void *arg)
{
    int cfd, i;

    struct sockaddr_in cin;
    socklen_t len;

    while (1)
    {
        bzero((struct sockaddr_in *)&cin, sizeof(cin));
        len = sizeof(cin);
        cfd = accept(lfd, (struct sockaddr *)&cin, &len);
        if (cfd > 0)
        {
            do
            {
                for (i = 0; i < MAX_EVENTS; i++)
                {
                    if (g_events[i].status == 0)
                    {
                        break;
                    }
                }

                if (i == MAX_EVENTS)
                {
                    close(cfd);
                    printf("[fd = %d] acceptconn error, for max events\n", cfd);
                    break;
                }

                if (setnonblock(cfd) < 0)
                {
                    close(cfd);
                    printf("[fd = %d] acceptconn error, for unable setnonblock\n", cfd);
                    continue;
                }

                g_events[i].events = EPOLLIN | EPOLLET;
                eventset(&g_events[i], cfd, recvdata, &g_events[i]);
                eventadd(g_efd, EPOLLIN | EPOLLET, &g_events[i]);
                add_or_update_timer(cfd, TIME_MS, &g_events[i]);

                printf("[fd = %d] connected, ip = %s, port = %d\n", cfd, inet_ntoa(cin.sin_addr), ntohs(cin.sin_port));
            } while (0);
        }
        else
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                break;
            }
            if (errno == EINTR)
            {
                continue;
            }
            else
            {
                printf("accept error\n");
            }
        }
    }
}
void recvdata(int cfd, int events, void *arg)
{
    struct myevent_s *ev = (struct myevent_s *)arg;
    int total_len = 0;
    int len;

    eventdel(g_efd, ev);

    while (1)
    {
        len = recv(cfd, ev->buf + total_len, MAX_LEN - total_len - 1, 0);

        if (len < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                break;
            }
            if (errno == EINTR)
            {
                continue;
            }
            perror("recv errno");
            close(cfd);
            remove_timer(cfd);
            return;
        }
        else if (len == 0)
        {
            close(cfd);
            remove_timer(cfd);
            printf("[fd = %d] close successfully\n", cfd);
            return;
        }
        else
        {
            total_len += len;
            if (total_len >= MAX_LEN - 1)
            {
                printf("[fd = %d] recv too much message\n", cfd);
                close(cfd);
                remove_timer(cfd);
                return;
            }
            else
            {
                continue;
            }
        }
    }
    if (total_len == 0)
    {
        ev->len = 0;
        ev->callback = recvdata;
        ev->events = EPOLLIN | EPOLLET;
        eventadd(g_efd, EPOLLIN | EPOLLET, ev);
        return;
    }

    ev->buf[total_len] = '\0';
    printf("recv from [fd = %d] : %s", cfd, ev->buf);

    ev->len = total_len;
    ev->callback = senddata;
    ev->events = EPOLLOUT;
    add_or_update_timer(cfd, TIME_MS, ev);
    eventadd(g_efd, EPOLLOUT, ev);
}
void senddata(int cfd, int events, void *arg)
{
    struct myevent_s *ev = (struct myevent_s *)arg;
    int total_len = 0;
    int len;

    eventdel(g_efd, ev);

    while (1)
    {
        len = send(cfd, ev->buf + total_len, ev->len - total_len, 0);
        if (len < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                break;
            }
            if (errno == EINTR)
            {
                continue;
            }
            perror("send error");
            close(cfd);
            remove_timer(cfd);
            return;
        }
        else if (len == 0)
        {
            close(cfd);
            remove_timer(cfd);
            printf("[fd = %d] has been closed\n", cfd);
            return;
        }
        else
        {
            fwrite(ev->buf + total_len, 1, len, stdout);
            total_len += len;
            if (total_len >= ev->len)
            {
                break;
            }
        }
    }

    if (total_len == ev->len)
    {
        ev->len = 0;
        ev->events = EPOLLIN | EPOLLET;
        ev->callback = recvdata;
        add_or_update_timer(cfd, TIME_MS, ev);
        eventadd(g_efd, EPOLLIN | EPOLLET, ev);
    }
    else
    {
        int remain = ev->len - total_len;
        memmove(ev->buf, ev->buf + total_len, remain);
        ev->len = remain;
        ev->events = EPOLLOUT;
        ev->callback = senddata;
        add_or_update_timer(cfd, TIME_MS, ev);
        eventadd(g_efd, EPOLLOUT, ev);
    }
}

int initlistensock(int efd, struct myevent_s *ev)
{
    int lfd;

    lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0)
    {
        perror("socket error");
        return 0;
    }

    if (setnonblock(lfd) < 0)
    {
        printf("setnonblock error\n");
        return 0;
    }
    int opt = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in sin;
    bzero((struct sockaddr_in *)&sin, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = INADDR_ANY;
    sin.sin_port = htons(SERV_PORT);
    socklen_t len = sizeof(sin);

    if (bind(lfd, (struct sockaddr *)&sin, len) < 0)
    {
        perror("bind error");
        return 0;
    }
    if (listen(lfd, 128) < 0)
    {
        perror("listen error");
        return 0;
    }

    ev->events = EPOLLIN | EPOLLET;
    eventset(ev, lfd, acceptconn, ev);
    eventadd(g_efd, EPOLLIN | EPOLLET, ev);
    return lfd;
}
void handle_expired_timers()
{
    long now = get_current_time_ms();

    while (g_timer_heap.size > 0)
    {
        int fd = g_timer_heap.heap[0].fd;
        struct myevent_s* ev = g_timer_heap.heap[0].ev;

        long expire = g_timer_heap.heap[0].expire_time;

        if (expire > now)
        {
            break;
        }
        printf("[fd=%d] timeout, close connection\n", fd);
        eventdel(g_efd, ev);
        close(fd);
        ev->status = 0;
        pop_timer();
    }
}
int main()
{
    int nfd, lfd;
    struct epoll_event events[MAX_EVENTS + 1];

    init_timer_system();
    g_efd = epoll_create(MAX_EVENTS + 1);
    if (g_efd < 0)
    {
        perror("epoll_create error");
        return -1;
    }

    lfd = initlistensock(g_efd, &g_events[MAX_EVENTS]);

    while (1)
    {
        int timeout = get_min_time();
        if (timeout == -1){
            timeout = 1000;
        }
        nfd = epoll_wait(g_efd, events, MAX_EVENTS + 1, timeout);
        for (int i = 0; i < nfd; i++)
        {
            struct myevent_s *ev = (struct myevent_s *)events[i].data.ptr;
            int revents = events[i].events;

            if (revents & (EPOLLERR | EPOLLHUP | EPOLLRDHUP))
            {
                printf("[fd = %d] peer closed or error,revents = %d\n", ev->fd, revents);
                eventdel(g_efd, ev);
                close(ev->fd);
                remove_timer(ev->fd);
                continue;
            }
            if ((events[i].events & EPOLLIN) && (ev->events & EPOLLIN))
            {
                ev->callback(ev->fd, EPOLLIN | EPOLLET, ev);
            }
            if ((events[i].events & EPOLLOUT) && (ev->events & EPOLLOUT))
            {
                ev->callback(ev->fd, EPOLLOUT | EPOLLET, ev);
            }
        }

        handle_expired_timers();
    }

    close(lfd);
    close(g_efd);
    return 0;
}
