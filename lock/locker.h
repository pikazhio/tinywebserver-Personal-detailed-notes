#ifndef LOCKER_H
#define LOCKER_H
 
#include <exception>    //异常处理的头文件
#include <pthread.h>	  //线程相关的头文件
#include <semaphore.h>  //信号量的头文件	
//信号封装类
class sem
{
public:
//构造函数
	sem()
	{
		if(sem_init(&m_sem, 0, 0) != 0) //初始化信号量。函数原型为int sem_init(sem_t *sem, int pshared, unsigned int value)，其中sem是要初始化的信号量，pshared表示此信号量是在进程间共享还是线程间共享，pshared为0表示只能为在当前进程的所有线程共享，value是信号量的初始值。成功返回0，错误返回-1。
		{
			throw std::exception();	//信号初始化失败。抛出异常
		}
	}
 //构造函数
	sem(int num)
	{
		if(sem_init(&m_sem, 0, num) != 0) //初始化信号量。这次将信号量初始值设为num
  		{
    			throw std::exception();	//信号初始化失败。抛出异常。
	  	}
	}
 //析构函数
	~sem()
	{
   		sem_destroy(&m_sem); //释放信号量。函数原型为：int sem_destroy(sem_t *sem)，其中sem是要销毁的信号量，只有用sem_init初始化的信号量才能用sem_destroy销毁。
	}
	bool wait()
	{
		return sem_wait(&m_sem) == 0; //等待信号量。函数原型为：int sem_wait(sem_t *sem)，其中sem是要等待的信号量。这是一个原子操作，如果信号量的值大于0,将信号量的值减1，立即返回，当sem值为0时，线程阻塞，等待sem值不为0，解除阻塞，sem值减1。成功返回0,失败返回-1。
	}
	bool post()
	{
		return sem_post(&m_sem) == 0; //释放信号量。函数原型为：int sem_post(sem_t *sem)，其中sem是要释放的信号量。让信号量的值加1，有线程阻塞在这个信号量上时，调用这个函数会使其中的一个线程不在阻塞。成功返回0,失败返回-1。
	}

private:
	sem_t m_sem; //声明信号量。信号量的数据类型为结构sem_t，它本质上是一个长整型的数
};
 //互斥锁封装类
class locker
{
public:
 //构造函数
	locker()
	{
		if(pthread_mutex_init(&m_mutex, NULL) != 0) //初始化互斥锁。函数原型为：int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t * attr)，其中mutex为要初始化的互斥锁，attr为锁属性，NULL值为默认属性。
		{
			throw std::exception(); //互斥锁初始化失败。抛出异常。
		}
	}
  //析构函数
	~locker()
	{
		pthread_mutex_destroy(&m_mutex); //释放互斥锁。函数原型为：int pthread_mutex_destroy(pthread_mutex_t *mutex)，其中mutex是要销毁的互斥锁。
	}
	bool lock()
	{
		return pthread_mutex_lock(&m_mutex) == 0; //对互斥锁加锁。函数原型为：int pthread_mutex_lock(pthread_mutex_t *mutex)，其中mutex是要加锁的互斥锁。
	}
	bool unlock()
	{
		return pthread_mutex_unlock(&m_mutex) == 0; //对互斥锁解锁。函数原型为：int pthread_mutex_unlock(pthread_mutex_t *mutex)，其中mutex是要解锁的互斥锁。
	}
 //获取互斥锁
	pthread_mutex_t *get()
	{
		return &m_mutex;
	}

private:
	pthread_mutex_t m_mutex; //声明互斥锁。互斥锁的数据结构为结构pthread_mutex_t。
};
//条件变量封装类
class cond
{
public:
//构造函数
	cond()
	{
		if(pthread_cond_init(&m_cond, NULL) != 0) //初始化条件变量。函数原型为：int pthread_cond_init(pthread_cond_t* restrict cond,const pthread_condattr_t* restrict attr)，其中cond为要初始化的条件变量，attr为初始化时条件变量的属性，NULL值为默认属性。
		{
      //pthread_mutex_destroy(&m_mutex);
			throw std::exception(); //条件变量初始化失败。抛出异常。
		}
	}
 //析构函数
	~cond()
	{
		pthread_cond_destroy(&m_cond); //释放条件变量。函数原型为：int pthread_cond_destroy(pthread_cond_t* cond)，其中cond为要销毁的条件变量。此函数只是反初始化互斥量，并没有释放内存空间。
	}
	bool wait(pthread_mutex_t *m_mutex)
	{
		int ret = 0; //新建一个变量，之后将pthread_cond_wait的返回值存入该变量。
   //pthread_mutex_lock(&m_mutex);//加锁
		ret = pthread_cond_wait(&m_cond, m_mutex); //无条件等待条件变量。函数原型为：int pthread_cond_wait(pthread_cond_t* cond cond,pthread_mutex_t* mutex)，其中cond为条件变量，mutex为一个全局互斥锁，防止多个线程同时请求。pthread_cond_wait会先解除之前的pthread_mutex_lock锁定的mutex，然后阻塞在等待对列里休眠，直到再次被唤醒，可以被pthread_cond_signal()或者是pthread_cond_broadcast()函数唤醒。不同之处在于，pthread_cond_signal()可以唤醒至少一个线程；而pthread_cond_broadcast()则是唤醒等待该条件满足的所有线程。在使用的时候需要注意，一定是在改变了条件状态以后再给线程发信号。被唤醒后，该进程会先锁定mutex，再读取资源。
   //pthread_mutex_unlock(&m_mutex);//解锁
		return ret == 0; //返回刚刚建的变量值是否为0，以判断pthread_cond_wait是否调用成功。
 	}
 	bool timewait(pthread_mutex_t *m_mutex, struct timespec t)
	{
		int ret = 0; //新建一个变量，之后将pthread_cond_timedwait的返回值存入该变量。
   //pthread_mutex_lock(&m_mutex);//加锁
		ret = pthread_cond_timedwait(&m_cond, m_mutex, &t); //计时等待条件变量。函数原型为：int pthread_cond_timedwait(pthread_cond_t* cond,pthread_mutex_t* mutex,const struct timespec* abstime)，和pthread_cond_wait比多了个abstime，如果阻塞时间超过abstime，超时解除阻塞，此函数将重新锁定mutex，然后返回错误ETIMEOUT。
   //pthread_mutex_unlock(&m_mutex);//解锁
		return ret == 0; //返回刚刚建的变量值是否为0，以判断pthread_cond_timedwait是否调用成功。
	}
	bool signal()
	{
		return pthread_cond_signal(&m_cond) == 0; //释放被阻塞在条件变量上的一个线程。函数原型为：int pthread_cond_signal(pthread_cond_t *cond)，cond为条件变量。
	}
	bool broadcast()
	{
		return pthread_cond_broadcast(&m_cond) == 0; //唤醒所有被 pthread_cond_wait 函数阻塞在条件变量上的线程。函数原型为：int pthread_cond_broadcast(pthread_cond_t *cond)，cond为条件变量。
	}

private:
  //static pthread_mutex_t m_mutex; //声明一个全局互斥锁，防止多个线程同时请求pthread_cond_wait()
	pthread_cond_t m_cond; //声明一个条件变量。条件变量的结构类型为结构pthread_cond_t。
};
#endif
		



