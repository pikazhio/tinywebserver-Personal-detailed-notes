/*
循环数组实现的阻塞列表，m_block = (m_back + 1) % m_max_size;
线程安全，每个操作前都要先加互斥锁，操作完后，在解锁
*/

#ifndef BLOCK_QUEUE_H
#define BLOCK_QUEUE_H

#include <iostream>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>
#include "../lock/locker.h"

using namespace std;

//模板
template<class T>
class block_queue
{
public:

//构造函数，默认max_size为1000
	block_queue(int max_size = 1000)
	{
		if(max_size <= 0) //判断值是否合理，不合理退出
		{
			exit(-1);
		}

		m_max_size = max_size; //将max_size赋值给m_max_size
		m_array = new T[max_size]; //新建一个max_size个T大小的数组
		m_size = 0; //初始化m_size为0
		m_front = -1; //初始化m_front为-1
		m_back = -1; //初始化m_back为-1
	}


	void clear()
	{
		m_mutex.lock(); //互斥锁锁定
		m_size = 0; //m_size设为0
		m_front = -1; //m_front设为-1
		m_back = -1; //m_back设为-1
		m_mutex.unlock(); //互斥锁解锁
	}

//析构函数
	~block_queue()
	{
		m_mutex.lock(); //互斥锁锁定
		if(m_array != NULL) //判断数组是否为空
			delete[] m_array; //释放数组
		m_mutex.unlock(); //互斥锁解锁
	}

//判断队列是否满了
	bool full()
	{
		m_mutex.lock(); //互斥锁锁定
		if(m_size >= m_max_size) //大于等于时说明满了
		{
			m_mutex.unlock(); //互斥锁解锁
			return true; //返回true
		}
		m_mutex.unlock(); //互斥锁解锁
		return false;//返回false
	}
 
//判断队列是否为空
	bool empty()
	{
		m_mutex.lock(); //互斥锁锁定
		if(m_size == 0) //如果m_size大小为0说明为空
		{
			m_mutex.unlock(); //互斥锁解锁
			return true; //返回true
		}
		m_mutex.unlock(); //互斥锁解锁
		return false; //返回false
	}

//返回队首元素
	bool front(T &value)
	{
		m_mutex.lock(); //互斥锁锁定
		if(m_size == 0) //如果m_size大小为0，说明空的
		{
			m_mutex.unlock(); //互斥锁解锁
			return false; //返回false
		}
		value = m_array[m_front]; //将队首值赋值为value
		m_mutex.unlock(); //互斥锁解锁
		return true; //返回true
	}
 
 //返回队尾元素
	bool back(T &value)
	{
		m_mutex.lock(); //互斥锁锁定
		if(m_size == 0) //如果m_size大小为0，说明空的
		{
			m_mutex.unlock(); //互斥锁解锁
			return false; //返回false
		}
		value = m_array[m_back];  //将队尾值赋值为value
		m_mutex.unlock(); //互斥锁解锁
		return true; //返回true
	}

//返回队列数据个数
	int size()
	{
		int tmp = 0; 
		m_mutex.lock(); //互斥锁锁定
		tmp = m_size; //将m_size赋值给待返回值

		m_mutex.unlock(); //互斥锁解锁
		return tmp; //返回数据
	}

//返回队列最多存多少数据
	int max_size()
	{
		int tmp = 0;
		m_mutex.lock(); //互斥锁锁定
		tmp = m_max_size; //将m_max_size赋值给待返回值
		m_mutex.unlock(); //互斥锁解锁
		return tmp; //返回数据
	}

//往队列添加元素，需要将所有使用队列的线程先唤醒
//当有元素push进队列，相当于生产者生产了一个元素
//若当前没有线程等待条件变量，则唤醒无意义
	bool push(const T &item)
	{
		m_mutex.lock(); //互斥锁锁定
		if(m_size >= m_max_size) //队列大小超过最大大小
		{
			m_cond.broadcast(); //唤醒所有被 pthread_cond_wait 函数阻塞在条件变量上的线程
			m_mutex.unlock(); //互斥锁解锁
			return false; //返回false
		}
		m_back = (m_back + 1) % m_max_size; //更新m_back值，因使用循环数组实现，所以采用这种公式
		m_array[m_back] = item; //将item赋值给m_array[m_back]

		m_size++; //目前数量加一

		m_cond.broadcast(); //唤醒所有被 pthread_cond_wait 函数阻塞在条件变量上的线程
		m_mutex.unlock(); //互斥锁解锁
		return true; //返回true
	}

//pop时，如果当前队列没有元素，将会等待条件变量
	bool pop(T &item)
	{
		m_mutex.lock(); //互斥锁锁定
		while(m_size <= 0) //m_size<=0时，当前队列没有元素，一直循环等待
		{
			if(!m_cond.wait(m_mutex.get())) //如果条件变量等待失败
			{
				m_mutex.unlock(); //互斥锁解锁
				return false; //返回flase
			}
		}

		m_front = (m_front + 1) % m_max_size; //更新m_front值，因使用循环数组实现，所以采用这种公式
		item = m_array[m_front]; //将m_array[m_front]赋值给item
		m_size--; //目前数量减一
		m_mutex.unlock(); //互斥锁解锁
		return true; //返回true
	}

//比上面那个pop增加了超时处理
	bool pop(T &item, int ms_timeout)
	{
		struct timespec t = {0 , 0};
		struct timeval now = {0, 0};
		gettimeofday(&now, NULL); //获取当前精准时间
		m_mutex.lock(); //互斥锁锁定
		if(m_size <= 0) //如果当前队列没有元素
		{
			t.tv_sec = now.tv_sec + ms_timeout / 1000; 
			t.tv_sec = (ms_timeout % 1000) * 1000; //修改超时等待的时间
			if(!m_cond.timewait(m_mutex.get(), t)) //如果条件变量超时等待失败
			{
				m_mutex.unlock(); //互斥锁解锁
				return false; //返回false
			}
		}

		if(m_size <= 0)  //如果当前队列没有元素
		{
			m_mutex.unlock(); //互斥锁解锁
			return false; //返回false
		}

		m_front = (m_front + 1) % m_max_size; //更新m_front值，因使用循环数组实现，所以采用这种公式
		item = m_array[m_front]; //将m_array[m_front]赋值给item
		m_size--; //目前数量减一
		m_mutex.unlock(); //互斥锁解锁
		return true; //返回true
	}

private:
	locker m_mutex; //互斥锁
	cond m_cond; //条件变量
 
	T *m_array; //数组
	int m_size; //存在数量
	int m_max_size; //最大数量
	int m_front; //指向队列头
	int m_back; //指向队列尾
};

#endif


