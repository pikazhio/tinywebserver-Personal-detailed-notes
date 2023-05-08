#include "http_conn.h"
#include "../log/log.h"
#include <map>
#include <mysql/mysql.h>
#include <fstream>


//#define connfdET //边缘触发非阻塞
#define connfdLT //水平触发阻塞

//#define listenfdET //边缘触发非阻塞
#define listenfdLT //水平触发阻塞

//定义http相应的一些状态信息
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *errpe_400_form = "Your request has bad syntax or inherently impossible to staisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "Your do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";
//当浏览器出线连接重置时，可能时网络根目录出错或http响应格式出错或者访问的文件中内容完全为空
//定义存储根目录，需要改成自己的
const char *doc_root = "/home/pikazhio/TinyWebServer/root";

//将表中的用户名和密码存入map
map<string, string> users;
//互斥锁
locker m_lock;

//初始化mysql
void http_conn::initmysql_result(connection_pool *connPool)
{
  //先从连接池中去一个连接
	MYSQL *mysql = NULL; //构建一个MYSQL类并置为空
	connectionRAII mysqlcon(&mysql, connPool); //将构造出来的MYSQL类与连接池类封装成一个connectionRAII类

  //在user表中检索username，passwd数据，浏览器输入
	if(mysql_query(mysql, "SELECT username,passwd FROM user")) //进行MySQL查询，函数原型int mysql_query(MYSQL *mysql, const char *query)，其中mysql为MYSQL类，query为要发送的SQL查询
	{
		LOG_ERROR("SELECT error:%s\n",mysql_error(mysql)); //调用LOG_ERROR函数（在log.h中定义），将错误输入到log文件中。
	}

  //从表中检索完整的结果集
	MYSQL_RES *result = mysql_store_result(mysql); //函数原型为MYSQL_RES *mysql_store_result(MYSQL *mysql) 将查询的全部结果读取到客户端，分配1个MYSQL_RES结构，并将结果置于该结构中

  //返回结果集中的列数
	int num_fields = mysql_num_fields(result); //函数原型为unsigned int mysql_num_fields(MYSQL_RES *result)，用于返回结果集中的行数

  //返回所有字段结构的数组
	MYSQL_FIELD *fileds = mysql_fetch_fields(result); //函数原型为MYSQL_FIELD *mysql_fetch_fields(MYSQL_RES *result)，返回结果集的所有结构的数组

  //从结果集中获取下一行，将对应的用户名和密码，存入map中
	while(MYSQL_ROW row = mysql_fetch_row(result)) //函数原型为MYSQL_ROW mysql_fetch_row(MYSQL_RES *result)， 检索结果集的下一行。在mysql_store_result（)之后使用时，如果没有要检索的行，mysql_fetch_row()返回NULL（循环结束）。返回值为下一行的MYSQL_ROW结构
	{
		string temp1(row[0]); //用户名字符串
		string temp2(row[1]); //密码字符串
		users[temp1] = temp2; //将对应的用户名和密码，存入map中
	}
}

//对文件描述符设置非阻塞
int setnonblocking(int fd)
{
	int old_option = fcntl(fd, F_GETFL); //获得原有文件描述符标记，函数原型为int fcntl(int fd, int cmd);参数fd是被参数cmd操作的描述符，针对cmd的值,fcntl能够接受第三个参数，这里cmd为F_GETFL，功能是取得fd的文件状态标志
	int new_option = old_option | O_NONBLOCK; //将新文件描述符标志设置为非阻塞
	fcntl(fd, F_SETFL, new_option); //设置文件描述符标记，函数原型为int fcntl(int fd, int cmd, long arg);参数fd是被参数cmd操作的描述符，针对cmd的值,fcntl能够接受第三个参数，这里cmd为F_SETFL，功能是设置给arg描述符状态标志，这里将新文件描述符标志（已经设置为非阻塞了）设置
	return old_option; //返回原有文件描述符标记
}

//将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
void addfd(int epollfd, int fd, bool one_shot)
{
/* epoll_event结构体
typedef union epoll_data {
               * ptr; 无效
               int fd;
               uint32_t u32;
               uint64_t u64;
           } epoll_data_t;
 
           struct epoll_event {
               uint32_t事件； / * Epoll事件* /
               epoll_data_t数据； / *用户数据变量* /
 
 };
 */
	epoll_event event; //构造一个epoll_event结构体
	event.data.fd = fd; //将fd赋值给event

/* 设置事件
EPOLLIN ：表示对应的文件描述符可以读（包括对端SOCKET正常关闭）；
EPOLLOUT：表示对应的文件描述符可以写；
EPOLLPRI：表示对应的文件描述符有紧急的数据可读（这里应该表示有带外数据到来）；
EPOLLERR：表示对应的文件描述符发生错误； 
EPOLLHUP：表示对应的文件描述符被挂断；
EPOLLET： 将EPOLL设为边缘触发(Edge Triggered)模式，这是相对于水平触发(Level Triggered)来说的。
EPOLLONESHOT：只监听一次事件，当监听完这次事件之后，如果还需要继续监听这个socket的话，需要再次把这个socket加入到EPOLL队列里。
*/
#ifdef connfdET
	event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
#endif

#ifdef connfdLT
	event.events = EPOLLIN | EPOLLRDHUP;
#endif

#ifdef listenfdET
	event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
#endif

#ifdef listenfdLT
	event.events = EPOLLIN | EPOLLRDHUP;
#endif

	if(one_shot)
		event.events |= EPOLLONESHOT; //如果one_shot为真，则选择开启EPOLLONESHOT
	epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event); //函数原型为int epoll_ctl（int epfd，int op，int fd，struct epoll_event * event）;该系统调用对文件描述符epfd引用的epoll实例执行控制操作。它要求操作op对目标文件描述符fd执行。 op参数的有效值为：EPOLL_CTL_ADD：在文件描述符epfd所引用的epoll实例上注册目标文件描述符fd，并将事件事件与内部文件链接到fd。EPOLL_CTL_MOD：更改与目标文件描述符fd相关联的事件。EPOLL_CTL_DEL：从epfd引用的epoll实例中删除（注销）目标文件描述符fd。该事件将被忽略，并且可以为NULL）。事件参数描述链接到文件描述符fd的对象。
	setnonblocking(fd); //将文件描述符设置为非阻塞模式
}

//从内核事件表删除描述符
void removefd(int epollfd, int fd)
{
	epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0); //从epollfd删除文件描述符描述符
	close(fd); //关闭文件描述符
}

//将事件重置为EPOLLONESHOT
void modfd(int epollfd, int fd, int ev)
{
	epoll_event event; //构造一个epoll_event结构体
	event.data.fd = fd; //将fd赋值给event

//将事件重置为EPOLLONESHOT
#ifdef connfdET
	event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
#endif

#ifdef connfdLT
	event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
#endif

	epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event); //更改与目标文件描述符fd相关联的事件
}

//初始化静态变量
int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;

//关闭连接，关闭一个连接，客户总量减一
void http_conn::close_conn(bool real_close)
{
	if(real_close && (m_sockfd != -1)) //只有real_close为真，并且网络套接字不为-1时
	{
		removefd(m_epollfd, m_sockfd); //调用removefd函数，从内核事件表删除描述符
		m_sockfd = -1; //将网络套接字赋值为-1
		m_user_count --; //客户总量减一
	}
}

//初始化连接，外部调用初始化套接字地址
void http_conn::init(int sockfd, const sockaddr_in &addr)
{
	m_sockfd = sockfd; //将输入的sockfd赋值给m_sockfd
	m_address = addr; //将输入的addr赋值给m_address
	//int reuse = 1;
	//setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
	addfd(m_epollfd, sockfd, true); //调用addfd函数，将内核事件表注册事件
	m_user_count++; //客户总量加一
	init(); //调用init()函数
}

//初始化新接受的连接
//check_state默认为分析请求行状态
void http_conn::init()
{
	mysql = NULL; //mysql设为空
	bytes_to_send = 0; //剩余需要发送字节数设为0
	bytes_have_send = 0; //已发送字节数设为0
	m_check_state  = CHECK_STATE_REQUESTLINE; //主状态机状态设为检查状态请求行
	m_linger = false; //是否停留设为否
	m_method = GET; //http请求方法设为GET
	m_url = 0; //网络地址设为0
	m_version = 0; //版本设为0
	m_content_length = 0; //传输长度设为0
	m_host = 0; //系统文件设为0
	m_start_line = 0; //读缓冲区中已经解析的字符个数设为0
	m_checked_idx = 0; //读缓冲区中读取的位置设为0
	m_read_idx = 0; //读缓冲区中数据的最后一个字节的下一个位置设为0
	m_write_idx = 0; //写缓冲区中数据的最后一个字节的下一个位置设为0
	cgi = 0; //是否启用的POST设为0
	memset(m_read_buf, '\0', READ_BUFFER_SIZE); //给m_read_buf分配READ_BUFFER_SIZE大小
	memset(m_write_buf, '\0', WRITE_BUFFER_SIZE); //给m_write_buf分配WRITE_BUFFE_SIZE大小
	memset(m_real_file, '\0', FILENAME_LEN); //给m_real_file分配FILENAME_LEN大小
}

//从状态机，用于分析出一行内容
//返回值为行的读取状态，有LINE_OK, LINE_BAD, LIE_OPEN
http_conn::LINE_STATUS http_conn::parse_line()
{
	char temp; //构建一个新字符
	for(; m_checked_idx < m_read_idx; ++m_checked_idx) //当读缓冲区中读取的位置小于读缓冲区中数据的最后一个字节的下一个位置
	{
		temp = m_read_buf[m_checked_idx]; //获取读缓冲区读取位置字符
		if(temp == '\r') //如果该字符为 '\r' 回车
		{
			if((m_checked_idx + 1) == m_read_idx) //如果读取位置到都缓冲区最后了
				return LINE_OPEN; //返回LINE_OPEN
			else if(m_read_buf[m_checked_idx + 1] == '\n') //如果下一个读取位置字符为'\n' 换行符
			{
				m_read_buf[m_checked_idx++] = '\0'; //将当前读取位置设为'\0' 当前读取位置加一
				m_read_buf[m_checked_idx++] = '\0'; //将当前读取位置设为'\0' 当前读取位置加一
				return LINE_OK; //返回LINE_OK
			}
			return  LINE_BAD; //否则返回LINE_BAD
		}
		else if (temp == '\n') //如果该字符为 '\n' 换行符
		{
			if(m_checked_idx > 1 && m_read_buf[m_checked_idx - 1] == '\r') //如果存在前一个字符，且前一个字符为 '\r' 回车
			{
				m_read_buf[m_checked_idx - 1] = '\0'; //前一个字符设为'\0'
				m_read_buf[m_checked_idx++] = '\0'; //将当前读取位置设为'\0' 当前读取位置加一
				return LINE_OK; //返回LINE_OK
			}
			return LINE_BAD; //否则返回LINE_BAD
		}
	}
	return LINE_OPEN; //读缓冲区读取完返回LINE_OPEN
}

//循环读取客户数据，直到无数据可读或对方关闭连接
//非阻塞ET工作模式下，需要一次性将数据读完
bool http_conn::read_once()
{
	if(m_read_idx >= READ_BUFFER_SIZE) //如果读缓冲区中数据的最后一个字节的下一个位置大于等于最大读缓冲区大小
	{
		return false; //返回失败
  }

	int bytes_read = 0; //构造一个整数为0，存储读取字节数

#ifdef connfdLT
	bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);//用来接收远程主机通过套接字sockfd发送来的数据，并把这些数据保存到数组buf中，函数原型为int recv( SOCKET s, char *buf, int len, int flags);其中第一个参数指定接收端套接字描述符，这里为m_sockfd，第二个参数指明一个缓冲区，该缓冲区用来存放recv函数接收到的数据，这里为m_read_buf + m_read_idx，这里为读缓冲区数据的最后一个字节的下一个位置，第三个参数指明buf的长度，这里为READ_BUFFER_SIZE - m_read_idx，读缓冲区最多能存的长度，第四个参数一般置0。返回值<0 出错，=0 连接关闭，>0 接收到的数据长度大小，这里存储在bytes_read中，用于后续计算
	if(bytes_read <= 0) //如果接受出错或者连接关闭
	{
		return false; //返回false
	}

	m_read_idx += bytes_read; //否则读缓冲区中数据的最后一个字节的下一个位置向后接收到的数据长度大小

	return true; //返回true
#endif

#ifdef connfdET
	while(true) //循环
	{
		bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);//同上
		if(bytes_read == -1) //如果连接出错
		{
			if(errno == EAGAIN || errno == EWOULDBLOCK) //如果错误码为EAGAIN或者EWOULDBLOCK
				break; //打断
			return false; //返回false
		}
		else if(bytes_read == 0) //如果连接关闭
		{
			return false; //返回false
		}
		m_read_idx += bytes_read; //否则则读缓冲区中数据的最后一个字节的下一个位置向后接收到的数据长度大小
	}
	return true; //返回true
#endif
}

//解析http请求行，获得请求方法，目标url及http版本号
http_conn::HTTP_CODE http_conn::parse_request_line(char *text)
{
	m_url = strpbrk(text, " \t"); //在text中找到' ' 或'\t'的字符位置，函数原型为char *strpbrk(const char *str1, const char *str2)，检索字符串 str1 中第一个匹配字符串 str2 中字符的字符，不包含空结束字符，该函数返回 str1 中第一个匹配字符串 str2 中字符的字符数，如果未找到字符则返回 NULL
	if(!m_url) //如果m_url为空，未找到
	{
		return BAD_REQUEST; //返回BAD_REQUEST
	}
	*m_url++ = '\0'; //m_url位置改为'\0'，m_url位置加一
	char *method = text; //构建一个字符指针指向text
	if(strcasecmp(method, "GET") == 0) //忽略大小写，判断text是否为GET，函数原型为int strcasecmp (const char *s1, const char *s2);返回值等于0说明相等
		m_method = GET; //text为GET时，将m_method赋值为GET 
	else if(strcasecmp(method, "POST") == 0) //忽略大小写，判断text是否为POST
	{
		m_method = POST; //text为POST时，将m_method赋值为POST 
		cgi = 1; //是否启用POST值设为1，表示启用POST
	}
	else //否则
		return BAD_REQUEST; //返回BAD_REQUEST
	m_url += strspn(m_url, " \t"); //m_url指向第一个不是' '和't'的位置。函数原型为size_t strspn(const char *str1, const char *str2)， 检索字符串 str1 中第一个不在字符串 str2 中出现的字符下标。
	m_version = strpbrk(m_url, " \t"); //m_version指向m_url中第一个' ' 或'\t'的字符位置
	if(!m_version) //如果m_version为空，未找到
		return BAD_REQUEST; //返回BAD_REQUEST
	*m_version++ = '\0'; //m_version位置改为'\0'，m_version位置加一
	m_version += strspn(m_version, " \t"); //m_version指向第一个不是' '和't'的位置。
	if(strcasecmp(m_version, "HTTP/1.1")!= 0) //忽略大小写，判断m_version是否为"HTTP/1.1"
		return BAD_REQUEST; //如果不是，返回BAD_REQUEST
	if(strncasecmp(m_url, "http://", 7) == 0) //忽略大小写，判断m_url前七个字符是否为"http://"函数原型为int	strncasecmp(const char * s1, const char * s2, size_t n);与strcasecmp区别主要在于只比较前n个字符
	{
		m_url += 7; //m_url向后移七个字符
		m_url = strchr(m_url, '/'); //查找m_url中第一次出现'/'的位置。函数原型为char *strchr(const char *str, int c)，用于查找str字符串中的一个字符c，并返回该字符在字符串中第一次出现的位置
	}
	if(strncasecmp(m_url, "https://", 8) == 0) //忽略大小写，判断m_url前八个字符是否为"https://"
	{
		m_url += 8; //m_url向后移八个字符
		m_url = strchr(m_url, '/');//查找m_url中第一次出现'/'的位置
	}

	if(!m_url || m_url[0] != '/') //如果m_url为空，或者指向位置字符部位'/'
		return BAD_REQUEST; //返回BAD_REQUEST
	if(strlen(m_url) == 1) //如果字符串长度为1。函数原型为size_t  strlen (const char* str);计算的是字符串str的长度，从字符的首地址开始遍历，以 '\0' 为结束标志，然后将计算的长度返回，计算的长度并不包含'\0'
		strcat(m_url, "judge.html"); //m_url后面追加"judge.heml"，函数原型为char *strcat(char *dest, const char *src)，功能是实现字符串的拼接，dest：指向目标数组，该目标包含看一个C字符串，且足够容纳追加之后的字符串，src:　指向要追加的字符串。
	m_check_state = CHECK_STATE_HEADER; //主状态机状态设为CHECK_STATE_HEADER
	return NO_REQUEST; //返回NO_REQUEST
}

//解析http请求的一个头部信息
http_conn::HTTP_CODE http_conn::parse_headers(char *text)
{
	if(text[0]== '\0') //如果文本头为'\0'
	{
		if(m_content_length != 0) //如果传输长度不为0
		{
			m_check_state = CHECK_STATE_CONTENT; //主机状态改为CHECK_STATE_CONTENT
			return NO_REQUEST; //返回NO_REQUEST
		}
		return GET_REQUEST; //否则返回GET_REQUEST
	}
	else if(strncasecmp(text, "Connection:", 11) == 0) //如果text的前十一个字符（忽略大小写）是"Connection:"
	{
		text += 11; //text指针向后11字符
		text += strspn(text, " \t"); //text指针指向第一个不是是' '和't'的位置
		if(strcasecmp(text, "keep-alive") == 0) //如果text的内容为"keep-alive"
		{
			m_linger = true; //是否停留改为真
		}
	}
	else if(strncasecmp(text, "Content-length:", 15) == 0) //如果text的前十一个字符（忽略大小写）是"Content-length:"
	{
		text += 15;//text指针向后15字符
		text += strspn(text, " \t"); //text指针指向第一个不是是' '和't'的位置
		m_content_length = atol(text); //传输长度改为text转换为长整数的数值。函数原型为long int atol(const char *str)，str为要转换为长整数的字符串，该函数返回转换后的长整数，如果没有执行有效的转换，则返回零。
	}
	else if(strncasecmp(text, "Host:", 5) == 0) //如果text的前五个字符（忽略大小写）是"Host:"
	{
		text += 5;//text指针向后5字符
		text += strspn(text, " \t"); //text指针指向第一个不是是' '和't'的位置
		m_host = text; //主机名改为host
	}
	else
	{
		//printf("oop!unknow header: %s\n, text);
		LOG_INFO("oop!unknow header: %s", text);//调用LOG_INFO函数存入日志
		Log::get_instance()->flush(); //刷新缓冲区
	}
	return NO_REQUEST;//返回NO_REQUEST
}

//判断http请求是否被完整读入
http_conn::HTTP_CODE http_conn::parse_content(char *text)
{
	if(m_read_idx >= (m_content_length + m_checked_idx)) //当读缓冲区中数据的最后一个字节的下一个位置大于等于传输长度加上读缓冲区中读取的位置
	{
		text[m_content_length] = '\0'; //text传输长度后置为'\0'
   //POST请求中最后为输入的用户名和密码
		m_string = text; //text存入存储请求头数据
		return GET_REQUEST; //返回GET_REQUEST
	}
	return NO_REQUEST; //返回NO_REQUEST
}

//进程读取
http_conn::HTTP_CODE http_conn::process_read()
{
	LINE_STATUS line_status = LINE_OK; //从状态机状态设为LINE_OK
	HTTP_CODE ret = NO_REQUEST; //HTTP状态设为NO_REQUEST
	char *text = 0; //一个空指针

	while((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) || ((line_status =  parse_line()) == LINE_OK))//当主状态机为CHECK_STATE_CONTENT并且从状态为LINE_OK 或者行的读取状态为LINE_OK时
	{
		text =  get_line(); //读取数据到text
		m_start_line = m_checked_idx;//读缓冲区中已经解析的字符个数等于读缓冲区中读取的位置
		LOG_INFO("%s", text); //调用LOG_INFO函数存入日志
		Log::get_instance()->flush(); //刷新缓冲区
		switch(m_check_state) //根据主状态机状态
		{
		case CHECK_STATE_REQUESTLINE:
		{
			ret = parse_request_line(text); //解析http请求行，获得请求方法，目标url及http版本号并赋值给ret
			if(ret == BAD_REQUEST) //如果获得BAD_REQUEST
				return BAD_REQUEST; //返回BAD_REQUEST
			break;
		}
		case CHECK_STATE_HEADER:
		{
			ret = parse_headers(text);//解析http请求的一个头部信息并赋值给ret
			if(ret == BAD_REQUEST)//如果获得BAD_REQUEST
        return BAD_REQUEST; //返回BAD_REQUEST
			else if	(ret == GET_REQUEST) //如果获得GET_REQUEST
			{
				return do_request(); //调用do_request函数并返回
			}
			break;
		}
		case CHECK_STATE_CONTENT:
		{
			ret = parse_content(text); //从状态机分析出一行内容并赋值给ret
			if(ret == GET_REQUEST) //如果获得GET_REQUEST
				return do_request(); //调用do_request函数并返回
			line_status = LINE_OPEN; //从状态机状态改为LINE_OPEN
			break;
		}
		default:
			return INTERNAL_ERROR; //返回INTERNAL_ERROR
		}
	}
	return NO_REQUEST; //返回NO_REQUEST
}

//执行
http_conn::HTTP_CODE http_conn::do_request()
{
	strcpy(m_real_file, doc_root); //把doc_root指向字符串复制到m_real_file。函数原型为char *strcpy(char *dest, const char *src)，其中dest为指向用于存储复制内容的目标数组，src为要复制的字符串，返回一个指向最终的目标字符串 dest 的指针
	int len = strlen(doc_root); //获取doc_root长度
	//printf("m_url:%s\n",m_url);
	const char *p = strrchr(m_url, '/');//在m_url字符串中搜索最后一次出现'/'的位置，函数原型为char *strrchr(const char *str, int c)，其中str为被搜索的字符串c为要搜索的字符，返回 str 中最后一次出现字符 c 的位置。如果未找到该值，则函数返回一个空指针

  //处理cgi
	if(cgi == 1 && (*(p+ 1) == '2' || *(p + 1) == '3')) //如果启用POST并且m_url最后一个/后面为2或者3
	{
    //根据标志判断是登陆检测还是注册检测
		char flag = m_url[1]; //获取标志

		char *m_url_real = (char *)malloc(sizeof(char) * 200); //分配一个区域给m_url_real
		strcpy(m_url_real, "/"); //将'/'复制到m_url_real
		strcat(m_url_real, m_url + 2); //将m_url两个字符之后内容追加到m_url_real
		strncpy(m_real_file + len, m_url_real, FILENAME_LEN - len - 1); //把m_url_real所指向的字符串复制到m_real_file + len，最多复制FILENAME_LEN - len - 1个字符，函数原型为char *strncpy(char *dest, const char *src, size_t n)，其中dest指向用于存储复制内容的目标数组，src为要复制的字符串，n要为从源中复制的字符数，该函数返回最终复制的字符串
		free(m_url_real); //释放m_url_real空间

    //将用户名和密码提取出来
    //user=123&passwd=123
		char name[100],password[100]; //构建连个数组分别存放用户名和密码
		int i; //构建一个证书
		for(i = 5; m_string[i] != '&'; ++ i) //从请求头数据第五个字符到'&'字符
			name[i - 5] = m_string[i]; //将内容存入用户名数组中
		name[i - 5] = '\0'; //用户名数组最后一个为'\0'

		int j = 0;
		for(i = i + 10; m_string[i] != '\0'; ++i, ++j) //从请求头数据去除用户名后第十个字符到'\0'字符
			password[j] = m_string[i]; //将内容存入密码数组中
		name[j] = '\0'; //密码数组最后一个为'\0'

    //同步线程登录检验
		if(*(p + 1) == '3')
		{
       //如果是注册，先检查数据库中是否有重名
       //没有重名的，进行增加数据
			char *sql_insert = (char *)malloc(sizeof(char) * 200); //分配一个区域给sql_insert
			strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES("); //将"INSERT INTO user(username, passwd) VALUES("复制到sql_insert
      strcat(sql_insert, "'");//后面追加"'"
			strcat(sql_insert, name); //后面追加名字
			strcat(sql_insert, "','"); //后面追加"','"
			strcat(sql_insert, password);//后面追加密码
			strcat(sql_insert, "')");//后面追加")"

			if(users.find(name) == users.end()) //如果没有重名
			{
				m_lock.lock(); //互斥锁加锁
				int res = mysql_query(mysql, sql_insert); //向mysql发送sql_insert查询
				users.insert(pair<string, string>(name, password)); //插入名字和密码
				m_lock.unlock(); //互斥锁解锁

				if(!res) //如果成功
					strcpy(m_url, "/log.html"); //m_url被复制为"/log.html"
				else
					strcpy(m_url, "/registerError.html"); //否则m_url被复制为"/registerError.html"
			}
			else
				strcpy(m_url, "/registerError.html"); //如果有重名，m_url被复制为"/registerError.html"
		}

    //如果是登录，直接判断
    //若浏览器端输入的用户名和密码在表中可以查找到，返回1，否则返回0
		else if(*(p + 1) == '2') //如果是登录
		{
			if(users.find(name) != users.end() && users[name] == password) //如果输入的用户名能找到，并且密码匹配
				strcpy(m_url, "/welcome.html"); //m_url被复制为"/welcome.html"
			else
				strcpy(m_url, "/logError.html"); //否则m_url被复制为"/logError.html"
		}
	}
	if(*(p + 1) == '0')
	{
		char *m_url_real = (char *)malloc(sizeof(char) * 200);  //分配一个区域给m_url_real
		strcpy(m_url_real, "/register.html"); //m_url被复制为"/register.html"
		strncpy(m_real_file + len, m_url_real, strlen(m_url_real)); //将m_url_real复制到m_ral_file + len后面

		free(m_url_real); //释放m_url_real空间
	}
	else if(*(p + 1) == '1')
	{
		char *m_url_real = (char *)malloc(sizeof(char) * 200);  //分配一个区域给m_url_real
		strcpy(m_url_real, "/log.html"); //m_url被复制为"/log.html"
		strncpy(m_real_file + len, m_url_real, strlen(m_url_real)); //将m_url_real复制到m_ral_file + len后面

		free(m_url_real); //释放m_url_real空间
	}
	else if(*(p + 1) == '5')
	{
		char *m_url_real = (char *)malloc(sizeof(char) * 200);  //分配一个区域给m_url_real
		strcpy(m_url_real, "/picture.html"); //m_url被复制为"/picture.html"
		strncpy(m_real_file + len, m_url_real, strlen(m_url_real)); //将m_url_real复制到m_ral_file + len后面

		free(m_url_real);//释放m_url_real空间
	}
	else if(*(p + 1) == '6')
	{
		char *m_url_real = (char *)malloc(sizeof(char) * 200); //分配一个区域给m_url_real
		strcpy(m_url_real, "/video.html");//m_url被复制为"/video.html"
		strncpy(m_real_file + len, m_url_real, strlen(m_url_real));//将m_url_real复制到m_ral_file + len后面

		free(m_url_real);//释放m_url_real空间
	}
	else if(*(p + 1) == '7')
	{
		char *m_url_real = (char *)malloc(sizeof(char) * 200);//分配一个区域给m_url_real
		strcpy(m_url_real, "/fans.html");//m_url被复制为"/fans.html"
		strncpy(m_real_file + len, m_url_real, strlen(m_url_real));//将m_url_real复制到m_ral_file + len后面

		free(m_url_real);//释放m_url_real空间
	}
	else strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1); //将最多FILENAME_LEN - len - 1长度m_url复制到m_ral_file + len后面

	if(stat(m_real_file, &m_file_stat) < 0) //如果获取文件信息失败。函数原型为int stat(const char *path, struct stat *buf)，path为需要查看属性的文件路径，buf为stat结构体，获取到的文件属性信息就记录在 struct stat 结构体中，成功返回0，失败返回-1
		return NO_RESOURCE; //返回NO_RESOURCE
	if(!(m_file_stat.st_mode & S_IROTH)) //如果不含有S_IROTH属性
		return FORBIDDEN_REQUEST; //返回FORBIDDEN_REQUEST
	if(S_ISDIR(m_file_stat.st_mode)) //如果路径不是目录
		return BAD_REQUEST; //返回BAD_REQUEST
	int fd = open(m_real_file, O_RDONLY); //以只读方式打开m_real_file，文件描述符为fd
	m_file_address = (char *)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);//m_file_address指向被映射区的指针，函数原型为void *mmap(void *start, size_t length, int prot, int flags,int fd, off_t offset);start：映射区的开始地址 length：映射区的长度 prot：期望的内存保护标志，不能与文件的打开模式冲突。flags：指定映射对象的类型，映射选项和映射页是否可以共享。fd：有效的文件描述词。offset：被映射对象内容的起点
	close(fd); //关闭文件描述符
	return FILE_REQUEST; //返回FILE_REQUEST
}

//取消内存映射
void http_conn::unmap()
{
	if(m_file_address) //如果读服务器的文件地址不为空
	{
		munmap(m_file_address, m_file_stat.st_size); //取消内存映射。函数原型为int munmap(void *addr, size_t length); 其中addr为映射区的开始地址，设置为0时表示由系统决定映射区的起始地址，length为映射区的长度，解除成功返回0，出错返回-1
		m_file_address = 0; //读服务器的文件地址设为空
	}
}

//写入数据
bool http_conn::write()
{
	int temp = 0; //构建一个整数
	if(bytes_to_send == 0) //如果剩余需要发送字节数为0
	{
		modfd(m_epollfd, m_sockfd, EPOLLIN); //调用modfd函数，将事件重置为EPOLLONESHOT
		init(); //调用init函数，初始化
		return true; //返回true
	}

	while(1) //循环
	{
		temp = writev(m_sockfd, m_iv, m_iv_count); //在一次函数调用中写多个非连续缓冲区，并将已写的字节数赋值给temp，函数原型为ssize_t writev(int fd, const struct iovec *iov, int iovcnt);将iov所指定的所有缓存区中的数据拼接(“集中”)起来，然后以连续的字节序列写入文件描述符fd指代的文件中，成功则返回已写的字节数，若出错则返回-1

		if(temp < 0) //如果写入失败
		{
			if(errno == EAGAIN) //如果errno为EAGAIN
			{
				modfd(m_epollfd, m_sockfd, EPOLLOUT); //调用modfd函数，将事件重置为EPOLLONESHOT
				return true; //返回true
			}
			unmap();//取消内存映射
			return false; //返回false
		}

		bytes_have_send += temp; //已发送字节数增加写入数
		bytes_to_send -= temp; //剩余需要发送字节数减去写入数
		if(bytes_have_send >= m_iv[0].iov_len) //如果已发送字节数大于等于第一个iovec信息的数据（第一个iovec信息的数据已经发送完）
		{
			m_iv[0].iov_len = 0; //第一个iovec信息的数据设为0 不再发送该数据
			m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx); //更新第二个iovec指向缓冲区位置
			m_iv[1].iov_len = bytes_to_send; //更新第二个iovec发送长度
		}
		else //第一个iovec数据没有发完
		{
			m_iv[0].iov_base = m_write_buf + bytes_have_send; //更新第一个iovec指向缓冲区位置
			m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send; //更新第一个iovec发送长度
		}

		if(bytes_to_send <= 0) //如果剩余需要发送字节数小于等于0
		{
			unmap(); //取消内存映射
			modfd(m_epollfd, m_sockfd, EPOLLIN);  //调用modfd函数，将事件重置为EPOLLONESHOT

			if(m_linger) //如果停留
			{
				init(); //调用init函数重新初始化
				return true; //返回true
			}
			else //如果不停留
			{
				return false; //返回false
			}
		}
	}
}

//添加相应报文
bool http_conn::add_response(const char *format, ...)
{
	if(m_write_idx >= WRITE_BUFFER_SIZE) //如果写缓冲区中数据的最后一个字节的下一个位置大于写缓冲区大小
		return false; //返回false
	va_list arg_list; //构建一个va_list结构体，typedef char *va_list;
	va_start(arg_list, format); //获取可变参数列表的第一个参数的地址
	int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list); //向写缓冲区写入可变参数列表内信息，并将写入长度存入len中，函数原型为：int vsnprintf (char * sbuf, size_t n, const char * format, va_list arg );用于向一个字符串缓冲区打印格式化字符串，且可以限定打印的格式化字符串的最大长度。参数sbuf：用于缓存格式化字符串结果的字符数组，参数n：限定最多打印到缓冲区sbuf的字符的个数为n-1个，因为vsnprintf还要在结果的末尾追加\0。如果格式化字符串长度大于n-1，则多出的部分被丢弃。如果格式化字符串长度小于等于n-1，则可以格式化的字符串完整打印到缓冲区sbuf。一般这里传递的值就是sbuf缓冲区的长度，参数format：格式化限定字符串，参数arg：可变长度参数列表，返回：成功打印到sbuf中的字符的个数，不包括末尾追加的\0。如果格式化解析失败，则返回负数。

	if(len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx)) //如果写入长度大于等于写缓冲区大小剩余大小
	{
		va_end(arg_list); //关闭可变参数列表
		return false; //返回false
	}
	m_write_idx += len; //写缓冲区中数据的最后一个字节的下一个位置加上写入大小
	va_end(arg_list); //关闭可变参数列表
	LOG_INFO("request:%s", m_write_buf); //调用LOG_INFO写入日志
	Log::get_instance()->flush(); //刷入缓冲区
	return true; //返回true
}

//添加状态行
bool http_conn::add_status_line(int status, const char *title)
{
	return add_response("%s %d %s\r\n", "HTTP/1.1", status, title); //调用add_response添加相应报文
}

//添加消息报头
bool http_conn::add_headers(int content_len)
{
	add_content_length(content_len); //调用add_content_length函数
	add_linger(); //调用add_linger函数
	add_blank_line(); //调用add_blank_line函数
}

//添加Content-Length，表示响应报文的长度
bool http_conn::add_content_length(int content_len)
{
	return add_response("Content-Length:%d\r\n", content_len); //调用add_response添加相应报文
}
//添加文本类型
bool http_conn::add_content_type()
{
	return add_response("Content-Type:%s\r\n", "text/html"); //调用add_response添加相应报文
}
//添加连接状态，通知浏览器端是保持连接还是关闭
bool http_conn::add_linger()
{
	return add_response("Connection:%s\r\n", (m_linger == true)? "keep-alive" : "close"); //调用add_response添加相应报文
}
//添加空行
bool http_conn::add_blank_line()
{
	return add_response("%s", "\r\n"); //调用add_response添加相应报文
}
//添加文本content
bool http_conn::add_content(const char *content)
{
	return add_response("%s", content); //调用add_response添加相应报文
}

//生成响应报文
bool http_conn::process_write(HTTP_CODE ret)
{
	switch(ret) //根据http状态
	{
	case INTERNAL_ERROR:
	{
		add_status_line(500, error_500_title); //调用add_status_line函数添加状态行
		add_headers(strlen(error_500_form)); //调用add_headers函数添加消息报头
		if(!add_content(error_500_form)) //如果没有添加文本content
			return false; //返回false
		break;
	}
	case BAD_REQUEST:
	{
		add_status_line(404, error_404_title); //调用add_status_line函数添加状态行
		add_headers(strlen(error_404_form)); //调用add_headers函数添加消息报头
		if(!add_content(error_404_form)) //如果没有添加文本content
			return false; //返回false
		break;
	}
	case FORBIDDEN_REQUEST:
	{
		add_status_line(403, error_403_title); //调用add_status_line函数添加状态行
		add_headers(strlen(error_403_form)); //调用add_headers函数添加消息报头
		if(!add_content(error_403_form)) //如果没有添加文本content
			return false; //返回false
		break;
	}
	case FILE_REQUEST:
	{
		add_status_line(200, ok_200_title); //调用add_status_line函数添加状态行
		if(m_file_stat.st_size != 0) //指定路径的文件或者文件夹的信息的文件大小不是0
		{
			add_headers(m_file_stat.st_size);  //调用add_headers函数添加消息报头
			m_iv[0].iov_base = m_write_buf; //第一个iovec指向地址为写缓冲区
			m_iv[0].iov_len = m_write_idx; //第一个iovec长度为写缓冲区中数据的最后一个字节的下一个位置
			m_iv[1].iov_base = m_file_address; //第二个iovec指向地址为读服务器的文件地址
			m_iv[1].iov_len = m_file_stat.st_size; //第二个iovec长度为指定路径的文件或者文件夹的信息的文件大小
			m_iv_count = 2; //m_iv的计数为2
			bytes_to_send = m_write_idx + m_file_stat.st_size; //剩余需要发送字节数更新
			return true; //返回true
		}
		else
		{
			const char *ok_string = "<html><body></body></html>"; //构建一个字符串存入消息报头信息
			add_headers(strlen(ok_string));  //调用add_headers函数添加消息报头
			if(!add_content(ok_string)) //如果没有添加文本content
				return false; //返回false
		}
	}
	default:
		return false; //返回false
	}
	m_iv[0].iov_base = m_write_buf;//第一个iovec指向地址为写缓冲区
	m_iv[0].iov_len = m_write_idx;//第一个iovec长度为写缓冲区中数据的最后一个字节的下一个位置
	m_iv_count = 1; //m_iv的计数为1
	bytes_to_send = m_write_idx; //剩余需要发送字节数更新
	return true; //返回true
}
//进程
void http_conn::process()
{
	HTTP_CODE read_ret = process_read(); //调用process_read函数进行进程读取并存入http状态
	if(read_ret == NO_REQUEST) //如果是NO_REQUEST
	{
		modfd(m_epollfd, m_sockfd, EPOLLIN); //将事件重置为EPOLLONESHOT
		return; //返回
	}
	bool write_ret = process_write(read_ret); //生成响应报文并将返回值存入write_ret
	if(!write_ret) //如果生成失败
	{
		close_conn();//调用close_conn函数，关闭连接
	}
	modfd(m_epollfd, m_sockfd, EPOLLOUT);//将事件重置为EPOLLONESHOT
}










