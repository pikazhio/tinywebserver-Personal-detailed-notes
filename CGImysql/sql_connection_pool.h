#ifndef _CONNECTION_POOL_
#define _CONNECTION_POOL_

#include <stdio.h>
#include <list>
#include <mysql/mysql.h>
#include <error.h>
#include <string.h>
#include <iostream>
#include <string>
#include "../lock/locker.h"

using namespace std;

//连接池类
class connection_pool
{
public:
	MYSQL *GetConnection(); //获取数据库连接
  bool ReleaseConnection(MYSQL *conn); //释放连接
	int GetFreeConn(); //获取连接
	void DestroyPool(); //销毁所有链接

  //单例模式
	static connection_pool *GetInstance();

	void init(string url, string User, string PassWord, string DataBaseName, int Port, unsigned int MaxConn); //初始化

	connection_pool(); //构造函数
	~connection_pool(); //析构函数

private:
	unsigned int MaxConn; //最大连接数
	unsigned int CurConn; //当前已经使用的连接数
	unsigned int FreeConn; //当前空闲的连接数

	locker lock; //互斥锁
	list<MYSQL *> connList;  //连接池
	sem reserve; //信号量

	string url; //主机地址
	string Port; //数据库端口号
	string User; //登录数据库用户名
	string PassWord; //登录数据库密码
	string DatabaseName; //使用数据库名
};

//RAII中文翻译为资源获取即初始化，是一种对资源申请、释放这种成对的操作的封装，这里将MYSQL类与连接池类封装
class connectionRAII{
public:
	connectionRAII(MYSQL **con, connection_pool * connPool); //构造函数
	~connectionRAII(); //析构函数

private:
	MYSQL *conRAII; //MYSQL类
	connection_pool *poolRAII; //连接池类
};

#endif
		
	
		
