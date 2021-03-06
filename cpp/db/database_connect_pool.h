//
// Created by hujianzhe on 17-6-30.
//

#ifndef	UTIL_CPP_DB_DATABASE_CONNECT_POOL_H
#define	UTIL_CPP_DB_DATABASE_CONNECT_POOL_H

#include "../../c/syslib/ipc.h"
#include "../../c/component/db.h"
#include <string>
#include <unordered_set>

namespace Util {
class DatabaseConnectPool {
public:
	DatabaseConnectPool(int db_type, const char* ip, unsigned short port, const char* user, const char* pwd, const char* database);
	~DatabaseConnectPool(void);

	void setConnectionAttribute(int timeout_sec, short max_connect_num);

	DB_HANDLE* getConnection(void);
	void pushConnection(DB_HANDLE* dbhandle);
	void cleanConnection(void);

private:
	DB_HANDLE* connect(void);

private:
	int m_connectTimeout;
	int m_connectNum;
	int m_connectMaxNum;

	const int m_dbType;
	const unsigned short m_port;
	const std::string m_ip;
	const std::string m_user;
	const std::string m_pwd;
	const std::string m_database;

	CSLock_t m_lock;
	std::unordered_set<DB_HANDLE*> m_dbhandles;
};
}

#endif
