#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"

//模板类
template <typename T>
class threadpool
{
public:
//thread_number是线程池中线程的数量，默认值为8，max_requerst是请求队列中最多允许的、等待处理的请求的数量，默认值为10000
	threadpool(connection_pool *connPool, int threadd_number = 8, int max_request = 10000);
	~threadpool();
	bool append(T *request);

private:
//工作线程运行的函数，它不断从工作队列中取出任务并执行之
	static void *worker(void *arg);
	void run();

private:
	int m_thread_number; //线程池中的线程数
	int m_max_requests; //请求队列中允许的最大请求数
	pthread_t *m_threads;  //描述线程池的数组，其大小为m_thread_number
	std::list<T *> m_workqueue; //请求队列
	locker m_queuelocker; //保护请求队列的互斥锁
	sem m_queuestat; //是否有任务需要处理
	bool m_stop; //是否结束线程
	connection_pool *m_connPool; //数据库
};

//构造函数，将thread_number赋值给m_thread_number，将max_requests赋值给m_max_requests，将m_stop赋值为false，将m_threads赋值为空，将connPool赋值给m_connPool
template <typename T>
threadpool<T>::threadpool(connection_pool *connPool, int thread_number, int max_requests): m_thread_number(thread_number), m_max_requests(max_requests), m_stop(false), m_threads(NULL), m_connPool(connPool)
{
	if(thread_number <= 0 || max_requests < 0) //如果给定数值不合理：线程池中的线程数小于等于0，请求队列中允许的最大请求数小于0
		throw std::exception(); //抛出异常
	m_threads = new pthread_t[m_thread_number]; //建立一个描述线程池的数组
	if(!m_threads) //如果建立失败
		throw std::exception(); //抛出异常
	for(int i = 0; i < thread_number; ++i) //依次建立thread_number个线程
	{
		if(pthread_create(m_threads + i, NULL, worker, this) != 0) //创建线程，如果失败。 创建线程函数原型为int pthread_create(pthread_t *thread,const pthread_attr_t *attr,void *(*start_routine) (void *),void *arg);thread：传递一个 pthread_t 类型的指针变量，也可以直接传递某个 pthread_t 类型变量的地址。pthread_t 是一种用于表示线程的数据类型，每一个 pthread_t 类型的变量都可以表示一个线程，这里传入的是描述线程池数组的第i项。attr：用于手动设置新建线程的属性，此处赋值为 NULL，pthread_create() 函数会采用系统默认的属性值创建线程。(*start_routine) (void *)以函数指针的方式指明新建线程需要执行的函数，该函数的参数最多有 1 个（可以省略不写），形参和返回值的类型都必须为 void* 类型，这里指worker函数。arg：指定传递给 start_routine 函数的实参，当不需要传递任何数据时，将 arg 赋值为 NULL 即可，这里将该类传递过去。如果成功创建线程，pthread_create() 函数返回数字 0，反之返回非零值。这里用于进行判断，如果返回值不为0则是如果创建线程失败。


		{
			delete[] m_threads; //释放描述线程池的数组所占有的内存
			throw std::exception(); //抛出异常
		}
		if(pthread_detach(m_threads[i])) //进行线程分离，指定该状态，线程主动与主控线程断开关系。线程结束后（不会产生僵尸线程），其退出状态不由其他线程获取，而直接自己自动释放（自己清理掉PCB的残留资源）。函数原型为int pthread_detach(pthread_t tid);参数tid为线程标识符。pthread_detach() 在调用成功完成之后返回零。
		{
			delete[] m_threads; //释放描述线程池的数组所占有的内存
			throw std::exception(); //抛出异常
		}
	}
}

//析构函数
template <typename T>
threadpool<T>::~threadpool()
{
	delete[] m_threads; //释放描述线程池的数组所占有的内存
	m_stop = true; //结束线程设为真
}

//添加元素
template <typename T>
bool threadpool<T>::append(T *request)
{
	m_queuelocker.lock();//互斥锁上锁
	if(m_workqueue.size() > m_max_requests) //如果请求队列大小大于最大请求数
	{
		m_queuelocker.unlock(); //互斥锁解锁
		return false; //返回false
	}
	m_workqueue.push_back(request); //将request放入请求队列
	m_queuelocker.unlock(); //互斥锁解锁
	m_queuestat.post(); //是否有任务需要处理信号量+1
	return true; //返回成功
}

//工作线程运行的函数，从工作队列中取出任务并执行
template <typename T>
void *threadpool<T>::worker(void *arg)
{
	threadpool *pool = (threadpool *)arg; //强制类型转换
	pool->run(); //调用run函数
	return pool; //返回
}

//执行
template <typename T>
void threadpool<T>::run()
{
	while(!m_stop) //只要不结束线程
	{
		m_queuestat.wait(); //是否有任务需要处理信号量阻塞-1
		m_queuelocker.lock(); //互斥锁上锁
		if(m_workqueue.empty()) //如果请求队列为空
		{
			m_queuelocker.unlock(); //互斥锁解锁
			continue; //跳过下面步骤继续循环
		}
		T *request = m_workqueue.front(); //获取请求队列头
		m_workqueue.pop_front(); //请求队列pop
		m_queuelocker.unlock(); //互斥锁解锁
		if(!request) //如果request为空
			continue; //跳过下面步骤继续循环
		connectionRAII mysqlcon(&request->mysql, m_connPool); //构造一个connectionRAII类，将MYSQL类与连接池类封装

		request->process(); //调用process函数
	}
}
#endif
