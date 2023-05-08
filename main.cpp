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

#include "./lock/locker.h"
#include "./threadpool/threadpool.h"
#include "./timer/lst_timer.h"
#include "./http/http_conn.h"
#include "./log/log.h"
#include "./CGImysql/sql_connection_pool.h"

#define MAX_FD 65536  //最大文件描述符
#define MAX_EVENT_NUMBER 10000 //最大事件数
#define TIMESLOT 5 //最小超时时间

#define SYNLOG //同步写日志
//#define ASYNLOG //异步写日志

//#define listenfdET //边缘触发非阻塞
#define listenfdLT //水平触发阻塞

//这三个函数在http_conn.cpp中定义，改变链路属性
extern int addfd(int epollfd, int fd, bool one_shot);
extern int remove(int epollfd, int fd);
extern int setnonblocking(int fd);

//设置定时器相关参数
static int pipefd[2];
static sort_timer_lst timer_lst;
static int epollfd = 0;

//信号处理函数
void sig_handler(int sig)
{
//为保证函数的可重入性，保留原来的errno
	int save_errno = errno; 
	int msg = sig; //信号
	send(pipefd[1], (char *)&msg, 1, 0); //发送消息到pipefd[1]，函数原型为ssize_t send(int sockfd, const void *buf, size_t len, int flags);sockfd：接收消息的套接字的文件描述符，buf：要发送的消息，len：要发送的字节数.flags：表示下列标志中的0个或多个，错误返回-1，否则返回发送的字节数
	errno = save_errno; //获取原来的errno
}

//设置信号函数
void addsig(int sig, void(handler)(int), bool restart = true)
{
	struct sigaction sa; //构建信号结构体
	memset(&sa, '\0', sizeof(sa)); //分配内存
	sa.sa_handler = handler; //获取信号的句柄
	if(restart) //如果restart为真
		sa.sa_flags |= SA_RESTART; //指定信号处理的行为包括使被信号打断的系统调用自动重新发起
	sigfillset(&sa.sa_mask); //初始化一个满的信号集，集合当中有所有的信号，所有的信号都被添加到这个集合中，原始函数为int sigfillset (sigset_t *set)，其中set为信号集标识的地址，以后操作此信号集，成功返回 0，失败返回-1。
	assert(sigaction(sig, &sa, NULL) != -1);//检查或修改与指定信号相关联的处理动作，如果返回错误，则终止程序执行。其中sigaction函数原型为int sigaction(int signum, const struct sigaction *act,struct sigaction *oldact);signum参数指出要捕获的信号类型，act参数指定新的信号处理方式，oldact参数输出先前信号的处理方式。assert函数原型为void assert( int expression )，先计算表达式 expression ，如果其值为假（即为0），那么它先向stderr打印一条出错信息，然后通过调用 abort 来终止程序运行
}

//定时处理任务，重新定时以不断触发SIGALRM信号
void timer_handler()
{
	timer_lst.tick(); //调用tick函数。进行定时处理任务
	alarm(TIMESLOT); //设置定时器，函数原型为unsigned int alarm(unsigned int seconds);在指定seconds后，内核会给当前进程发送14）SIGALRM信号。进程收到该信号，默认动作终止。每个进程都有且只有唯一的一个定时器，返回0或剩余的秒数，无失败
}

//定时器回调函数，删除非活动连接在socket上的注册事件，并关闭
void cb_func(client_data *user_data)
{
	epoll_ctl(epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0); //从epollfd中删除一个user_data->sockfd，函数原型为int epoll_ctl（int epfd，int op，int fd，struct epoll_event * event）; 该系统调用对文件描述符epfd引用的epoll实例执行控制操作。它要求操作op对目标文件描述符fd执行
	assert(user_data); //判断是否失败，失败则终止程序
	close(user_data->sockfd); //关闭user_data->sockfd
	http_conn::m_user_count --; //用户连接数减一
	LOG_INFO("close fd %d", user_data->sockfd); //输入到日志
	Log::get_instance()->flush(); //刷新缓冲区
}

//输出错误
void show_error(int connfd, const char *info)
{
	printf("%s", info); //输出信息
	send(connfd, info, strlen(info), 0); //发送消息到connfd
	close(connfd); //关闭connfd
}

int main(int argc, char *argv[])
{
#ifdef ASYNLOG
	Log::get_instance()->init("ServerLog", 2000, 800000, 8); //异步日志模型
#endif

#ifdef SYNLOG
	Log::get_instance()->init("ServerLog", 2000, 800000, 0); //同步日志模型
#endif

	if(argc <= 1) //如果输入参数<=1
	{
		printf("usage: %s ip_address port_number\n", basename(argv[0])); //输出信息
		return 1; //返回
	}

	int port = atoi(argv[1]); //获取端口号

	addsig(SIGPIPE, SIG_IGN); //调用addsig设置信号函数

//创建数据库连接池
	connection_pool *connPool = connection_pool::GetInstance(); //创建连接池
	connPool->init("localhost", "root", "W1369y5z944!", "tiny", 3306, 8); //初始化连接池

//创建线程池
	threadpool<http_conn> *pool = NULL; 
	try
	{
		pool = new threadpool<http_conn>(connPool); //创建一个线程池
	}
	catch (...)
	{
		return 1; //失败返回
	}

	http_conn *users = new http_conn[MAX_FD]; //构建http_conn数组
	assert(users); //判断是否失败，失败则终止程序

//初始化数据库读取表
	users->initmysql_result(connPool);

	int listenfd = socket(PF_INET, SOCK_STREAM, 0); //创建套接字。函数原型为int socket(int af, int type, int protocol);1) af 为地址族（Address Family），也就是 IP 地址类型，常用的有 AF_INET 和 AF_INET6。 type 为数据传输方式/套接字类型，常用的有 SOCK_STREAM（流格式套接字/面向连接的套接字） 和 SOCK_DGRAM（数据报套接字/无连接的套接字），protocol 表示传输协议，常用的有 IPPROTO_TCP 和 IPPTOTO_UDP，分别表示 TCP 传输协议和 UDP 传输协议。
	assert(listenfd >= 0); //判断是否失败，失败则终止程序

	//struct linger tmp = {1, 0};
	//SO_LINGER若有数据待发送，延迟关闭
	//setsockopt(listenfd, SOL_SOCKET, SO_LINGER, &tmp, sieof(tmp));//设置套接字描述符属性。函数原型为int setsockopt( int sockfd, int level, int optname,const void *optval, size_t optlen);sockfd：要设置的套接字描述符。level：选项定义的层次。或为特定协议的代码（如IPv4，IPv6，TCP，SCTP），或为通用套接字代码（SOL_SOCKET）。optname：选项名。level对应的选项，一个level对应多个选项，不同选项对应不同功能。optval：指向某个变量的指针，该变量是要设置新值的缓冲区。可以是一个结构体，也可以是普通变量optlen：optval的长度。
	

	int ret = 0;
	struct sockaddr_in address; //构建一个网络地址结构体
	bzero(&address, sizeof(address)); //将结构体内容清零
	address.sin_family = AF_INET; //AF_INET地址族
	address.sin_addr.s_addr = htonl(INADDR_ANY); //ip地址设为0.0.0.0（本机）
	address.sin_port = htons(port); //赋予端口号

	int flag = 1;
	setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag)); //设置套接字描述符属性，打开地址复用功能
	ret = bind(listenfd, (struct sockaddr *)&address, sizeof(address)); //对套接字进行地址和端口的绑定，函数原型为：int bind(int sockfd, const struct sockaddr *my_addr, socklen_t addrlen);sockfd是用socket()函数创建的文件描述符，my_addr是指向一个结构为sockaddr参数的指针，sockaddr中包含了地址、端口和IP地址的信息。在进行地址绑定的时候，需要弦将地址结构中的IP地址、端口、类型等结构struct sockaddr中的域进行设置之后才能进行绑定，这样进行绑定后才能将套接字文件描述符与地址等接合在一起。addrlen是my_addr结构的长度，可以设置成sizeof(struct sockaddr)。bind()函数的返回值为0时表示绑定成功，-1表示绑定失败
	assert(ret >= 0); //判断是否失败，失败则终止程序
	ret = listen(listenfd, 5);//将套接字文件描述符从主动转为被动文件描述符，然后用于被动监听客户端的连接，函数原型为int listen(int sockfd, int backlog);sockfd 表示socket创建的套接字文件描述符，backlog 指定队列的容量，成功返回0，失败返回-1， errno被设置
	assert(ret >= 0); //判断是否失败，失败则终止程序

//创建内核事件表
	epoll_event events[MAX_EVENT_NUMBER]; //创建epoll_event结构体数组
	epollfd = epoll_create(5); //创建一个epoll的句柄，大小为5.函数原型为int epoll_create(int size)，生成一个epoll专用的文件描述符，其中的参数是指定生成描述符的最大范围；
	assert(epollfd != -1); //判断是否失败，失败则终止程序

	addfd(epollfd, listenfd, false); //调用addfd函数（定义于http_conn.cpp中）
	http_conn::m_epollfd = epollfd; //将epollfd赋值给http_conn::m_epollfd

//创建管道
	ret = socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd); //创建一对套节字进行进程间通信，函数原型为int socketpair(int domain, int type, int protocol, int sv[2]);domain表示协议族，type表示协议，protocol表示类型，protocol表示类型
	assert(ret != 1); //判断是否失败，失败则终止程序
	setnonblocking(pipefd[1]); //设置套接字为非阻塞套接字
	addfd(epollfd, pipefd[0], false); //调用addfd函数

	addsig(SIGALRM, sig_handler, false); //调用addsig设置信号函数
	addsig(SIGTERM, sig_handler, false); //调用addsig设置信号函数
	bool stop_server = false; //构建一个布尔值存放服务器是否停止

	client_data *users_timer = new client_data[MAX_FD]; //构建client_data数组

	bool timeout = false;
	alarm(TIMESLOT); //设置信号传送闹钟，函数原型为unsigned int alarm(unsigned int seconds);用来设置信号SIGALRM在经过参数seconds秒数后发送给目前的进程。如果未设置信号SIGALARM的处理函数，那么alarm()默认处理终止进程

	while(!stop_server) //只要stop_server不为真，就一直循环
	{
		int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1); //系统调用等待文件描述符epfd引用的epoll实例上的事件，函数原型为int epoll_wait（int epfd，struct epoll_event * events， int maxevents，int timeout）;epfd：由epoll_create生成的epoll专用的文件描述符；events：用于回传代处理事件的数组；maxevents：每次能处理的事件数；timeout：等待I/O事件发生的超时值，  成功：返回发生的事件数；失败：-1
		if(number < 0 && errno != EINTR) //如果失败了，并且errno为ENTER
		{
			LOG_ERROR("%s", "epoll failure"); //输入到日志
			break; //跳出循环，打断
		}

		for(int i = 0; i < number; i++)
		{
			int sockfd = events[i].data.fd; //获取套接字

      //处理新到的客户连接
			if(sockfd == listenfd)
			{
				struct sockaddr_in client_address; //构建一个sockaddr_in结构体
				socklen_t client_addrlength = sizeof(client_address); //获取socklen_t长度
#ifdef listenfdLT
				int connfd = accept(listenfd, (struct sockaddr *)&client_address, &client_addrlength); //在套接字上进行传入连接尝试，函数原型为int accept(int sockfd, void *addr, int *addrlen); sockfd是由socket函数返回的套接字描述符，参数addr和addrlen用来返回已连接的对端进程（客户端）的协议地址，返回若成功则为非负描述符，若出错则为-1
				if(connfd < 0) //如果出错
				{
					LOG_ERROR("%s:errno is:%d", "accept error", errno); //输入到日志
					continue; //跳出本次循环，继续
				}
				if(http_conn::m_user_count >= MAX_FD) //如果用户数量超出限制
				{
					show_error(connfd, "Internal server busy"); //调用show_error函数
					LOG_ERROR("%s", "Internal server busy"); //输入到日志
					continue; //跳出本次循环，继续
				}
				users[connfd].init(connfd, client_address); //初始化user[connfd]

        //初始化client_data数据
        //创建定时器，设置回调函数和超时时间，绑定用户数据，将定时器添加到链表中
				users_timer[connfd].address = client_address; //设置地址
				users_timer[connfd].sockfd = connfd; //设置套接字
				util_timer *timer = new util_timer; //构造一个双向链表的形式的定时器类
				timer->user_data = &users_timer[connfd]; //绑定客户端数据
				timer->cb_func = cb_func; //设置回调函数
				time_t cur = time(NULL); //获取当前时间
				timer->expire = cur + 3 * TIMESLOT; //设置超时时间
				users_timer[connfd].timer = timer; //设置双向链表的形式的定时器类
				timer_lst.add_timer(timer); //添加进基于升序链表的定时器
#endif

#ifdef listenfdET
				while(1)
				{
					int connfd = accept(listenfd, (struct sockaddr *)&client_address, &client_addrlength);//在套接字上进行传入连接尝试
					if(connfd < 0)//如果出错
					{
						LOG_ERROR("%s:errno is:%d", "accept error", errno); //输入到日志
						break; //跳出循环，打断
					}
					if(http_conn::m_user_count>= MAX_FD)//如果用户数量超出限制
					{
						show_error(connfd, "Internal server busy"); //调用show_error函数
						LOG_ERROR("%s", "Internal server busy");  //输入到日志
						break; //跳出循环，打断
					}
					users[connfd].init(connfd, client_address);  //初始化user[connfd]

          //初始化client_data数据
          //创建定时器，设置回调函数和超时时间，绑定用户数据，将定时器添加到链表中
					users_timer[connfd].address = client_address; //设置地址
					users_timer[connfd].sockfd = connfd;  //设置套接字
					util_timer *timer = new util_timer;   //构造一个双向链表的形式的定时器类
					timer->user_data = &users_timer[connfd];  //绑定客户端数据
					timer->cb_func = cb_func;   //设置回调函数
					time_t cur = time(NULL);  //获取当前时间
					timer->expire = cur + 3 * TIMESLOT; //设置超时时间
					users_timer[connfd].timer = timer; //设置双向链表的形式的定时器类
					timer_lst.add_timer(timer);  //添加进基于升序链表的定时器
				}
				continue;  //跳出本次循环，继续
#endif
			}

			else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) //如果事件是EPOLLRDHUP | EPOLLHUP | EPOLLERR形式
			{
      //服务器端关闭连接，移除对应的定时器
				util_timer *timer = users_timer[sockfd].timer; //获取对应的定时器
				timer->cb_func(&users_timer[sockfd]); //调用回调函数

				if(timer) //如果定时器存在
				{
					timer_lst.del_timer(timer); //调用del_timer删除定时器
				}
			}

      //处理信号
			else if((sockfd == pipefd[0]) && (events[i].events & EPOLLIN)) //如果网络套接字等于管道读端文件描述符，并且事件是EPOLLIN形式
			{
				int sig;  //构建一个整型
				char signals[1024]; //构建一个字符串数组
				ret = recv(pipefd[0], signals, sizeof(signals), 0 ); //从管道读端接收到数据存入signals中，函数原型为int recv( SOCKET s, char *buf, int len, int flags);s指定接收端套接字描述符，buf指明一个缓冲区，该缓冲区用来存放recv函数接收到的数据，len指明buf的长度，flags一般置0，返回值<0 出错，=0 连接关闭，>0 接收到的数据长度大小
				if(ret == -1) //如果出错
				{
					continue;  //跳出本次循环，继续
				}
				else if(ret == 0) //如果连接关闭
				{
					continue;  //跳出本次循环，继续
				}
				else
				{
					for(int i = 0; i < ret; ++i) //对接收到的所有数据逐个执行
					{
						switch(signals[i])
						{
							case SIGALRM: //如果是SIGALRM
							{
								timeout = true; //timeout设为true
								break; //跳出循环，打断
							}
							case SIGTERM:  //如果是SIGTERM
							{
								stop_server = true; //停止服务器设为true
							}
						}
					}
				}
			}
      
      //处理客户连接上接收到的数据
			else if(events[i].events & EPOLLIN) //如果事件是EPOLLIN形式
			{
				util_timer *timer = users_timer[sockfd].timer;  //获取对应的定时器
				if(users[sockfd].read_once()) //如果读取客户数据
				{
					LOG_INFO("deal with the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr)); //输入到日志
					Log::get_instance()->flush(); //刷新缓冲区
           //若检测到读时间，将该事件放入请求队列
					pool->append(users + sockfd);

          //若有数据传输，则将定时器往后延迟3个单位
          //并对新的定时器在链表上的位置进行调整
					if(timer)  //如果定时器存在
					{
						time_t cur = time(NULL); //获取当前时间
						timer->expire = cur + 3 * TIMESLOT;  //设置超时时间
						LOG_INFO("%s", "adjust timer once"); //输入到日志
						Log::get_instance()->flush(); //刷新缓冲区
						timer_lst.adjust_timer(timer); //调整定时器
					}
				}
				else  //如果没读到客户数据
				{
					timer->cb_func(&users_timer[sockfd]); //调用回调函数
					if(timer) //如果定时器存在
					{
						timer_lst.del_timer(timer);  //删除定时器
					}
				}
			}
			else if(events[i].events & EPOLLOUT) //如果事件是EPOLLOUT形式
			{
				util_timer *timer = users_timer[sockfd].timer;  //获取对应的定时器
				if(users[sockfd].write()) //如果写入数据
				{
					LOG_INFO("send data to the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr)); //输入到日志
					Log::get_instance()->flush(); //刷新缓冲区

          //若有数据传输，则将定时器往后延迟3个单位
          //并对新的定时器在链表上的位置进行调整
					if(timer) //如果定时器存在
					{
						time_t cur = time(NULL);  //获取当前时间
						timer->expire = cur + 3 * TIMESLOT;  //设置超时时间
						LOG_INFO("%s", "adjust timer once"); //输入到日志
						Log::get_instance()->flush(); //刷新缓冲区
						timer_lst.adjust_timer(timer); //调整定时器
					}
				}
				else //如果没有写入
				{
					timer->cb_func(&users_timer[sockfd]); //调用回调函数
					if(timer) //如果定时器存在
					{
						timer_lst.del_timer(timer); //删除定时器
					}
				}
			}
		}
		if(timeout) //如果timeout为真
		{
			timer_handler(); //调用timer_handler函数，定时处理任务，重新定时以不断触发SIGALRM信号
			timeout = false; //timeout置为false
		}
	}
	close(epollfd); //关闭epollfd
	close(listenfd); //关闭listenfd
	close(pipefd[1]); //关闭pipefd[1]
	close(pipefd[0]); //关闭pipefd[0]
	delete[] users; //删除users数组
	delete[] users_timer; //删除users_timer数组
	delete pool; //删除pool
	return 0; //返回
}



