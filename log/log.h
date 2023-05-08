#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <iostream>
#include <string>
#include <stdarg.h>
#include <pthread.h>
#include "block_queue.h"

using namespace std;

class Log
{
public:
  //C++11以后，使用局部变量懒汉不用加锁
  //使用静态变量的单例模式
	static Log *get_instance()
	{
		static Log instance;
		return &instance;
	}

  //使静态变量调用async_write_log函数，用于线程异步写日志
	static void *flush_log_thread(void *args)
	{
		Log::get_instance()->async_write_log();
	}
  //可选择的参数有日志文件、日志缓冲区大小、最大行数以及最长日志条队列，日志缓冲区大小默认8192，日志最大行数默认5000000，最大阻塞队列数默认0
	bool init(const char *file_name, int log_buf_size = 8192, int split_lines = 5000000, int max_queue_size = 0);

	void write_log(int level, const char *format, ...);

	void flush(void);

private:
	Log();
	virtual ~Log();
 
 //异步写入日志
	void *async_write_log()
	{
		string single_log;
   //从阻塞队列中取出一个日志string，写入文件
		while(m_log_queue->pop(single_log))
		{
			m_mutex.lock(); //互斥锁锁定
			fputs(single_log.c_str(), m_fp); //将single_log内容写入日志文件
			m_mutex.unlock(); //互斥锁解锁
		}
	}

private:
	char dir_name[128]; //路径名
	char log_name[128]; //log文件名
	int m_split_lines; //日志最大行数
	int m_log_buf_size; //日志缓冲区大小
	long long m_count; //日志行数纪录
	int m_today; //因为按天分类，记录当前时间是那一天
	FILE *m_fp; //打开log的文件指针
	char *m_buf; //缓冲区指针
	block_queue<string> *m_log_queue; //阻塞队列
	bool m_is_async; //是否同步标志位，为true是异步
	locker m_mutex; //互斥锁
};

//宏定义，分别将LOG_DEBUG，LOG_INFO，LOG_WARN，LOG_ERROR改为调用write_log函数进行使用
#define LOG_DEBUG(format, ...) Log::get_instance()->write_log(0, format, ##__VA_ARGS__)
#define LOG_INFO(format, ...) Log::get_instance()->write_log(1, format, ##__VA_ARGS__)
#define LOG_WARN(format, ...) Log::get_instance()->write_log(2, format, ##__VA_ARGS__)
#define LOG_ERROR(format, ...) Log::get_instance()->write_log(3, format, ##__VA_ARGS__)

#endif

