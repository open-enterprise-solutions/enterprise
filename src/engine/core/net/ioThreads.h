#ifndef __CORE_NET_IO_THREADS_H__
#define __CORE_NET_IO_THREADS_H__

// THE I/O THREADS OF A PROCESS THAT LISTENS ON NOTHING — the designer's debugger, a thin client's connection: the
// context core/net's clients run on (httpClient.h), and the threads that carry it. A server has its own
// (ibHttpServer::GetContext). A socket waiting for its peer costs a buffer here, not a thread.
//
// ⚠ THE OWNER LETS ITS CLIENTS GO FIRST. A client's socket is the context's; one that outlives the context is closed
// on nothing. Whoever holds both declares this one before the clients, so it is the last to go.

#include <cstddef>
#include <memory>

#include "core/core.h"

namespace asio { class io_context; }

// The context and its threads — Asio's, kept in the core (ioThreads.cpp).
struct ibIoThreadsState;

class CORE_API ibIoThreads {
public:

	// `threads` — how many carry the context; at least one.
	explicit ibIoThreads(std::size_t threads = 1);
	// The context stopped and its threads joined: what was still waiting on it is let go.
	~ibIoThreads();

	ibIoThreads(const ibIoThreads&) = delete;
	ibIoThreads& operator=(const ibIoThreads&) = delete;

	asio::io_context& GetContext();

private:

	std::unique_ptr<ibIoThreadsState> m_state;
};

#endif
