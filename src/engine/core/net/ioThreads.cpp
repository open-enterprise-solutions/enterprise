#include "ioThreads.h"

#include <algorithm>
#include <optional>
#include <thread>
#include <vector>

#include <asio/executor_work_guard.hpp>
#include <asio/io_context.hpp>

struct ibIoThreadsState {
	asio::io_context                                                         m_io;
	std::optional<asio::executor_work_guard<asio::io_context::executor_type>> m_work;   // the context runs with nothing queued
	std::vector<std::thread>                                                 m_threads;
};

ibIoThreads::ibIoThreads(std::size_t threads) :
	m_state(std::make_unique<ibIoThreadsState>())
{
	ibIoThreadsState& state = *m_state;
	state.m_work.emplace(asio::make_work_guard(state.m_io));
	for (std::size_t i = 0; i < std::max<std::size_t>(threads, 1); ++i) {
		state.m_threads.emplace_back([&state]() {
			// A handler that throws is not the end of the threads that carry every socket (httpServer.cpp says the same).
			for (;;) {
				try {
					state.m_io.run();
					return;
				}
				catch (...) {
				}
			}
		});
	}
}

ibIoThreads::~ibIoThreads()
{
	ibIoThreadsState& state = *m_state;
	state.m_work.reset();
	state.m_io.stop();
	for (std::thread& thread : state.m_threads) {
		if (thread.joinable())
			thread.join();
	}
}

asio::io_context& ibIoThreads::GetContext()
{
	return m_state->m_io;
}
