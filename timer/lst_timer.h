#ifndef LST_TIMER
#define LST_TIMER

#include <time.h>
#include "../log/log.h"

//需要提前声明
class util_timer;
//客户端数据
struct client_data
{
	sockaddr_in address; //网络环境下套接字的地址形式
	int sockfd; //socket连接的文件句柄
	util_timer *timer; //双向链表的形式的定时器类
};

//双向链表的形式的定时器类
class util_timer
{
public:
//构造函数，将前向定时器和后向定时器初始化为空
	util_timer() : prev(NULL), next(NULL){}

public:
	time_t expire; //超时时间
	void (*cb_func)(client_data *); //回调函数
	client_data *user_data; //客户端数据
	util_timer *prev; //前向定时器
	util_timer *next; //后向定时器
};

//基于升序链表的定时器
class sort_timer_lst
{
public:
//构造函数，将前向定时器和后向定时器初始化为空
	sort_timer_lst() : head(NULL), tail(NULL) {}
//析构函数
	~sort_timer_lst()
	{
		util_timer *tmp =head; //构造一个定时器指向链表头
		while(tmp) //当链表不为空
		{
			head = tmp->next; //头指针指向后向定时器
			delete tmp; //释放tmp
			tmp = head; //再次指向链表头
		}
	}
//添加定时器
	void add_timer(util_timer *timer)
	{
		if(!timer) //如果定时器指针为空
		{
			return; //返回
		}
		if(!head) //如果升序链表的定时器类没有链表头
		{
			head = tail = timer; //将链表头和链表尾指向添加的定时器
			return; //返回
		}
		if(timer->expire < head->expire) //如果添加的定时器的超时时间少于链表头的定时器的超时时间，则将该定时器添加到链表头
		{
			timer->next = head; //添加的定时器的后向定时器指向目前的链表头
			head->prev = timer; //链表头的定时器的前向定时器指向添加的定时器
			head = timer; //将链表头指向添加的定时器
			return; //返回
		}
		add_timer(timer, head); //调用void add_timer(util_timer *timer, util_timer *lst_head)函数，从链表头开始遍历添加定时器
	}
 //调整定时器
	void adjust_timer(util_timer *timer)
	{
		if(!timer) //如果定时器指针为空
		{
			return; //返回
		}
		util_timer *tmp = timer->next; //新建一个tmp定时器指针指向给定定时器的后向定时器
		if(!tmp || (timer->expire < tmp->expire)) //如果不存在后向定时器或者后向定时器的超时时间大于当前定时器超时时间
		{
			return; //返回
		}
		if(timer == head) //如果当前定时器为链表头的定时器
		{
			head = head->next; //链表头指向其后向定时器
			head->prev = NULL; //将链表头前向定时器设为空
      timer->next = NULL; //将当前定时器（之前的链表头的定时器）的后向定时器设为空
			add_timer(timer, head); //调用void add_timer(util_timer *timer, util_timer *lst_head)函数，从链表头开始遍历添加定时器
		}
		else
		{
			timer->prev->next = timer->next; //将当前定时器的前向定时器的后向定时器设为当前定时器的后向定时器
			timer->next->prev = timer->prev; //将当前定时器的后向定时器的前向定时器设为当前定时器的前向定时器
      //上面两行相当于将当前定时器从整个链表中删除掉
			add_timer(timer, timer->next); //调用void add_timer(util_timer *timer, util_timer *lst_head)函数，从当前定时器后向定时开始遍历添加定时器，因为之前已经判断过其后向定时器的超时时间应该小于当前定时器超时时间，从当前定时器后向定时开始遍历就可以
		}
	}
 //删除定时器
	void del_timer(util_timer *timer)
	{
		if(!timer) //如果定时器指针为空
		{
			return; //返回
		}
		if((timer == head) && (timer == tail)) //如果需要删除的定时器为链表头并且是链表尾，那么删除该定时器之后链表为空
		{
			delete timer; //释放该定时器的内存
			head = NULL; //链表头设为空
			tail = NULL; //链表尾设为空
			return; //返回
		}
		if(timer == head) //如果需要删除的定时器为链表头
		{
			head = head->next; //将链表头指向链表头定时器的后向定时器
			head->prev = NULL; //将链表头前向定时器设为空
			delete timer; //释放该定时器的内存
			return; //返回
		}
		if(timer == tail) //如果需要删除的定时器为链表尾
		{
			tail = tail->prev; //将链表尾指向链表尾定时器的前向定时器
			tail->next = NULL; //将链表头的后向定时器设为空
			delete timer; //释放该定时器的内存
			return; //返回
		}
		timer->prev->next = timer->next; //将当前定时器的前向定时器的后向定时器设为当前定时器的后向定时器
		timer->next->prev = timer->prev; //将当前定时器的后向定时器的前向定时器设为当前定时器的前向定时器
		delete timer; //返回
	}
 //定时任务处理
	void tick()
	{
		if(!head) //如果不存在链表头（链表为空）
		{
			return; //返回
		}
		//printf("timer tick\n");
		LOG_INFO("%s", "timer tick"); //计入日志
		Log::get_instance()->flush(); //刷入日志
		time_t cur = time(NULL); //获取当前时间
		util_timer *tmp = head; //构造一个定时器指向链表头
		while(tmp) //该定时器不为空（链表不为空）
		{
			if(cur < tmp->expire) //如果当前时间少于定时器的超时时间
			{
				break; //离开循环
			}
			tmp->cb_func(tmp->user_data); //将客户端数据传入回调函数并调用
			head = tmp->next; //将链表头指向后向定时器
			if(head) //如果链表头存在（如果链表不为空）
			{
				head->prev = NULL; //链表头前向定时器设为空
			}
			delete tmp; //释放当前定时器指向空间（原有链表头空间）
			tmp = head; //将临时定时器指向链表头
		}
	}
private:
//私有成员函数，被add_timer和adjust_timer调用，添加链表节点
	void add_timer(util_timer *timer, util_timer *lst_head)
	{
		util_timer *prev = lst_head;  //构造一个prev定时器指向开始遍历链表的头
		util_timer *tmp = prev->next; //构造一个tmp定时器指向开始遍历链表的头的后向定时器
		while(tmp) //tmp定时器不为空
		{
			if(timer->expire < tmp->expire) //如果tmp定时器超时时间大于需要添加的定时器的超时时间
			{
				prev->next = timer; //prev定时器后向定时器设为添加的定时器
				timer->next = tmp; //添加的定时器后向定时器设为tmp定时器
				tmp->prev = timer; //tmp定时器前向定时器设为添加的定时器
				timer->prev = prev; //添加的定时器的前向定时器设为prev定时器
        //以上四行就是将添加的定时器添加到prev和tmp定时器之间，以达到升序链表的效果
				break; //离开循环
			}
			prev = tmp; //prev定时器指向tmp定时器（其后向定时器）
			tmp = tmp->next; //tmp定时器指向其后向定时器
		}
		if(!tmp) //如果tmp定时器为空，那么链表尾的超时时间也小于当前要添加的定时器
		{
			prev->next = timer; //prev定时器的后向定时器设为添加的定时器
			timer->prev = prev; //添加的定时器的前向定时器设为prev定时器
			timer->next = NULL; //添加的定时器后向定时器设为空
			tail = timer; //将链表尾指向添加的定时器
		}
	}

private:
	util_timer *head; //链表头
	util_timer *tail; //链表尾
};

#endif
		

