#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <stdarg.h>
#include "log.h"
#include <pthread.h>

using namespace std;

//构造函数
Log::Log()
{
	m_count = 0; //日志行数初始化为0
	m_is_async = false; //是否同步标志位初始化为false，表示为同步
}

//析构函数
Log::~Log()
{
	if(m_fp != NULL) //如果日志文件指针不为空
	{
		fclose(m_fp); //关闭日志文件指针
	}
}

//Log类初始化
//异步需要设置组赛队列的长度，同步不需要设置
bool Log::init(const char *file_name, int log_buf_size, int split_lines, int max_queue_size)
{
	if(max_queue_size >= 1) //如果阻塞队列大小大于等于1
	{
		m_is_async = true; //是否同步标志位设为true，表示为异步
		m_log_queue = new block_queue<string>(max_queue_size); //建立max_queue_size大小的阻塞队列
		pthread_t tid; //建立一个线程
   //flush_log_thread为回调函数，这里表示创建线程异步写日志
		pthread_create(&tid, NULL, flush_log_thread, NULL); //用于创建线程的函数，这里用于异步写日志。函数原型为int pthread_create(pthread_t* restrict tidp,const pthread_attr_t* restrict_attr,void* (*start_rtn)(void*),void *restrict arg);其中tidp为事先创建好的pthread_t类型的参数，成功时被设置为新创建线程的线程id，attr用于定制不同的线程属性，start_rtn表示新创建线程从此函数开始运行，这里为flush_log_thread，进行异步写日志，arg为start_rtn函数的参数，这里无参数，为NULL
	}

	m_log_buf_size = log_buf_size; //将log_buf_size赋值给日志缓冲区大小
	m_buf = new char[m_log_buf_size]; //建立一个m_log_buf_size大小的缓冲区
	memset(m_buf, '\0', m_log_buf_size); //将缓冲区全部设置为\0。函数原型为void *memset(void *s, int c, size_t n); s指向要填充的内存块，c是要被设置的值，n是要被设置该值的字符数。
	m_split_lines = split_lines; //将split_lines赋值给日志最大行数


	time_t t = time(NULL); //获取从1970-1-1, 00:00:00到当前时间系统所偏移的秒数时间
	struct tm *sys_tm = localtime(&t); //获取从1970-1-1, 00:00:00到当前时间系统所偏移的秒数时间转换为本地时间
	struct tm my_tm = *sys_tm; //时间结构体

	const char *p = strrchr(file_name, '/'); //搜索在file_name所指向字符串中最后一次出现'/'的位置。函数原型为char *strrchr(const char *str, int c)
	char log_full_name[256] = {0}; //存放日志名字的指针

	if(p == NULL) //如果没在file_name中找到‘/’
	{
		snprintf(log_full_name, 255, "%d_%02d_%02d_%s", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, file_name); //将格式化的数据写入log_full_name字符串。函数原型为int snprintf(char *str, int n, char * format [, argument, …]);str为要写入的字符串；n为要写入的字符的最大数目，超过n会被截断；format为格式化字符串，与printf()函数相同；argument为变量。snprintf的返回值n，当调用失败时，n为负数，当调用成功时，n为格式化的字符串的总长度（不包括\0），当然这个字符串有可能被截断，因为buf的长度不够放下整个字符串。
	}
	else
	{
		strcpy(log_name, p + 1); //将‘/’后面字符串复制到log_name中。函数原型为char*strcpy（char*dest，const char*src）;将src复制到dest字符数组中
		strncpy(dir_name, file_name, p - file_name + 1); //将‘/’前字符串复制到dir_name中。函数原型为char *strncpy( char *dest, const char *source, size_t count );将source中count个字符复制到dest中
		snprintf(log_full_name, 255, "%s%d_%02d_%02d_%s", dir_name, my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, log_name); //将格式化的数据写入log_full_name字符串
	}

	m_today = my_tm.tm_mday; //获取今天是那一天

	m_fp = fopen(log_full_name, "a"); //以追加方式打开名字为log_full_name的文件，并使m_fp指向。函数原型为FILE *fopen(char *filename, char *mode);filename为文件名（包括文件路径），mode为打开方式，返回一个FILE类型的指针
	if(m_fp == NULL) //如果打开失败
	{
		return false; //返回false
	}

	return true; //返回true
}

//写入日志
void Log::write_log(int level, const char *format, ...)
{
	struct timeval now = {0, 0};
	gettimeofday(&now, NULL);  //获取当前时间，放置在now中。函数原型为int gettimeofday(struct  timeval*tv,struct  timezone *tz )，目前的时间用tv 结构体返回，当地时区的信息则放到tz所指的结构中
	time_t t = now.tv_sec; //获取当前时间秒数
	struct tm *sys_tm = localtime(&t); //转换为本地时间
	struct tm my_tm = *sys_tm; //存入my_tm中
	char s[16] = {0};
	switch(level) //通过level进行判断，这里与头文件宏定义部分相对应。判断后将相关信息复制到s中。
	{
	case 0:
		strcpy(s, "[debug]:"); 
		break;
	case 1:
		strcpy(s, "[info]:");
		break;
	case 2:
		strcpy(s, "[warn]:");
		break;
	case 3:
		strcpy(s, "[erro]:");
		break;
	defult:
		strcpy(s, "[info]:");
		break;
	}
 //写入一个log，对m_count++，m_spilt_lines最大行数
	m_mutex.lock(); //互斥锁加锁
	m_count++; //日志行数加一

	if(m_today != my_tm.tm_mday || m_count % m_split_lines == 0) //如果当前天和数组记录天不一样，或者日志行数为m_spilt_lines最大行数的倍数
	{
		char new_log[256] = {0};
		fflush(m_fp); // 刷新流m_fp指向的log文件的输出缓冲区。函数原型为int fflush(FILE *stream)， 刷新流 stream 的输出缓冲区。
		fclose(m_fp); //关闭m_fp指向的log文件。函数原型为int close(int fd)。
		char tail[16] = {0};

		snprintf(tail, 16, "%d_%02d_%02d_", my_tm.tm_year + 1900, my_tm.tm_mon +1, my_tm.tm_mday); //将当前时间信息写入到tail字符串中

		if(m_today != my_tm.tm_mday) //如果是因为天时间不对
		{
			snprintf(new_log, 255, "%s%s%s", dir_name, tail, log_name); //将路径 时间 日志名写入到new_log字符串中
			m_today = my_tm.tm_mday; //修改当前天数
			m_count = 0; //日志行数设为0
		}
		else
		{
			snprintf(new_log, 255, "%s%s%s.%lld", dir_name, tail, log_name, m_count / m_split_lines); //将路径 时间 日志名 m_count / m_spilt_lines（因超过最大行数，所以原日志后加m_count / m_spilt_lines进行记录）写入到new_log字符串中
		}
		m_fp = fopen(new_log, "a"); //以追加方式打开名字为new_log的文件，并使m_fp指向。
	}

	m_mutex.unlock(); //互斥锁解锁

	va_list valst; //可变参数列表类型， 是在C语言中解决变参问题的一组宏
	va_start(valst, format); //初始化valst变量。函数原型为void va_start(va_list ap, last_arg);ap是一个 va_list 类型的对象，它用来存储通过 va_arg 获取额外参数时所必需的信息，last_arg是最后一个传递给函数的已知的固定参数。

	string log_str; 
	m_mutex.lock(); //互斥锁加锁

//写入的具体时间内容格式
	int n = snprintf(m_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, s); //将格式化的信息写入m_buf，返回写入的长度n

	int m =vsnprintf(m_buf + n, m_log_buf_size - 1, format, valst); //通过valst从可变参数列表中以format格式写入m_buf + n后面，最大为m_log_buf_size - 1，返回写入的长度m。函数原型为int vsnprintf (char * s, size_t n, const char * format, va_list arg );s指向存储结果C字符串的缓冲区的指针，缓冲区应至少有n个字符的大小。n在缓冲区中使用的最大字节数，生成的字符串的长度至多为n-1，为额外的终止空字符留下空间，size_t是一个无符号整数类型。format包含格式字符串的C字符串，其格式字符串与printf中的格式相同。arg标识使用va_start初始化的变量参数列表的值。返回值：如果n足够大，则会写入的字符数，不包括终止空字符。如果发生编码错误，则返回负数，注意，只有当这个返回值是非负值且小于n时，字符串才被完全写入。
	m_buf[n + m] = '\n';  //缓冲区写入'\n'
	m_buf[n + m + 1] = '\0'; //缓冲区写入'\0'
	log_str = m_buf; //将缓冲区内信息存入到log_str

	m_mutex.unlock(); //互斥锁解锁

	if(m_is_async && !m_log_queue->full()) //如果异步模式，并且阻塞队列不满
	{
		m_log_queue->push(log_str); //将log_str也就是缓冲区内信息push到阻塞队列
	}
	else //否则
	{
		m_mutex.lock(); //互斥锁锁定
		fputs(log_str.c_str(), m_fp); //直接将log_str也就是缓冲区信息输出到文件中。函数原型为int fputs(const char *s, FILE *stream);s 代表要输出的字符串的首地址，可以是字符数组名或字符指针变量名。stream 表示向何种流中输出，可以是标准输出流 stdout，也可以是文件流。标准输出流即屏幕输出，printf 其实也是向标准输出流中输出的。

		m_mutex.unlock(); //互斥锁解锁
	}

	va_end(valst); //释放指针valst。函数原型为void va_end ( va_list ap );与va_start成对出现
}

//强制刷新写入流缓冲区
void Log::flush(void)
{
	m_mutex.lock(); //互斥锁锁定
 //强制刷新写入流缓冲区
	fflush(m_fp);
	m_mutex.unlock(); //互斥锁解锁
}
