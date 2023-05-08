#include <mysql/mysql.h>
#include <stdio.h>
#include <string>
#include <string.h>
#include <stdlib.h>
#include <list>
#include <pthread.h>
#include <iostream>
#include "sql_connection_pool.h"

using namespace std;

//构造函数
connection_pool::connection_pool()
{
	this->CurConn = 0; //将类当前已经使用的连接数初始化为0
	this->FreeConn = 0; //将类当前空闲的连接数初始化为0
}

//采用static静态变量，保证全局唯一，单例模式
connection_pool *connection_pool::GetInstance()
{
	static connection_pool connPool;
	return &connPool;
}

//构造初始化连接池
void connection_pool::init(string url, string User, string PassWord, string DBName, int Port, unsigned int MaxConn)
{
	this->url = url; //赋值连接池的主机地址
	this->Port = Port; //赋值连接池的数据库端口号
	this->User = User; //赋值连接池的登录数据库的用户名
	this->PassWord = PassWord; //赋值连接池的登录数据库的密码
	this->DatabaseName = DBName; //赋值连接池的数据库名

	lock.lock(); //锁定
	for(int i = 0; i < MaxConn; ++i) //循环执行MaxConn次
	{
		MYSQL *con = NULL; 
		con = mysql_init(con); //获取一个MYSQL结构

		if(con == NULL) //获取失败
		{
			cout << "Error:" << mysql_error(con);  //输出错误信息
			exit(1); //退出
		}
		con = mysql_real_connect(con, url.c_str(), User.c_str(), PassWord.c_str(), DBName.c_str(), Port, NULL, 0); //连接到MYSQL服务器

		if(con == NULL) //连接失败
		{
			cout << "Error:" << mysql_error(con); //输出错误信息
			exit(1); //退出
		}
		connList.push_back(con); //将MYSQL放入连接池
		++FreeConn; //空闲的连接数 +1
	}

	reserve = sem(FreeConn); //信号量初始化为FreeConn

	this->MaxConn = FreeConn; //最大连接数复制为FreeConn

	lock.unlock(); //解锁
}

//获取连接
MYSQL *connection_pool::GetConnection()
{
	MYSQL *con = NULL; //创建一个MYSQL结构体

	if(connList.size() == 0) //如果连接池为空返回
		return NULL;

	reserve.wait(); //等待信号量减一

	lock.lock(); //互斥锁锁定

	con = connList.front(); //将连接池第一个赋值给MYSQL
	connList.pop_front(); //将连接池第一个pop出

	--FreeConn; //空闲连接数减一
	++CurConn; //目前连接数加一

	lock.unlock(); //互斥锁解锁
	return con; //将获得的MYSQL返回
}

//释放连接
bool connection_pool::ReleaseConnection(MYSQL *con)
{
	if(con == NULL) //给定的MYSQL连接为空时返回false
		return false;
	lock.lock(); //互斥锁加锁

	connList.push_back(con); //将连接push到连接池中
	++FreeConn; //空闲连接数加一
	--CurConn;//目前连接数减一

	lock.unlock(); //互斥锁解锁

	reserve.post(); //信号量加一
	return true; //返回true
}

//销毁连接池
void connection_pool::DestroyPool()
{
	lock.lock(); //互斥锁锁定
	if(connList.size() > 0) //判断连接池不为空
	{
		list<MYSQL *>::iterator it; //建立一个iterator
		for(it = connList.begin(); it != connList.end(); ++it) //对连接池内所有数据
		{
			MYSQL *con = *it;
			mysql_close(con); //关闭MYSQL服务器连接
		}
		CurConn = 0; //将目前连接数置为0
		FreeConn = 0; //将空闲连接数置为0
		connList.clear(); //销毁连接池的list
		
		lock.unlock(); //互斥锁解锁
	}

	lock.unlock(); //互斥锁解锁
}

//获取连接池空闲连接数
int connection_pool::GetFreeConn()
{
	return this->FreeConn; //返回连接池空闲连接数
}

//connection_pool析构函数
connection_pool::~connection_pool()
{
	DestroyPool(); //调用销毁连接池函数
}

//connectionRAII构造函数
connectionRAII::connectionRAII(MYSQL **SQL, connection_pool *connPool){
	*SQL = connPool->GetConnection();

	conRAII = *SQL;
	poolRAII = connPool;
}

//connectionRAII析构函数
connectionRAII::~connectionRAII(){
	poolRAII->ReleaseConnection(conRAII);
}

