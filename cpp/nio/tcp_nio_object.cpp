//
// Created by hujianzhe on 16-9-7.
//

#include "../../c/syslib/error.h"
#include "tcp_nio_object.h"
#include <string.h>
#include <exception>
#include <stdexcept>

namespace Util {
TcpNioObject::TcpNioObject(FD_t fd) :
	NioObject(fd, SOCK_STREAM),
	m_connecting(false),
	m_outbufMutexInitOk(false),
	m_writeCommit(false),
	m_outbufhead(NULL),
	m_outbuftail(NULL)
{
	if (mutex_Create(&m_outbufMutex) != EXEC_SUCCESS) {
		throw std::logic_error("Util::TcpNioObject mutex_Create failure");
	}
	m_outbufMutexInitOk = true;
}
TcpNioObject::~TcpNioObject(void) {
	if (m_outbufMutexInitOk) {
		mutex_Close(&m_outbufMutex);
	}
}

bool TcpNioObject::reactorConnect(int family, const char* ip, unsigned short port, const std::function<bool(TcpNioObject*, bool)>& cb) {
	struct sockaddr_storage saddr;
	if (sock_Text2Addr(&saddr, family, ip, port) != EXEC_SUCCESS) {
		return false;
	}
	m_connecting = true;
	m_connectCallback = cb;
	if (reactor_Commit(m_reactor, m_fd, REACTOR_CONNECT, &m_writeOl, &saddr) == EXEC_SUCCESS) {
		return true;
	}
	m_connecting = false;
	m_valid = false;
	return false;
}
bool TcpNioObject::reactorConnect(struct sockaddr_storage* saddr, const std::function<bool(TcpNioObject*, bool)>& cb) {
	m_connecting = true;
	m_connectCallback = cb;
	if (reactor_Commit(m_reactor, m_fd, REACTOR_CONNECT, &m_writeOl, saddr) == EXEC_SUCCESS) {
		return true;
	}
	m_connecting = false;
	m_valid = false;
	return false;
}
bool TcpNioObject::onConnect(void) {
	m_connecting = false;
	bool ok = false;
	try {
		if (m_connectCallback) {
			ok = m_connectCallback(this, reactor_ConnectCheckSuccess(m_fd));
			m_connectCallback = nullptr;//std::function<bool(NioObject*, bool)>();
		}
		else {
			ok = onConnect(reactor_ConnectCheckSuccess(m_fd));
		}
	}
	catch (...) {}
	return ok;
}
bool TcpNioObject::onConnect(bool success) { return success; }

int TcpNioObject::inbufRead(unsigned int nbytes, struct sockaddr_storage* saddr) {
	size_t offset = m_inbuf.size();
	m_inbuf.resize(offset + nbytes);
	int res = sock_Recv(m_fd, &m_inbuf[offset], nbytes, 0, saddr);
	if (res > 0) {
		m_inbuf.resize(offset + res);
	}
	return res;
}
void TcpNioObject::inbufRemove(unsigned int nbytes) {
	if (m_inbuf.size() <= nbytes) {
		std::vector<unsigned char>().swap(m_inbuf);
	}
	else {
		m_inbuf.erase(m_inbuf.begin(), m_inbuf.begin() + nbytes);
	}
}
IoBuf_t TcpNioObject::inbuf(void) {
	IoBuf_t __buf;
	iobuffer_len(&__buf) = m_inbuf.size();
	iobuffer_buf(&__buf) = m_inbuf.empty() ? NULL : (char*)&m_inbuf[0];
	return __buf;
}
int TcpNioObject::recv(void) {
	struct sockaddr_storage saddr;
	int res = sock_TcpReadableBytes(m_fd);
	do {
		if (res <= 0) {
			m_valid = false;
			break;
		}
		res = inbufRead(res, &saddr);
		if (res <= 0) {
			m_valid = false;
			break;
		}
		try {
			inbufRemove(onRead(inbuf(), &saddr, res));
		}
		catch (...) {
			m_valid = false;
			break;
		}
	} while (0);
	return res;
}

bool TcpNioObject::sendv(IoBuf_t* iov, unsigned int iovcnt, struct sockaddr_storage* saddr) {
	if (!m_valid) {
		return false;
	}
	if (!iov || !iovcnt) {
		return true;
	}
	size_t nbytes = 0;
	for (unsigned int i = 0; i < iovcnt; ++i) {
		nbytes += iobuffer_len(iov + i);
	}
	if (0 == nbytes) {
		return true;
	}

	mutex_Lock(&m_outbufMutex);

	do {
		int res = 0;
		if (!m_outbuftail) {
			res = sock_SendVec(m_fd, iov, iovcnt, 0, saddr);
			if (res < 0) {
				if (error_code() != EWOULDBLOCK) {
					m_valid = false;
					break;
				}
				res = 0;
			}
		}
		if (res < nbytes) {
			WaitSendData* wsd = (WaitSendData*)malloc(sizeof(WaitSendData) + (nbytes - res));
			if (!wsd) {
				m_valid = false;
				break;
			}
			wsd->len = nbytes - res;
			wsd->offset = 0;

			unsigned int i, off;
			for (off = 0, i = 0; i < iovcnt; ++i) {
				if (res >= iobuffer_len(iov + i)) {
					res -= iobuffer_len(iov + i);
				}
				else {
					memcpy(wsd->data + off, ((char*)iobuffer_buf(iov + i)) + res, iobuffer_len(iov + i) - res);
					off += iobuffer_len(iov + i) - res;
					res = 0;
				}
			}

			if (m_outbuftail) {
				list_node_insert_back(m_outbuftail, &wsd->m_listnode);
				m_outbuftail = &wsd->m_listnode;
			}
			else {
				list_node_init(&wsd->m_listnode);
				m_outbufhead = m_outbuftail = &wsd->m_listnode;
			}
			//
			if (!m_writeCommit) {
				m_writeCommit = true;
				reactorWrite();
			}
		}
	} while (0);

	mutex_Unlock(&m_outbufMutex);

	return m_valid;
}
int TcpNioObject::onWrite(void) {
	int count = 0;
	if (m_connecting) {
		if (!onConnect()) {
			m_valid = false;
			return 0;
		}
	}
	if (!m_valid) {
		return 0;
	}

	mutex_Lock(&m_outbufMutex);

	for (list_node_t* iter = m_outbufhead; iter; ) {
		WaitSendData* wsd = field_container(iter, WaitSendData, m_listnode);
		int res = sock_Send(m_fd, wsd->data + wsd->offset, wsd->len - wsd->offset, 0, NULL);
		if (res < 0) {
			if (error_code() != EWOULDBLOCK) {
				m_valid = false;
				break;
			}
			res = 0;
		}
		count += res;
		wsd->offset += res;
		if (wsd->offset >= wsd->len) {
			list_node_remove(iter);
			if (iter == m_outbufhead) {
				m_outbufhead = iter->next;
			}
			if (iter == m_outbuftail) {
				m_outbuftail = iter->next;
			}
			iter = iter->next;
			free(wsd);
		}
		else if (m_valid) {
			reactorWrite();
			break;
		}
	}
	if (!m_outbuftail) {
		m_writeCommit = false;
	}

	mutex_Unlock(&m_outbufMutex);

	return count;
}
}
