// =============================================================================
// OES Enterprise — the application server's client connections
//
// cpp-httplib's own pool grows to 4 * max(8, cores - 1) and then queues. A
// WebSocket holds its thread until the socket closes, so the client past that
// ceiling is accepted and never answered. ClientConnections raises the ceiling
// (1000 when backend.conf leaves it out). This opens more sockets than the old
// ceiling and requires a reply on every one.
// =============================================================================

#include <gtest/gtest.h>

#include "backend/appHost.h"
#include "clientListener.h"
#include "frmserver/client/clientHost.h"

#include <wx/file.h>
#include <wx/fileconf.h>
#include <wx/filename.h>
#include <wx/init.h>
#include <wx/stdpaths.h>

#if defined(_WIN32) && !defined(_SSIZE_T_DEFINED)
#	define _SSIZE_T_DEFINED
#endif
#include <cpp-httplib/httplib.h>

#include <string>
#include <thread>
#include <vector>

namespace {

void WxReady()
{
	static wxInitializer init;
	ASSERT_TRUE(init.IsOk());
}

// The pool the listener used to inherit. Never under 32, and 32 on 8 cores.
unsigned OldClientCeiling()
{
	const unsigned cores = std::thread::hardware_concurrency();
	const unsigned base = std::max(8u, cores > 0 ? cores - 1 : 0u);
	return base * 4;
}

} // namespace

TEST(ClientConnections, AbsentOrZeroMeansOneThousand) {
	WxReady();
	wxFileConfig empty(wxT(""), wxT(""), wxT(""), wxT(""));
	EXPECT_EQ(ibApplicationHost::ReadCount(empty, wxT("backend.conf"), wxT("ClientConnections"), 1, 1000),
		static_cast<std::size_t>(1000));

	const wxString path = wxFileName(wxStandardPaths::Get().GetTempDir(),
		wxT("oes-client-connections.conf")).GetFullPath();
	{
		wxFile file;
		ASSERT_TRUE(file.Create(path, true));
		ASSERT_TRUE(file.Write(wxString(wxT("ClientConnections=40\n"))));
	}
	wxFileConfig forty(wxT(""), wxT(""), wxT(""), path);
	EXPECT_EQ(ibApplicationHost::ReadCount(forty, wxT("backend.conf"), wxT("ClientConnections"), 1, 1000),
		static_cast<std::size_t>(40));
	wxRemoveFile(path);

	{
		wxFile file;
		ASSERT_TRUE(file.Create(path, true));
		ASSERT_TRUE(file.Write(wxString(wxT("ClientConnections=0\n"))));
	}
	wxFileConfig zero(wxT(""), wxT(""), wxT(""), path);
	EXPECT_EQ(ibApplicationHost::ReadCount(zero, wxT("backend.conf"), wxT("ClientConnections"), 1, 1000),
		static_cast<std::size_t>(1000));
	wxRemoveFile(path);
}

TEST(ClientListener, MoreConnectionsThanTheOldPoolAllGetAReply) {
	WxReady();
	const unsigned ceiling = OldClientCeiling();
	const unsigned count = ceiling + 4;
	ASSERT_LT(count, 1000u)
		<< "this machine's old pool already holds more than the new default";

	// No application host in this process, so the listener takes the default ceiling (1000).
	ASSERT_EQ(ibApplicationHost::Get(), nullptr);

	ibClientHost host(nullptr);
	ibClientListener listener;
	unsigned short port = 28173;
	std::map<wxString, ibClientHost*> hosts;
	hosts[wxT("demo")] = &host;
	wxString refusal;
	ASSERT_TRUE(listener.Start(wxT("127.0.0.1"), port, true, std::move(hosts), refusal))
		<< refusal.ToStdString();

	const std::string url = "ws://127.0.0.1:" + std::to_string(port) + "/demo/client";
	const std::string request = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}";

	std::vector<std::unique_ptr<httplib::ws::WebSocketClient>> clients(count);
	std::vector<int> opened(count, 0);
	std::vector<std::thread> threads;
	threads.reserve(count);
	for (unsigned i = 0; i < count; ++i) {
		clients[i] = std::make_unique<httplib::ws::WebSocketClient>(url);
		clients[i]->set_connection_timeout(8);
		clients[i]->set_read_timeout(8);
		threads.emplace_back([&, i] {
			opened[i] = static_cast<bool>(clients[i]->connect()) ? 1 : 0;
		});
	}
	for (std::thread& thread : threads)
		thread.join();

	unsigned connected = 0;
	for (int open : opened)
		connected += static_cast<unsigned>(open);
	EXPECT_EQ(connected, count);

	std::vector<std::string> replies(count);
	std::vector<int> reads(count, -1);
	threads.clear();
	for (unsigned i = 0; i < count; ++i) {
		if (!opened[i])
			continue;
		threads.emplace_back([&, i] {
			if (!clients[i]->send(request)) {
				reads[i] = -2;
				return;
			}
			reads[i] = static_cast<int>(clients[i]->read(replies[i]));
		});
	}
	for (std::thread& thread : threads)
		thread.join();

	for (unsigned i = 0; i < count; ++i) {
		if (!opened[i])
			continue;
		EXPECT_EQ(reads[i], static_cast<int>(httplib::ws::Text)) << "connection " << i;
		EXPECT_NE(replies[i].find("no method named"), std::string::npos)
			<< "connection " << i << " answered: " << replies[i];
	}

	clients.clear();
	listener.Stop();
}
