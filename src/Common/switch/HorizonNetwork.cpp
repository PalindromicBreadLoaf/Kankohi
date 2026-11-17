// SPDX-FileCopyrightText: 2026 PalindromicBreadLoaf
// SPDX-License-Identifier: GPL-3.0-or-later

#include "HorizonNetwork.h"
#include "HorizonDiagnostics.h"

#include <atomic>
#include <cerrno>
#include <mutex>

#include <netdb.h>
#include <sys/socket.h>

#include <switch.h>

extern "C" int __real_socket(int domain, int type, int protocol);
extern "C" int __real_socketpair(int domain, int type, int protocol, int sv[2]);
extern "C" int __real_getaddrinfo(const char* node, const char* service, const struct addrinfo* hints, struct addrinfo** res);
extern "C" int __real_getnameinfo(const struct sockaddr* sa, socklen_t salen, char* host, socklen_t hostlen, char* serv, socklen_t servlen, int flags);
extern "C" struct hostent* __real_gethostbyname(const char* name);
extern "C" struct hostent* __real_gethostbyaddr(const void* addr, socklen_t len, int type);

namespace
{
	enum class State
	{
		Down,
		Up,
		Failed,
	};

	std::mutex s_mutex;
	std::atomic<State> s_state{State::Down};
	bool s_sslUp = false;
}

namespace HorizonNetwork
{
	bool EnsureInitialised()
	{
		const State state = s_state.load(std::memory_order_acquire);
		if (state != State::Down)
			return state == State::Up;

		std::lock_guard lock(s_mutex);
		if (s_state.load(std::memory_order_relaxed) != State::Down)
			return s_state.load(std::memory_order_relaxed) == State::Up;

		const Result socketResult = socketInitializeDefault();
		if (R_FAILED(socketResult))
		{
			HorizonDiagnostics::Log("network: socketInitializeDefault failed (%#x)", socketResult);
			s_state.store(State::Failed, std::memory_order_release);
			return false;
		}
		const Result sslResult = sslInitialize(3);
		s_sslUp = R_SUCCEEDED(sslResult);
		if (s_sslUp)
			HorizonDiagnostics::Log("network: up");
		else
			HorizonDiagnostics::Log("network: up without ssl (%#x)", sslResult);
		s_state.store(State::Up, std::memory_order_release);
		return true;
	}

	void Shutdown()
	{
		std::lock_guard lock(s_mutex);
		if (s_state.load(std::memory_order_relaxed) == State::Up)
		{
			if (s_sslUp)
				sslExit();
			socketExit();
			s_sslUp = false;
			HorizonDiagnostics::Log("network: down");
		}
		s_state.store(State::Down, std::memory_order_release);
	}
}

extern "C" int __wrap_socket(int domain, int type, int protocol)
{
	if (!HorizonNetwork::EnsureInitialised())
	{
		errno = ENETDOWN;
		return -1;
	}
	return __real_socket(domain, type, protocol);
}

extern "C" int __wrap_socketpair(int domain, int type, int protocol, int sv[2])
{
	if (!HorizonNetwork::EnsureInitialised())
	{
		errno = ENETDOWN;
		return -1;
	}
	return __real_socketpair(domain, type, protocol, sv);
}

extern "C" int __wrap_getaddrinfo(const char* node, const char* service, const struct addrinfo* hints, struct addrinfo** res)
{
	if (!HorizonNetwork::EnsureInitialised())
		return EAI_FAIL;
	return __real_getaddrinfo(node, service, hints, res);
}

extern "C" int __wrap_getnameinfo(const struct sockaddr* sa, socklen_t salen, char* host, socklen_t hostlen, char* serv, socklen_t servlen, int flags)
{
	if (!HorizonNetwork::EnsureInitialised())
		return EAI_FAIL;
	return __real_getnameinfo(sa, salen, host, hostlen, serv, servlen, flags);
}

extern "C" struct hostent* __wrap_gethostbyname(const char* name)
{
	if (!HorizonNetwork::EnsureInitialised())
	{
		h_errno = NO_RECOVERY;
		return nullptr;
	}
	return __real_gethostbyname(name);
}

extern "C" struct hostent* __wrap_gethostbyaddr(const void* addr, socklen_t len, int type)
{
	if (!HorizonNetwork::EnsureInitialised())
	{
		h_errno = NO_RECOVERY;
		return nullptr;
	}
	return __real_gethostbyaddr(addr, len, type);
}
