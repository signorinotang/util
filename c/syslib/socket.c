//
// Created by hujianzhe
//

#include "socket.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#if !defined(_WIN32) && !defined(_WIN64)
#include <signal.h>
#endif

#ifdef	__cplusplus
extern "C" {
#endif

static unsigned short check_sum(unsigned short* buffer, int iSize) {
	unsigned long ulCksum = 0;
	while (iSize > 1) {
		ulCksum += *buffer++;
		iSize -= sizeof(unsigned short);
	}
	if (iSize)
		ulCksum += *(unsigned char*)buffer;
	ulCksum = (ulCksum >> 16) + (ulCksum & 0xffff);
	ulCksum += (ulCksum >> 16);
	return (unsigned short)~ulCksum;
}

/* Network */
#if defined(_WIN32) || defined(_WIN64)
#define	__GetErrorCode()	GetLastError()
#define	__SetErrorCode(e)	SetLastError((e))
#define	SOCKET_ERROR_VALUE(CODE)	(WSA##CODE)
static PIP_ADAPTER_ADDRESSES __win32GetAdapterAddress(void) {
	ULONG Flags = GAA_FLAG_SKIP_FRIENDLY_NAME | GAA_FLAG_INCLUDE_GATEWAYS;
	ULONG bufsize = 0;
	PIP_ADAPTER_ADDRESSES adapterList = NULL;
	if (GetAdaptersAddresses(AF_UNSPEC, Flags, NULL, adapterList, &bufsize) == ERROR_BUFFER_OVERFLOW) {
		adapterList = (PIP_ADAPTER_ADDRESSES)malloc(bufsize);
		if (adapterList == NULL) {
			SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		}
		else if (GetAdaptersAddresses(AF_UNSPEC, Flags, NULL, adapterList, &bufsize) != ERROR_SUCCESS) {
			free(adapterList);
			adapterList = NULL;
		}
	}
	return adapterList;
}

if_nameindex_t* if_nameindex(void) {
	if_nameindex_t* nameindex = NULL;
	PIP_ADAPTER_ADDRESSES adapterList = NULL;
	do {
		if_nameindex_t* p;
		size_t memsize = sizeof(if_nameindex_t);
		PIP_ADAPTER_ADDRESSES adapter;
		adapterList = __win32GetAdapterAddress();
		if (!adapterList) {
			break;
		}
		for (adapter = adapterList; adapter; adapter = adapter->Next) {
			memsize += sizeof(if_nameindex_t);
		}
		nameindex = (if_nameindex_t*)calloc(1, memsize);
		if (!nameindex) {
			break;
		}
		for (p = nameindex, adapter = adapterList; adapter; adapter = adapter->Next, ++p) {
			if (if_indextoname(adapter->IfIndex, p->if_name)) {
				p->if_index = adapter->IfIndex;
			}
			else {
				break;
			}
		}
		if (adapter) {
			free(nameindex);
			nameindex = NULL;
		}
	} while (0);
	free(adapterList);
	return nameindex;
}

void if_freenameindex(if_nameindex_t* ptr) { free(ptr); }
#else
#include <sys/ioctl.h>
#include <sys/wait.h>
#define	__GetErrorCode()	(errno)
#define	__SetErrorCode(e)	((errno) = (e))
#define	SOCKET_ERROR_VALUE(CODE)	(CODE)
#define	SOCKET_ERROR	(-1)
#define	INVALID_SOCKET	(-1)
static void sig_free_zombie(int sig) {
	int status;
	while (waitpid(-1, &status, WNOHANG) > 0);
}
#ifdef __linux__
static unsigned long long __swap64(unsigned long long val) {
	if (__BYTE_ORDER == __LITTLE_ENDIAN) {
		int i;
		unsigned char* p = (unsigned char*)&val;
		for (i = 0; i < sizeof(val) / 2; ++i) {
			unsigned char temp = p[i];
			p[i] = p[sizeof(unsigned long long) - i - 1];
			p[sizeof(unsigned long long) - i - 1] = temp;
		}
	}
	return val;
}
unsigned long long htonll(unsigned long long val) { return __swap64(val); }
unsigned long long ntohll(unsigned long long val) { return __swap64(val); }
#endif
#endif

EXEC_RETURN network_SetupEnv(void) {
#if defined(_WIN32) || defined(_WIN64)
	WSADATA data;
	return WSAStartup(MAKEWORD(2, 2), &data) == 0 ? EXEC_SUCCESS : EXEC_ERROR;
#else
	struct sigaction act, oact;
/*
	struct rlimit rlim;
	rlim.rlim_cur = rlim.rlim_max = 65535;
	if (setrlimit(RLIMIT_NOFILE, &rlim)) {
		return EXEC_ERROR;
	}
*/
	if (signal(SIGPIPE, SIG_IGN) == SIG_ERR)
		return EXEC_ERROR;
	act.sa_handler = sig_free_zombie;
	sigemptyset(&act.sa_mask);
	act.sa_flags = SA_RESTART;
	return sigaction(SIGCHLD, &act, &oact) == 0 ? EXEC_SUCCESS : EXEC_ERROR;
#endif
}

EXEC_RETURN network_CleanEnv(void) {
#if defined(_WIN32) || defined(_WIN64)
	return (WSACleanup() != 0 && WSAGetLastError() != WSANOTINITIALISED) ? EXEC_ERROR : EXEC_SUCCESS;
#else
	void(*old0)(int) = signal(SIGPIPE, SIG_DFL);
	void(*old1)(int) = signal(SIGCHLD, SIG_DFL);
	return (old0 == SIG_ERR || old1 == SIG_ERR) ? EXEC_ERROR : EXEC_SUCCESS;
#endif
}

NETWORK_INTERFACE_INFO* network_InterfaceInfo(void) {
	int ok = 1;
	NETWORK_INTERFACE_INFO* info_head = NULL, *info;
#if defined(_WIN32) || defined(_WIN64)
	PIP_ADAPTER_ADDRESSES pAdapterEntry = __win32GetAdapterAddress(), p;
	for (p = pAdapterEntry; p && ok; p = p->Next) {
		struct address_list* addr;
		PIP_ADAPTER_UNICAST_ADDRESS unicast;
		if (IfOperStatusUp != p->OperStatus) { continue; }
		if (IF_TYPE_TUNNEL == p->IfType) { continue; }
		info = (NETWORK_INTERFACE_INFO*)calloc(1, sizeof(NETWORK_INTERFACE_INFO));
		if (!info) { ok = 0; break; }
		info->next = info_head;
		info_head = info;
		/* index-name */
		info->if_index = p->IfIndex;
		if (!if_indextoname(p->IfIndex, info->if_name)) { ok = 0; break; }
		/* type */
		switch (p->IfType) {
			case IF_TYPE_PPP:
				info->if_type = NETWORK_INTERFACE_PPP;
				break;
			case IF_TYPE_ISO88025_TOKENRING:
				info->if_type = NETWORK_INTERFACE_TOKENRING;
				break;
			case IF_TYPE_SOFTWARE_LOOPBACK:
				info->if_type = NETWORK_INTERFACE_LOOPBACK;
				break;
			case IF_TYPE_ETHERNET_CSMACD:
				info->if_type = NETWORK_INTERFACE_ETHERNET;
				break;
			case IF_TYPE_IEEE80211:
				info->if_type = NETWORK_INTERFACE_WIRELESS;
				break;
			case IF_TYPE_IEEE1394:
				info->if_type = NETWORK_INTERFACE_FIREWIRE;
				break;
		}
		/* mac */
		memcpy(info->phyaddr, p->PhysicalAddress, p->PhysicalAddressLength);
		info->phyaddrlen = p->PhysicalAddressLength;
		/* mtu */
		info->mtu = p->Mtu;
		for (unicast = p->FirstUnicastAddress; unicast; unicast = unicast->Next) {
			addr = (struct address_list*)calloc(1, sizeof(struct address_list));
			if (!addr) { ok = 0; break; }
			addr->next = info->address;
			info->address = addr;
			/* ip */
			if (unicast->Address.lpSockaddr->sa_family == AF_INET && unicast->OnLinkPrefixLength <= 32) {
				struct sockaddr_in* mask = (struct sockaddr_in*)&addr->mask;
				mask->sin_family = AF_INET;
				mask->sin_addr.s_addr = 0xffffffff >> (32 - unicast->OnLinkPrefixLength);
			}
			else if (unicast->Address.lpSockaddr->sa_family == AF_INET6 && unicast->OnLinkPrefixLength <= 128) {
				unsigned int i;
				struct sockaddr_in6* mask = (struct sockaddr_in6*)&addr->mask;
				mask->sin6_family = AF_INET6;
				for (i = 0; i < (unsigned int)(unicast->OnLinkPrefixLength >> 3); ++i)
					((unsigned char*)(&mask->sin6_addr))[i] = 0xff;
				if (unicast->OnLinkPrefixLength & 7)
					((unsigned char*)(&mask->sin6_addr))[i] = unicast->OnLinkPrefixLength & 7;
			}
			else {
				addr->mask.ss_family = AF_UNSPEC;
			}
			memcpy(&addr->ip, unicast->Address.lpSockaddr, unicast->Address.iSockaddrLength);
		}
	}
	free(pAdapterEntry);
#else
	struct ifaddrs* ifap = NULL;
	struct ifaddrs* p;
	if (getifaddrs(&ifap)) { return NULL; }
	for (p = ifap; p && ok; p = p->ifa_next) {
		unsigned int i;
		if (p->ifa_addr == NULL || !(p->ifa_flags & IFF_RUNNING)) { continue; }
		i = if_nametoindex(p->ifa_name);
		if (!i) { ok = 0; break; }
		for (info = info_head; info && info->if_index != i; info = info->next);
		if (!info) {
			info = (NETWORK_INTERFACE_INFO*)calloc(1, sizeof(NETWORK_INTERFACE_INFO));
			if (!info) { ok = 0; break; }
			info->next = info_head;
			info_head = info;
			/* index-name */
			info->if_index = i;
			strcpy(info->if_name, p->ifa_name);
		}
		if (!info) { ok = 0; break; }
#if defined(__FreeBSD__) || defined(__APPLE__)
		if (AF_LINK == p->ifa_addr->sa_family) {
			struct if_data* ifd = (struct if_data*)(p->ifa_data);
			struct sockaddr_dl* sdl = (struct sockaddr_dl*)(p->ifa_addr);
			/* type */
			switch (sdl->sdl_type) {
				case IFT_PPP:
					info->if_type = NETWORK_INTERFACE_PPP;
					break;
				case IFT_ISO88025:
					info->if_type = NETWORK_INTERFACE_TOKENRING;
					break;
				case IFT_LOOP:
					info->if_type = NETWORK_INTERFACE_LOOPBACK;
					break;
				case IFT_ETHER:
					info->if_type = NETWORK_INTERFACE_ETHERNET;
					break;
					/*   not defined this macro ???
				case IFT_IEEE80211:
					info->if_type = NETWORK_INTERFACE_WIRELESS;
					break;
					*/
				case IFT_IEEE1394:
					info->if_type = NETWORK_INTERFACE_FIREWIRE;
					break;
			}
			/* mtu */
			info->mtu = ifd->ifi_mtu;
			/* mac */
			memcpy(info->phyaddr, LLADDR(sdl), sdl->sdl_alen);
			info->phyaddrlen = sdl->sdl_alen;
		}
#elif __linux__
		if (AF_PACKET == p->ifa_addr->sa_family) {
			int probefd;
			struct ifreq req = {0};
			struct sockaddr_ll* sll = (struct sockaddr_ll*)(p->ifa_addr);
			/* type */
			switch (sll->sll_hatype) {
				case ARPHRD_PPP:
					info->if_type = NETWORK_INTERFACE_PPP;
					break;
				case ARPHRD_PRONET:
					info->if_type = NETWORK_INTERFACE_TOKENRING;
					break;
				case ARPHRD_LOOPBACK:
					info->if_type = NETWORK_INTERFACE_LOOPBACK;
					break;
				case ARPHRD_ETHER:
				case ARPHRD_EETHER:
					info->if_type = NETWORK_INTERFACE_ETHERNET;
					break;
				case ARPHRD_IEEE80211:
					info->if_type = NETWORK_INTERFACE_WIRELESS;
					break;
				case ARPHRD_IEEE1394:
					info->if_type = NETWORK_INTERFACE_FIREWIRE;
					break;
			}
			/* mac */
			if (p->ifa_flags & IFF_LOOPBACK) {
				info->phyaddrlen = 0;
			}
			else {
				memcpy(info->phyaddr, sll->sll_addr, sll->sll_halen);
				info->phyaddrlen = sll->sll_halen;
			}
			/* mtu */
			if ((probefd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) { ok = 0; break; }
			strcpy(req.ifr_name, p->ifa_name);
			if (ioctl(probefd, SIOCGIFMTU, &req)) { assert_true(0 == close(probefd)); ok = 0; break; }
			assert_true(0 == close(probefd));
			info->mtu = (unsigned int)(req.ifr_ifru.ifru_mtu);
		}
#endif
		else if (p->ifa_addr->sa_family == AF_INET || p->ifa_addr->sa_family == AF_INET6) {
			size_t slen;
			struct address_list* addr = (struct address_list*)calloc(1, sizeof(struct address_list));
			if (!addr) { ok = 0; break; }
			addr->next = info->address;
			info->address = addr;
			/* ip */
			slen = (p->ifa_addr->sa_family == AF_INET ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6));
			memcpy(&addr->ip, p->ifa_addr, slen);
			if (p->ifa_netmask) {
				memcpy(&addr->mask, p->ifa_netmask, slen);
			}
			else {
				addr->mask.ss_family = AF_UNSPEC;
			}
		}
	}
	if (ifap) {
		freeifaddrs(ifap);
	}
#endif
	if (!ok) {
		network_FreeInterfaceInfo(info_head);
		return NULL;
	}
	return info_head;
}

void network_FreeInterfaceInfo(NETWORK_INTERFACE_INFO* info) {
	NETWORK_INTERFACE_INFO* i;
	if (!info) { return; }
	i = info;
	while (i) {
		NETWORK_INTERFACE_INFO* p = i;
		struct address_list* a = i->address;
		i = i->next;
		while (a) {
			struct address_list* p = a;
			a = a->next;
			free(p);
		}
		free(p);
	}
}

/* SOCKET ADDRESS */
IP_TYPE IPtype(const struct sockaddr_storage* sa) {
	if (AF_INET == sa->ss_family) {
		unsigned int saddr = ntohl(((const struct sockaddr_in*)sa)->sin_addr.s_addr);
		if ((saddr >> 31) == 0)
			return IPv4_TYPE_A;
		if ((saddr >> 30) == 0x2)
			return IPv4_TYPE_B;
		if ((saddr >> 29) == 0x6)
			return IPv4_TYPE_C;
		if ((saddr >> 28) == 0xe)
			return IPv4_TYPE_D;
		if ((saddr >> 27) == 0x1e)
			return IPv4_TYPE_E;
		return IP_TYPE_UNKNOW;
	}
	else if (AF_INET6 == sa->ss_family) {
		const unsigned char* saddr = ((const struct sockaddr_in6*)sa)->sin6_addr.s6_addr;
		if (0xfe == saddr[0] && 0x80 == saddr[1]) {
			int i;
			for (i = 2; i < 8 && !saddr[i]; ++i);
			if (8 == i)
				return IPv6_TYPE_LINK;
		}
		if ((0xfe == saddr[0] || 0xfd == saddr[0]) && 0xc0 == saddr[1]) {
			int i;
			for (i = 2; i < 6 && !saddr[i]; ++i);
			if (6 == i)
				return IPv6_TYPE_SITE;
		}
		if (0xff == saddr[10] && 0xff == saddr[11]) {
			int i;
			for (i = 0; i < 10 && !saddr[i]; ++i);
			if (10 == i)
				return IPv6_TYPE_v4MAP;
		}
		if (saddr[0] & 0x20)
			return IPv6_TYPE_GLOBAL;
		return IP_TYPE_UNKNOW;
	}
	else {
		return IP_TYPE_UNKNOW;
	}
}

const char* loopbackIPstring(int family) {
	switch (family) {
		case AF_INET:
			return "127.0.0.1";
		case AF_INET6:
			return "::1";
		default:
			return "";
	}
}

BOOL IPstringIsLoopBack(const char* ip) {
	if (!strcmp(ip, "::1")) {
		return TRUE;
	}
	return strstr(ip, "127.0.0.1") != NULL;
}

BOOL IPstringIsInner(const char* ip) {
	/*
	* A 10.0.0.0		--	10.255.255.255
	* B 172.16.0.0		--	172.31.255.255
	* C 192.168.0.0	--	192.168.255.255
	*/
	unsigned int addr = inet_addr(ip);
	return ((unsigned char)addr == 0xa) ||
			((unsigned short)addr == 0xa8c0) ||
			((unsigned char)addr == 0xac && (unsigned char)(addr >> 8) >= 0x10 && (unsigned char)(addr >> 8) <= 0x1f);
}

int IPstring2Family(const char* ip) {
	while (*ip) {
		if ('.' == *ip)
			return AF_INET;
		if (':' == *ip)
			return AF_INET6;
		++ip;
	}
	return AF_UNSPEC;
}

struct addrinfo* sock_AddrInfoList(const char* host, const char* serv, int ai_socktype) {
	struct addrinfo* res = NULL;
	struct addrinfo hinfo = {0};/* some fields must clear zero */
	hinfo.ai_flags = AI_CANONNAME;
	hinfo.ai_socktype = ai_socktype;
	getaddrinfo(host,serv,&hinfo,&res);
	return res;
}

EXEC_RETURN sock_Text2Addr(struct sockaddr_storage* saddr, int af, const char* strIP, unsigned short port) {
	memset(saddr, 0, sizeof(*saddr));// win32 must zero this structure, otherwise report 10049 error.
	if (af == AF_INET) {/* IPv4 */
		struct sockaddr_in* addr_in = (struct sockaddr_in*)saddr;
		addr_in->sin_family = AF_INET;
		addr_in->sin_port = htons(port);
		if (!strIP || !strIP[0])
			addr_in->sin_addr.s_addr = htonl(INADDR_ANY);
		else {
			unsigned int net_addr = inet_addr(strIP);
			if (net_addr == INADDR_NONE)
				return EXEC_ERROR;
			else
				addr_in->sin_addr.s_addr = net_addr;
		}
		return EXEC_SUCCESS;
	}
	else if (af == AF_INET6) {
		struct sockaddr_in6* addr_in6 = (struct sockaddr_in6*)saddr;
		addr_in6->sin6_family = AF_INET6;
		addr_in6->sin6_port = htons(port);
		if (strIP && strIP[0]) {
#if defined(_WIN32) || defined(_WIN64)
			int len = sizeof(struct sockaddr_in6);
			return WSAStringToAddressA((char*)strIP, AF_INET6, NULL, (struct sockaddr*)addr_in6, &len) == 0 ? EXEC_SUCCESS : EXEC_ERROR;
#else
			return inet_pton(AF_INET6, strIP, addr_in6->sin6_addr.s6_addr) == 1 ? EXEC_SUCCESS : EXEC_ERROR;
#endif
		}
		else
			addr_in6->sin6_addr = in6addr_any;
		return EXEC_SUCCESS;
	}
	__SetErrorCode(SOCKET_ERROR_VALUE(EAFNOSUPPORT));
	return EXEC_ERROR;
}

EXEC_RETURN sock_Addr2Text(const struct sockaddr_storage* saddr, char* strIP, unsigned short* port) {
	int res;
	unsigned long len;
	if (saddr->ss_family == AF_INET) {
		struct sockaddr_in* addr_in = (struct sockaddr_in*)saddr;
		len = INET_ADDRSTRLEN + 6;
		if (port)
			*port = ntohs(addr_in->sin_port);
#if defined(_WIN32) || defined(_WIN64)
		res = WSAAddressToStringA((LPSOCKADDR)saddr, sizeof(struct sockaddr_in), NULL, strIP, &len);
		if (!res) {
			char* p;
			if (p = strchr(strIP, ':')) {
				*p = 0;
			}
		}
		return res == 0 ? EXEC_SUCCESS : EXEC_ERROR;
#else
		return inet_ntop(AF_INET, &addr_in->sin_addr, strIP, len) ? EXEC_SUCCESS : EXEC_ERROR;
#endif
	}
	else if (saddr->ss_family == AF_INET6) {
		char buf[INET6_ADDRSTRLEN + 8];
		struct sockaddr_in6* addr_in6 = (struct sockaddr_in6*)saddr;
		len = sizeof(buf);
		if (port)
			*port = ntohs(addr_in6->sin6_port);
#if defined(_WIN32) || defined(_WIN64)
		res = WSAAddressToStringA((LPSOCKADDR)saddr, sizeof(struct sockaddr_in6), NULL, buf, &len);
		if (!res) {
			char* p;
			if (p = strchr(buf, '%'))
				*p = 0;
			p = buf;
			if (*p == '[')
				*strchr(++p,']') = 0;
			strcpy(strIP, p);
		}
		return res == 0 ? EXEC_SUCCESS : EXEC_ERROR;
#else
		return inet_ntop(AF_INET6, &addr_in6->sin6_addr, strIP, len) ? EXEC_SUCCESS : EXEC_ERROR;
#endif
	}
	__SetErrorCode(SOCKET_ERROR_VALUE(EAFNOSUPPORT));
	return EXEC_ERROR;
}

EXEC_RETURN sock_AddrSetPort(struct sockaddr_storage* saddr, unsigned short port) {
	if (saddr->ss_family == AF_INET) {
		struct sockaddr_in* addr_in = (struct sockaddr_in*)saddr;
		addr_in->sin_port = htons(port);
	}
	else if (saddr->ss_family == AF_INET6) {
		struct sockaddr_in6* addr_in6 = (struct sockaddr_in6*)saddr;
		addr_in6->sin6_port = htons(port);
	}
	else {
		__SetErrorCode(SOCKET_ERROR_VALUE(EAFNOSUPPORT));
		return EXEC_ERROR;
	}
	return EXEC_SUCCESS;
}

EXEC_RETURN sock_BindSockaddr(FD_t sockfd, const struct sockaddr_storage* saddr) {
	int on = 1;
/*
#ifdef  SO_REUSEPORT
	if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, (char*)(&on), sizeof(on)) < 0)
		return EXEC_ERROR;
#endif
*/
	if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (char*)(&on), sizeof(on)) < 0)
		return EXEC_ERROR;
	return bind(sockfd, (struct sockaddr*)saddr, sizeof(*saddr)) == 0 ? EXEC_SUCCESS : EXEC_ERROR;
}

EXEC_RETURN sock_GetSockAddr(FD_t sockfd, struct sockaddr_storage* saddr) {
	socklen_t slen = sizeof(*saddr);
	return getsockname(sockfd, (struct sockaddr*)saddr, &slen) == 0 ? EXEC_SUCCESS : EXEC_ERROR;
}

EXEC_RETURN sock_GetPeerAddr(FD_t sockfd, struct sockaddr_storage* saddr) {
	socklen_t slen = sizeof(*saddr);
	return getpeername(sockfd, (struct sockaddr*)saddr, &slen) == 0 ? EXEC_SUCCESS : EXEC_ERROR;
}

/* SOCKET */
static int sock_Family(FD_t sockfd) {
	struct sockaddr_storage ss; 
	socklen_t len = sizeof(ss);
	return getsockname(sockfd, (struct sockaddr*)&ss, &len) ? -1 : ss.ss_family;
}

int sock_Type(FD_t sockfd) {
	int type;
	socklen_t len = sizeof(type);
	return getsockopt(sockfd, SOL_SOCKET, SO_TYPE, (char*)&type, &len) ? -1 : type;
}

int sock_Error(FD_t sockfd) {
	int error;
	socklen_t len = sizeof(int);
	if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, (char*)&error, &len) == 0) {
		__SetErrorCode(error);
	}
	else {
		error = __GetErrorCode();
	}
	return error;
}

EXEC_RETURN sock_UdpConnect(FD_t sockfd, const struct sockaddr_storage* saddr) {
	return connect(sockfd, (struct sockaddr*)saddr, sizeof(*saddr)) == 0 ? EXEC_SUCCESS : EXEC_ERROR;
}

EXEC_RETURN sock_UdpDisconnect(FD_t sockfd) {
	int res;
	struct sockaddr sa = {0};
	sa.sa_family = AF_UNSPEC;
	res = connect(sockfd, &sa, sizeof(sa));
	return (res != 0 && __GetErrorCode() != SOCKET_ERROR_VALUE(EAFNOSUPPORT)) ? EXEC_ERROR : EXEC_SUCCESS;
}

FD_t sock_TcpConnect(const struct sockaddr_storage* addr, int msec) {
	FD_t sockfd = INVALID_SOCKET;
	int res, error;
	/* create a TCP socket */
	sockfd = socket(addr->ss_family, SOCK_STREAM, 0);
	if (sockfd == INVALID_SOCKET)
		goto end;
	/* if set timedout,must set sockfd nonblock */
	if (msec >= 0)
		sock_NonBlock(sockfd, 1);
	/* try connect */
	res = connect(sockfd, (struct sockaddr*)addr, sizeof(*addr));
	if (!res) {
		/* connect success,destination maybe localhost */
	}
	/* occur other error...connect failure */
	else if (__GetErrorCode() != SOCKET_ERROR_VALUE(EINPROGRESS) &&
				__GetErrorCode() != SOCKET_ERROR_VALUE(EWOULDBLOCK))/* winsock report this error code */
	{
		error = __GetErrorCode();
		sock_Close(sockfd);
		__SetErrorCode(error);
		sockfd = INVALID_SOCKET;
		goto end;
	}
	else {
	/* wait connect finish...
		if timedout or socket occur error...we must close socket to stop 3 times handshank. 
	*/
		struct timeval tval;
		fd_set rset, wset;
		FD_ZERO(&rset);
		FD_SET(sockfd, &rset);
		wset = rset;
		tval.tv_sec = msec / 1000;
		tval.tv_usec = msec % 1000 * 1000;
		res = select(sockfd + 1, &rset, &wset, NULL, &tval);/* wait tval */
		if (res == 0) {/* timeout */
			sock_Close(sockfd);
			__SetErrorCode(SOCKET_ERROR_VALUE(ETIMEDOUT));
			sockfd = INVALID_SOCKET;
			goto end;
		}
		else if (FD_ISSET(sockfd, &rset) || FD_ISSET(sockfd, &wset)) {/* maybe success,maybe error */
			/* check error occur ??? */
			socklen_t len = sizeof(error);
			error = 0;/* need clear */
			res = getsockopt(sockfd, SOL_SOCKET, SO_ERROR, (char*)&error, &len);
			if (res < 0)/* solaris */
				error = errno;
			if (error) {
				sock_Close(sockfd);
				__SetErrorCode(error);
				sockfd = INVALID_SOCKET;
				goto end;
			}
		}
		else {/* select error,must close socket */
			error = __GetErrorCode();
			sock_Close(sockfd);
			__SetErrorCode(error);
			sockfd = INVALID_SOCKET;
			goto end;
		}		
	}
	/* socket connect success,then reset block io */
	if (msec >= 0)
		sock_NonBlock(sockfd, 0);
end:
	return sockfd;
}

EXEC_RETURN sock_IsListened(FD_t sockfd, BOOL* bool_value) {
	socklen_t len = sizeof(*bool_value);
	return getsockopt(sockfd, SOL_SOCKET, SO_ACCEPTCONN, (char*)bool_value, &len) == 0 ? EXEC_SUCCESS : EXEC_ERROR;
}

FD_t sock_TcpAccept(FD_t listenfd, int msec, struct sockaddr_storage* from) {
	FD_t confd = INVALID_SOCKET;
	socklen_t slen = sizeof(*from);
	socklen_t* p_slen = from ? &slen : NULL;
	if (msec < 0) {
		do {
			confd = accept(listenfd, (struct sockaddr*)from, p_slen);
		} while (confd == INVALID_SOCKET &&
				(__GetErrorCode() == SOCKET_ERROR_VALUE(ECONNABORTED) ||
				 errno == EPROTO || errno == EINTR));
	}
	else {
		int on = 1;
		struct pollfd poll_fd;
		poll_fd.fd = listenfd;
		poll_fd.events = POLLIN;
		poll_fd.revents = 0;
		do {
#if defined(_WIN32) || defined(_WIN64)
			if (WSAPoll(&poll_fd, 1, msec) < 0) {
#else
			if (poll(&poll_fd, 1, msec) < 0) {
#endif
				break;
			}
			/* avoid block... */
			if (sock_NonBlock(listenfd, 1) != EXEC_SUCCESS) {
				break;
			}
			confd = accept(listenfd, (struct sockaddr*)from, p_slen);
		} while (0);
	}
	return confd;
}

EXEC_RETURN sock_Pair(int type, FD_t sockfd[2]) {
#if defined(_WIN32) || defined(_WIN64)
	socklen_t slen = sizeof(struct sockaddr_in);
	struct sockaddr_in saddr = { 0 };
	SOCKET connfd = INVALID_SOCKET;
	sockfd[0] = sockfd[1] = INVALID_SOCKET;
	do {
		sockfd[0] = socket(AF_INET, type, 0);
		if (INVALID_SOCKET == sockfd[0]) {
			break;
		}
		sockfd[1] = socket(AF_INET, type, 0);
		if (INVALID_SOCKET == sockfd[1]) {
			break;
		}
		saddr.sin_family = AF_INET;
		saddr.sin_port = 0;
		saddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		if (bind(sockfd[0], (struct sockaddr*)&saddr, sizeof(saddr))) {
			break;
		}
		if (getsockname(sockfd[0], (struct sockaddr*)&saddr, &slen)) {
			break;
		}
		if (SOCK_STREAM == type) {
			if (listen(sockfd[0], 1)) {
				break;
			}
		}
		else {
			struct sockaddr_in saddr1;
			saddr1.sin_family = AF_INET;
			saddr1.sin_port = 0;
			saddr1.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
			if (bind(sockfd[1], (struct sockaddr*)&saddr1, sizeof(saddr1))) {
				break;
			}
			if (getsockname(sockfd[1], (struct sockaddr*)&saddr1, &slen)) {
				break;
			}
			if (connect(sockfd[0], (struct sockaddr*)&saddr1, slen)) {
				break;
			}
		}
		if (connect(sockfd[1], (struct sockaddr*)&saddr, slen)) {
			break;
		}
		if (SOCK_STREAM == type) {
			int on = 1;
			connfd = accept(sockfd[0], NULL, NULL);
			if (INVALID_SOCKET == connfd) {
				break;
			}
			closesocket(sockfd[0]);
			sockfd[0] = connfd;
			setsockopt(sockfd[0], IPPROTO_TCP, TCP_NODELAY, (char*)&on, sizeof(on));
			setsockopt(sockfd[1], IPPROTO_TCP, TCP_NODELAY, (char*)&on, sizeof(on));
		}
		return EXEC_SUCCESS;
	} while (0);
	if (sockfd[0] != INVALID_SOCKET) {
		closesocket(sockfd[0]);
	}
	if (sockfd[1] != INVALID_SOCKET) {
		closesocket(sockfd[1]);
	}
	return EXEC_ERROR;
#else
	return socketpair(AF_UNIX, type, 0, sockfd) != -1 ? EXEC_SUCCESS : EXEC_ERROR;
#endif
}

/* SOCKET IO */
EXEC_RETURN sock_NonBlock(FD_t sockfd, BOOL bool_val) {
#if defined(_WIN32) || defined(_WIN64)
	u_long ctl = bool_val ? 1 : 0;
	return ioctlsocket(sockfd, FIONBIO, &ctl) == 0 ? EXEC_SUCCESS : EXEC_ERROR;
#else
	return ioctl(sockfd, FIONBIO, &bool_val) == 0 ? EXEC_SUCCESS : EXEC_ERROR;
#endif
}

int sock_TcpReadableBytes(FD_t sockfd) {
	/* for udp: there is no a reliable sysapi to get udp next pending dgram size in different platform */
#if defined(_WIN32) || defined(_WIN64)
	u_long bytes;
	return ioctlsocket(sockfd, FIONREAD, &bytes) ? -1 : bytes;
#else
	int bytes;
	return ioctl(sockfd, FIONREAD, &bytes) ? -1 : bytes;
#endif
}

int sock_Recv(FD_t sockfd, void* buf, unsigned int nbytes, int flags, struct sockaddr_storage* from) {
	socklen_t slen = from ? sizeof(*from) : 0;
	return recvfrom(sockfd, (char*)buf, nbytes, flags, (struct sockaddr*)from, &slen);
}

int sock_Send(FD_t sockfd, const void* buf, unsigned int nbytes, int flags, const struct sockaddr_storage* to) {
	return sendto(sockfd, (const char*)buf, nbytes, flags, (struct sockaddr*)to, to ? sizeof(*to) : 0);
}

int sock_RecvVec(FD_t sockfd, IoBuf_t* iov, unsigned int iovcnt, int flags, struct sockaddr_storage* saddr) {
#if defined(_WIN32) || defined(_WIN64)
	DWORD realbytes, Flags = flags;
	int slen = saddr ? sizeof(*saddr) : 0;
	return WSARecvFrom(sockfd, iov, iovcnt, &realbytes, &Flags, (struct sockaddr*)saddr, &slen, NULL, NULL) ? -1 : realbytes;
#else
	struct msghdr msghdr = { 0 };
	msghdr.msg_name = (struct sockaddr*)saddr;
	msghdr.msg_namelen = saddr ? sizeof(*saddr) : 0;
	msghdr.msg_iov = iov;
	msghdr.msg_iovlen = iovcnt;
	return recvmsg(sockfd, &msghdr, flags);
#endif	
}

int sock_SendVec(FD_t sockfd, IoBuf_t* iov, unsigned int iovcnt, int flags, const struct sockaddr_storage* saddr) {
#if defined(_WIN32) || defined(_WIN64)
	DWORD realbytes;
	return WSASendTo(sockfd, iov, iovcnt, &realbytes, flags, (struct sockaddr*)saddr, saddr ? sizeof(*saddr) : 0, NULL, NULL) ? -1 : realbytes;
#else
	struct msghdr msghdr = {0};
	msghdr.msg_name = (struct sockaddr*)saddr;
	msghdr.msg_namelen = saddr ? sizeof(*saddr) : 0;
	msghdr.msg_iov = iov;
	msghdr.msg_iovlen = iovcnt;
	return sendmsg(sockfd, &msghdr, flags);
#endif
}

int sock_TcpSendAll(FD_t sockfd, const void* buf, unsigned int nbytes) {
	int wn = 0;
	int res;
	while (wn < (int)nbytes) {
		res = send(sockfd, ((char*)buf) + wn, nbytes - wn, 0);
		if (res > 0)
			wn += res;
		else
			return wn ? wn : res;
	}
	return nbytes;
}

int sock_TcpRecvAll(FD_t sockfd, void* buf, unsigned int nbytes) {
#ifdef	MSG_WAITALL
	return recv(sockfd, (char*)buf, nbytes, MSG_WAITALL);
#else
	int rn = 0;
	int res;
	while (rn < (int)nbytes) {
		res = recv(sockfd, ((char*)buf) + rn, nbytes - rn, 0);
		if (res > 0)
			rn += res;
		else
			return rn ? rn : res;
	}
	return nbytes;
#endif
}

int sock_TcpCanRecvOOB(FD_t sockfd){
#if defined(_WIN32) || defined(_WIN64)
    u_long ok;
	return !ioctlsocket(sockfd,SIOCATMARK,&ok) ? ok:-1;
#else
    return sockatmark(sockfd);
#endif
}

EXEC_RETURN sock_TcpRecvOOB(FD_t sockfd, unsigned char* p_oob) {
	unsigned char c;
	void* p = p_oob;
	if (p == NULL) {
		p = &c;
	}
    return recv(sockfd, (char*)p, 1, MSG_OOB) == 1 ? EXEC_SUCCESS : EXEC_ERROR;
}

/* SOCKET Reactor_t */
int sock_Select(int nfds, fd_set* rset, fd_set* wset, fd_set* xset, int msec) {
	int res;
	struct timeval tval;
	do {
		if (msec >= 0) {
			tval.tv_sec = msec / 1000;
			tval.tv_usec = msec % 1000 * 1000;
		}
		res = select(nfds, rset, wset, xset, msec < 0 ? NULL : &tval);
	} while (res < 0 && errno == EINTR);
	return res;
}

int sock_Poll(struct pollfd* fdarray, unsigned long nfds, int msec) {
	int res;
	do {
#if defined(_WIN32) || defined(_WIN64)
		res = WSAPoll(fdarray, nfds, msec);
#else
		res = poll(fdarray, nfds, msec);
#endif
	} while (res < 0 && errno == EINTR);
	return res;
}

/* SOCKET OPTION */
EXEC_RETURN sock_SetSendTimeOut(FD_t sockfd, int msec) {
#if defined(_WIN32) || defined(_WIN64)
	return setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, (char*)&msec, sizeof(msec)) == 0 ? EXEC_SUCCESS : EXEC_ERROR;
#else
	struct timeval tval;
	tval.tv_sec = msec / 1000;
	msec %= 1000;
	tval.tv_usec = msec * 1000;
	return setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, (char*)&tval, sizeof(tval)) == 0 ? EXEC_SUCCESS : EXEC_ERROR;
#endif
}

EXEC_RETURN sock_SetRecvTimeOut(FD_t sockfd, int msec) {
#if defined(_WIN32) || defined(_WIN64)
	return setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char*)&msec, sizeof(msec)) == 0 ? EXEC_SUCCESS : EXEC_ERROR;
#else
	struct timeval tval;
	tval.tv_sec = msec / 1000;
	msec %= 1000;
	tval.tv_usec = msec * 1000;
	return setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char*)&tval, sizeof(tval)) == 0 ? EXEC_SUCCESS : EXEC_ERROR;
#endif
}

EXEC_RETURN sock_SetRecvMininumSize(FD_t sockfd, int size) {
    /* nio use(eg. select) */
    return setsockopt(sockfd, SOL_SOCKET, SO_RCVLOWAT, (char*)&size, sizeof(size)) == 0 ? EXEC_SUCCESS : EXEC_ERROR;
}

EXEC_RETURN sock_SetSendMininumSize(FD_t sockfd, int size) {
    /* nio use(eg. select) */
    return setsockopt(sockfd, SOL_SOCKET, SO_SNDLOWAT, (char*)&size, sizeof(size)) == 0 ? EXEC_SUCCESS : EXEC_ERROR;
}

int sock_GetSendBufSize(FD_t sockfd) {
	int res;
	socklen_t len = sizeof(res);
	return getsockopt(sockfd, SOL_SOCKET, SO_SNDBUF,(char*)&res, &len) ? -1 : res;
}

EXEC_RETURN sock_SetSendBufSize(FD_t sockfd, int size) {
	return setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, (char*)&size, sizeof(size)) == 0 ? EXEC_SUCCESS : EXEC_ERROR;
}

int sock_GetRecvBufSize(FD_t sockfd) {
	int res;
	socklen_t len = sizeof(res);
	return getsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, (char*)&res, &len) ? -1 : res;
}

EXEC_RETURN sock_SetRecvBufSize(FD_t sockfd, int size) {
	return setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, (char*)&size, sizeof(size)) == 0 ? EXEC_SUCCESS : EXEC_ERROR;
}

EXEC_RETURN sock_SetUniCastTTL(FD_t sockfd, unsigned char ttl) {
	int val = ttl;
	int family = sock_Family(sockfd);
	if (family == AF_INET)
		return setsockopt(sockfd, IPPROTO_IP, IP_TTL, (char*)&val, sizeof(val)) == 0 ? EXEC_SUCCESS : EXEC_ERROR;
	else if (family == AF_INET6)
		return setsockopt(sockfd, IPPROTO_IPV6, IPV6_UNICAST_HOPS, (char*)&val, sizeof(val)) == 0 ? EXEC_SUCCESS : EXEC_ERROR;
	else if (family != -1)
		__SetErrorCode(SOCKET_ERROR_VALUE(EAFNOSUPPORT));
	return EXEC_ERROR;
}

EXEC_RETURN sock_SetMultiCastTTL(FD_t sockfd, int ttl) {
	int family = sock_Family(sockfd);
	if (family == AF_INET) {
		unsigned char val = ttl > 0xff ? 0xff : ttl;
		return setsockopt(sockfd, IPPROTO_IP, IP_MULTICAST_TTL, (char*)&val, sizeof(val)) == 0 ? EXEC_SUCCESS : EXEC_ERROR;
	}
	else if (family == AF_INET6)
		return setsockopt(sockfd, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, (char*)&ttl, sizeof(ttl)) == 0 ? EXEC_SUCCESS : EXEC_ERROR;
	else if (family != -1)
		__SetErrorCode(SOCKET_ERROR_VALUE(EAFNOSUPPORT));
	return EXEC_ERROR;
}

EXEC_RETURN sock_TcpGetMSS(FD_t sockfd, int* mss) {
	socklen_t len = sizeof(*mss);
	return getsockopt(sockfd, IPPROTO_TCP, TCP_MAXSEG, (char*)mss, &len) == 0 ? EXEC_SUCCESS : EXEC_ERROR;
}

EXEC_RETURN sock_TcpSetMSS(FD_t sockfd, int mss) {
	return setsockopt(sockfd, IPPROTO_TCP, TCP_MAXSEG, (char*)&mss, sizeof(mss)) == 0 ? EXEC_SUCCESS : EXEC_ERROR;
}

EXEC_RETURN sock_TcpNoDelay(FD_t sockfd, BOOL bool_val) {
	int on = (bool_val != 0);
	return setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (char*)&on, sizeof(on)) == 0 ? EXEC_SUCCESS : EXEC_ERROR;
}

EXEC_RETURN sock_TcpEnableProbePeerAlive(FD_t sockfd, BOOL bool_val) {
	int on = (bool_val != 0);
	return setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, (char*)&on, sizeof(on)) == 0 ? EXEC_SUCCESS : EXEC_ERROR;
}

EXEC_RETURN sock_TcpEnableOOBInLine(FD_t sockfd, BOOL bool_val) {
	int on = (bool_val != 0);
	return setsockopt(sockfd, SOL_SOCKET, SO_OOBINLINE, (char*)&on, sizeof(on)) == 0 ? EXEC_SUCCESS : EXEC_ERROR;
}

EXEC_RETURN sock_TcpEnableLinger(FD_t sockfd, BOOL bool_val, unsigned int sec) {
	/* it is used by close()/closesocket(), not shutdown */
	struct linger sl = {0};
	sl.l_onoff = bool_val;
	sl.l_linger = sec;
	/* if(l_onoff == TRUE && sec == 0)
			close()/closesocket() will send RST to peer host;
	*/
	return setsockopt(sockfd, SOL_SOCKET, SO_LINGER, (char*)&sl, sizeof(sl)) == 0 ? EXEC_SUCCESS : EXEC_ERROR;
}

EXEC_RETURN sock_UdpEnableBroadcast(FD_t sockfd, BOOL bool_val) {
	bool_val = bool_val ? 1:0;
	return setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, (char*)&bool_val, sizeof(bool_val)) == 0 ? EXEC_SUCCESS : EXEC_ERROR;
}

EXEC_RETURN sock_UdpMcastGroupJoin(FD_t sockfd, const struct sockaddr_storage* grp) {
	if (grp->ss_family == AF_INET) {/* IPv4 */
		struct ip_mreq req = {0};
		req.imr_interface.s_addr = htonl(INADDR_ANY);
		req.imr_multiaddr = ((struct sockaddr_in*)grp)->sin_addr;
		return setsockopt(sockfd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char*)&req, sizeof(req)) == 0 ? EXEC_SUCCESS : EXEC_ERROR;
	}
	else if (grp->ss_family == AF_INET6) {/* IPv6 */
		struct ipv6_mreq req = {0};
		req.ipv6mr_interface = 0;
		req.ipv6mr_multiaddr = ((struct sockaddr_in6*)grp)->sin6_addr;
		return setsockopt(sockfd, IPPROTO_IPV6, IPV6_JOIN_GROUP, (char*)&req, sizeof(req)) == 0 ? EXEC_SUCCESS : EXEC_ERROR;
	}
	__SetErrorCode(SOCKET_ERROR_VALUE(EAFNOSUPPORT));
	return EXEC_ERROR;
}

EXEC_RETURN sock_UdpMcastGroupLeave(FD_t sockfd, const struct sockaddr_storage* grp) {
	if (grp->ss_family == AF_INET) {/* IPv4 */
		struct ip_mreq req = {0};
		req.imr_interface.s_addr = htonl(INADDR_ANY);
		req.imr_multiaddr = ((struct sockaddr_in*)grp)->sin_addr;
		return setsockopt(sockfd, IPPROTO_IP, IP_DROP_MEMBERSHIP, (char*)&req, sizeof(req)) == 0 ? EXEC_SUCCESS : EXEC_ERROR;
	}
	else if (grp->ss_family == AF_INET6) {/* IPv6 */
		struct ipv6_mreq req = {0};
		req.ipv6mr_interface = 0;
		req.ipv6mr_multiaddr = ((struct sockaddr_in6*)grp)->sin6_addr;
		return setsockopt(sockfd, IPPROTO_IPV6, IPV6_LEAVE_GROUP, (char*)&req, sizeof(req)) == 0 ? EXEC_SUCCESS : EXEC_ERROR;
	}
	__SetErrorCode(SOCKET_ERROR_VALUE(EAFNOSUPPORT));
	return EXEC_ERROR;
}

EXEC_RETURN sock_UdpMcastEnableLoop(FD_t sockfd, BOOL bool_val) {
	int family = sock_Family(sockfd);
	if (family == AF_INET) {
		unsigned char val = (bool_val != 0);
		return setsockopt(sockfd, IPPROTO_IP, IP_MULTICAST_LOOP, (char*)&val, sizeof(val)) == 0 ? EXEC_SUCCESS : EXEC_ERROR;
	}
	else if (family == AF_INET6) {
		bool_val = (bool_val != 0);
		return setsockopt(sockfd, IPPROTO_IPV6, IPV6_MULTICAST_LOOP, (char*)&bool_val, sizeof(bool_val)) == 0 ? EXEC_SUCCESS : EXEC_ERROR;
	}
	else if (family != -1) {
		__SetErrorCode(SOCKET_ERROR_VALUE(EAFNOSUPPORT));
	}
	return EXEC_ERROR;
}

EXEC_RETURN sock_UdpMcastGetInterface(FD_t sockfd, struct in_addr* iaddr, unsigned int* ifindex) {
	int family = sock_Family(sockfd);
	socklen_t optlen;
	if (family == AF_INET) {
		optlen = sizeof(struct in_addr);
		return getsockopt(sockfd, IPPROTO_IP, IP_MULTICAST_IF, (char*)iaddr, &optlen) == 0 ? EXEC_SUCCESS : EXEC_ERROR;
	}
	else if (family == AF_INET6) {
		optlen = sizeof(unsigned int);
		return getsockopt(sockfd, IPPROTO_IPV6, IPV6_MULTICAST_IF, (char*)ifindex, &optlen) == 0 ? EXEC_SUCCESS : EXEC_ERROR;
	}
	else if (family != -1) {
		__SetErrorCode(SOCKET_ERROR_VALUE(EAFNOSUPPORT));
	}
	return EXEC_ERROR;
}

EXEC_RETURN sock_UdpMcastSetInterface(FD_t sockfd, struct in_addr iaddr, unsigned int ifindex) {
	int family = sock_Family(sockfd);
	if (family == AF_INET)
		return setsockopt(sockfd, IPPROTO_IP, IP_MULTICAST_IF, (char*)&iaddr, sizeof(struct in_addr)) == 0 ? EXEC_SUCCESS : EXEC_ERROR;
	else if (family == AF_INET6)
		return setsockopt(sockfd, IPPROTO_IPV6, IPV6_MULTICAST_IF, (char*)&ifindex, sizeof(ifindex)) == 0 ? EXEC_SUCCESS : EXEC_ERROR;
	else if (family != -1) {
		__SetErrorCode(SOCKET_ERROR_VALUE(EAFNOSUPPORT));
	}
	return EXEC_ERROR;
}

#ifdef	__cplusplus
}
#endif
