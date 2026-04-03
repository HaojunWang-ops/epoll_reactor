# epoll Reactor Server

一个基于 `epoll` 的单线程 Reactor 网络服务器示例，使用非阻塞 socket、事件回调和最小堆定时器实现连接管理与空闲连接超时清理。

## 项目简介

这个项目实现了一个最小可运行的 `epoll` 反应堆模型，主要包含以下能力：

- 使用 `epoll` 统一管理监听 socket 和客户端连接
- 使用非阻塞 socket 避免单线程事件循环被阻塞
- 使用事件对象 `myevent_s` 管理 fd、回调函数和缓冲区
- 使用 `acceptconn / recvdata / senddata` 完成连接接入、读取和发送
- 使用最小堆维护连接超时时间
- 在主循环中结合 `epoll_wait` 的超时参数和最小堆完成空闲连接回收

这个项目适合用来学习：

- `epoll` 反应堆模型
- 非阻塞 IO
- 事件驱动编程
- 最小堆定时器
- 空闲连接超时管理

---

## 整体架构

### 1. Reactor 事件对象

每个连接使用一个 `myevent_s` 结构体表示，结构体中维护：

- `fd`：文件描述符
- `events`：当前关注的事件类型
- `arg`：回调参数
- `callback`：事件回调函数
- `buf` / `len`：读写缓冲区与数据长度
- `status`：是否已挂入 epoll

监听 fd 和客户端连接 fd 都使用统一的事件对象模型管理。

---

### 2. 事件管理函数

项目中用以下函数管理 epoll 事件：

- `eventset()`：初始化事件对象
- `eventadd()`：将事件加入 epoll，或者修改已存在事件
- `eventdel()`：从 epoll 中删除事件

---

### 3. 核心回调函数

#### `acceptconn`
负责处理监听 socket 的可读事件：

- 接收新连接
- 将新连接设置为非阻塞
- 从事件池中分配一个空闲事件对象
- 将新连接注册为 `EPOLLIN`
- 为新连接加入定时器

#### `recvdata`
负责处理连接的读事件：

- 非阻塞循环读取数据
- 处理 `EAGAIN / EWOULDBLOCK / EINTR`
- 读到数据后切换到写事件
- 刷新连接超时时间

#### `senddata`
负责处理连接的写事件：

- 非阻塞循环发送数据
- 处理部分发送
- 发送完成后切回读事件
- 刷新连接超时时间

---

### 4. 最小堆定时器

项目中使用最小堆维护连接的过期时间。

#### 定时器节点
每个节点包含：

- `fd`
- `ev`
- `expire_time`

#### 最小堆功能
实现了以下操作：

- `add_or_update_timer()`：新增或更新连接定时器
- `get_min_time()`：获取最近一个超时连接距离当前还剩多少毫秒
- `pop_timer()`：弹出堆顶
- `remove_timer()`：删除任意连接的定时器
- `sift_up()` / `sift_down()`：维护堆有序性

---

## 工作流程

### 服务启动流程

1. 创建 `epoll` 实例
2. 初始化监听 socket
3. 将监听 fd 加入 epoll
4. 进入事件循环

### 新连接到来

1. `listenfd` 可读
2. 触发 `acceptconn`
3. `accept` 新连接
4. 设置为非阻塞
5. 加入 epoll，监听 `EPOLLIN`
6. 加入最小堆定时器

### 数据读写流程

1. 客户端连接可读
2. 触发 `recvdata`
3. 读取数据到缓冲区
4. 切换到 `EPOLLOUT`
5. 客户端连接可写
6. 触发 `senddata`
7. 发送完成后切回 `EPOLLIN`

### 超时清理流程

1. 主循环调用 `get_min_time()` 计算 `epoll_wait` 的 timeout
2. `epoll_wait` 返回后调用 `handle_expired_timers()`
3. 从堆顶开始检查是否超时
4. 如果超时则：
   - 从 epoll 删除事件
   - 关闭 fd
   - 弹出最小堆节点

---

## 代码结构说明

```text
.
├── main
│   ├── initlistensock()          # 初始化监听 socket
│   ├── epoll_wait()              # 事件循环
│   └── handle_expired_timers()   # 处理超时连接
│
├── 事件对象与事件管理
│   ├── struct myevent_s
│   ├── eventset()
│   ├── eventadd()
│   └── eventdel()
│
├── 回调函数
│   ├── acceptconn()
│   ├── recvdata()
│   └── senddata()
│
└── 定时器最小堆
    ├── struct timer_node_t
    ├── struct min_heap_t
    ├── swap_node()
    ├── sift_up()
    ├── sift_down()
    ├── add_or_update_timer()
    ├── get_min_time()
    ├── pop_timer()
    └── remove_timer()
```

## 编译方式

```bash
g++ -o reactor epoll_reactor.cpp
```
## 运行方式
启动服务端

```bash
./reactor
```
默认监听端口
```text
8080
```
你可以使用 *nc* 或 *telnet* 连接
或
```bash
talnet 127.0.0.1 8080
```

## 关键宏定义
```c
#define MAX_TIMERS 1024
#define SERV_PORT 8080
#define MAX_EVENTS 1024
#define MAX_LEN 4096
#define MAX_FDS 10000
#define TIME_MS 6000
```

说明
- MAX_TIMERS 最小堆支持的最大定时器数量
- SERV_PORT 服务端监听端口号
- MAX_EVENTS epoll和事件池容量
- MAX_LEN 单词缓冲区的最大长度
- MAX_FDS 用于heap_index的fd映射上限
- TIME_MS 连接超时时间（毫秒）

## 当前已实现特点
### 已实现
- 单线程Reactor模型
- 非阻塞accept/ recv/ send
- epoll读写事件分发
- 最小堆管理连接超时
- 空闲连接自动关闭
### 适合作为学习项目的点
- 结构清晰，方便理解Reactor模型
- 读写事件切换逻辑直观
- 可以用来继续扩展HTTP/RPC/libevent风格实现
###后续可优化方向
- 将定时器与连接对象进一步解耦，减少辅助映射的复杂度
- 增加日志级别与调试输出
- 进一步封装成更标准的Reactor框架

## 学习路线建议
如果你是按照网络编程路线推进， 这个项目可以作为下面这些内容的过渡板
```text
socket 基础
-> epoll
-> 单线程 Reactor
-> 最小堆定时器
-> libevent
-> HTTP 协议
-> RPC 框架
```