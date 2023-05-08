#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"

class http_conn
{
public:
  //读取文件名字长度
	static const int FILENAME_LEN = 200;
   //读缓冲区大小
	static const int READ_BUFFER_SIZE = 2048;
   //写缓冲区大小
	static const int WRITE_BUFFER_SIZE = 1024;
   //枚举http方法
	enum METHOD
	{
		GET = 0,
		POST,
		HEAD,
		PUT,
		DELETE,
		TRACE,
		OPTIONS,
		CONNECT,
		PATH
	};
   //枚举主状态机的状态
	enum CHECK_STATE
	{
		CHECK_STATE_REQUESTLINE = 0,
		CHECK_STATE_HEADER,
		CHECK_STATE_CONTENT
	};
   //枚举HTTP状态
	enum HTTP_CODE
	{
		NO_REQUEST,
		GET_REQUEST,
		BAD_REQUEST,
		NO_RESOURCE,
		FORBIDDEN_REQUEST,
    FILE_REQUEST,
		INTERNAL_ERROR,
		CLOSED_CONNECTION
	};
   //枚举从状态机状态
	enum LINE_STATUS
	{
		LINE_OK = 0,
		LINE_BAD,
		LINE_OPEN
	};

public:
//构造函数（内容为空）
	http_conn() {}
//析构函数（内容为空）
	~http_conn() {}


	void init(int sockfd, const sockaddr_in &addr);
	void close_conn(bool real_close = true);
	void process();
	bool read_once();
	bool write();
 //获取地址信息
	sockaddr_in *get_address()
	{
		return &m_address; //返回地址信息
	}
	void initmysql_result(connection_pool *connPool);

private:
	void init();
	HTTP_CODE process_read();
	bool process_write(HTTP_CODE ret);
	HTTP_CODE parse_request_line(char *text);
	HTTP_CODE parse_headers(char *text);
	HTTP_CODE parse_content(char *text);
	HTTP_CODE do_request();
 //获取未处理的字符位置
	char *get_line() {return m_read_buf + m_start_line;}; 
	LINE_STATUS parse_line();
	void unmap();
	bool add_response(const char *format, ...);
	bool add_content(const char *content);
	bool add_status_line(int status, const char *title);
	bool add_headers(int content_length);
	bool add_content_type();
	bool add_content_length(int content_length);
	bool add_linger();
	bool add_blank_line();

public:
	static int m_epollfd; //静态变量 epoll的文件描述符
	static int m_user_count; //静态变量 用户计数
	MYSQL *mysql; //MYSQL类

private:
	int m_sockfd; //socket网络文件描述符
	sockaddr_in m_address; //sockaddr_in类，主要存储地址族 端口号 ip地址
	char m_read_buf[READ_BUFFER_SIZE]; //READ_BUFFER_SIZE大小的读缓存数组
	int m_read_idx; //读缓冲区中数据的最后一个字节的下一个位置
	int m_checked_idx; //读缓冲区中读取的位置
	int m_start_line; //读缓冲区中已经解析的字符个数
	char m_write_buf[WRITE_BUFFER_SIZE]; //READ_BUFFER_SIZE大小的写缓存数组
	int m_write_idx; //写缓冲区中数据的最后一个字节的下一个位置
	CHECK_STATE m_check_state; //主状态机的状态
	METHOD m_method; //http请求方法
	char m_real_file[FILENAME_LEN]; //用来存储读取文件的名称的数组
	char *m_url; //网络地址
	char *m_version; //版本
	char *m_host; //主机名
	int m_content_length; //传输长度
	bool m_linger; //是否停留
	char *m_file_address; //读服务器的文件地址
	struct stat m_file_stat; //stat结构体，用来获取指定路径的文件或者文件夹的信息
	struct iovec m_iv[2]; //io向量机制iovec
	int m_iv_count; //m_iv的计数
	int cgi; //是否启用的POST
	char *m_string; //存储请求头数据
	int bytes_to_send; //剩余需要发送字节数
	int bytes_have_send; //已发送字节数
};

#endif
