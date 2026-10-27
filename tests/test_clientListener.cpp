// =============================================================================
// OES Enterprise — the application server's client connections
//
// The listener is the core's HTTP (core/net), asynchronous: a socket waiting for
// its client costs a buffer, not a thread. Under cpp-httplib a WebSocket held a
// pool thread until it closed, and the client past 4 * max(8, cores - 1) was
// accepted and never answered. ClientConnections is the ceiling (1000 when
// backend.conf leaves it out). This opens more sockets than the old pool held
// and requires a reply on every one, and a burst of them adds no thread.
// =============================================================================

#include <gtest/gtest.h>

#include "backend/appData.h"
#include "backend/appHost.h"
#include "backend/rpc/rpcMessage.h"
#include "clientListener.h"
#include "core/serialize/dataBuilder.h"
#include "frmserver/client/clientHost.h"

#include <wx/file.h>
#include <wx/fileconf.h>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/init.h>
#include <wx/log.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>

#if defined(_WIN32) && !defined(_SSIZE_T_DEFINED)
#	define _SSIZE_T_DEFINED
#endif
#include <cpp-httplib/httplib.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <future>
#include <string>
#include <thread>
#include <vector>

#if defined(__linux__)
#	include <dirent.h>
#elif defined(_WIN32)
#	include <windows.h>
#	include <tlhelp32.h>
#elif defined(__APPLE__)
#	include <mach/mach.h>
#endif

namespace {

void WxReady()
{
	static wxInitializer init;
	ASSERT_TRUE(init.IsOk());
	// A warning on the GUI log is a dialog. Headless, that dialog never closes.
	if (wxLog::GetActiveTarget() != nullptr)
		delete wxLog::SetActiveTarget(new wxLogStderr());
}

// The pool the listener used to inherit. Never under 32, and 32 on 8 cores.
unsigned OldClientCeiling()
{
	const unsigned cores = std::thread::hardware_concurrency();
	const unsigned base = std::max(8u, cores > 0 ? cores - 1 : 0u);
	return base * 4;
}

// A request for a method nobody has, as the packet a client sends (OES-RPC): the server answers it with a refusal
// that names the method. Text it does not take — it closes the socket.
const std::string& PingPacket()
{
	static const std::string packet = [] {
		ibDataNode request;
		request.SetValue(wxT("id"), 1);
		request.SetValue(wxT("method"), ibString(wxT("ping")));
		ibMemoryBuffer bytes;
		ibBinaryProvider().Write(request, bytes);
		return std::string(static_cast<const char*>(bytes.GetData()), bytes.GetDataLen());
	}();
	return packet;
}

bool SendPing(httplib::ws::WebSocketClient& client)
{
	return client.send(PingPacket().data(), PingPacket().size());
}

// THE TEST'S CLIENT WRITES WHERE THE SERVER HAS HUNG UP — a close frame after Stop, a ping past the fifteen seconds.
// Off Windows such a write raises SIGPIPE, whose default ends the process, and under ctest the process is the test
// (macOS CI, 2026-10-26). Ignored for the test's duration: the other way round from ibDefaultSigpipe
// (test_valueHttp.cpp), which puts the default back to see that the engine's own sends never raise it.
#ifndef _WIN32
struct ibIgnoredSigpipe {
	void (*m_before)(int);
	ibIgnoredSigpipe() : m_before(signal(SIGPIPE, SIG_IGN)) {}
	~ibIgnoredSigpipe() { signal(SIGPIPE, m_before); }
};
#else
struct ibIgnoredSigpipe {};
#endif

// …and the refusal, read from the packet it came in: its JSON view names the method.
bool Refused(const std::string& packet)
{
	ibDataNode answer;
	if (!ibBinaryProvider().Read(ibMemoryBuffer(packet.data(), packet.size()), answer))
		return false;
	return ibRpcRenderNode(answer).ToUtf8().find("no method named") != std::string::npos;
}

// Threads of this process. A retired pool thread stays visible until some later enqueue joins it.
unsigned CountThreads()
{
#if defined(__linux__)
	unsigned count = 0;
	if (DIR* dir = ::opendir("/proc/self/task")) {
		while (::readdir(dir) != nullptr)
			++count;
		::closedir(dir);
	}
	return count >= 2 ? count - 2 : 0;
#elif defined(_WIN32)
	unsigned count = 0;
	const DWORD self = ::GetCurrentProcessId();
	const HANDLE snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
	if (snap == INVALID_HANDLE_VALUE)
		return 0;
	THREADENTRY32 entry;
	entry.dwSize = sizeof(entry);
	if (::Thread32First(snap, &entry)) {
		do {
			if (entry.th32OwnerProcessID == self)
				++count;
		} while (::Thread32Next(snap, &entry));
	}
	::CloseHandle(snap);
	return count;
#elif defined(__APPLE__)
	thread_act_array_t threads = nullptr;
	mach_msg_type_number_t count = 0;
	if (::task_threads(::mach_task_self(), &threads, &count) != KERN_SUCCESS)
		return 0;
	for (mach_msg_type_number_t i = 0; i < count; ++i)
		::mach_port_deallocate(::mach_task_self(), threads[i]);
	::vm_deallocate(::mach_task_self(), reinterpret_cast<vm_address_t>(threads),
		static_cast<vm_size_t>(count) * sizeof(*threads));
	return static_cast<unsigned>(count);
#else
	return 0;
#endif
}

bool Answered(httplib::ws::WebSocketClient& client)
{
	if (!SendPing(client))
		return false;
	std::string reply;
	if (client.read(reply) != httplib::ws::Binary)
		return false;
	return Refused(reply);
}

// Connected, and a reply has come back, so the server holds the socket and has answered on it.
bool Hold(std::unique_ptr<httplib::ws::WebSocketClient>& client, const std::string& url)
{
	client = std::make_unique<httplib::ws::WebSocketClient>(url);
	client->set_connection_timeout(8);
	client->set_read_timeout(8);
	// No heartbeat: cpp-httplib pings from a thread of each socket's own, and the threads counted here are the server's.
	client->set_websocket_ping_interval(0);
	if (!client->connect())
		return false;
	return Answered(*client);
}

bool Bind(ibClientListener& listener, ibClientHost& host, unsigned short& port, std::string& url)
{
	std::map<ibString, ibClientHost*> hosts;
	hosts[wxT("demo")] = &host;
	ibString refusal;
	if (!listener.Start(ibPortRange{ port, 100 }, port, std::move(hosts), refusal))
		return false;
	url = "ws://127.0.0.1:" + std::to_string(port) + "/demo/client";
	return true;
}

// Stop while sockets are still open. The future is not an std::async future: its destructor does not wait,
// so a pool that never returns fails this assertion instead of hanging it.
void StopWithin(ibClientListener& listener)
{
	std::packaged_task<void()> task([&listener] { listener.Stop(); });
	std::future<void> stopped = task.get_future();
	std::thread stopper(std::move(task));
	const auto status = stopped.wait_for(std::chrono::seconds(10));
	EXPECT_EQ(status, std::future_status::ready);
	if (status == std::future_status::ready)
		stopper.join();
	else
		stopper.detach();
}

struct RestoreCwd {
	ibString saved;
	RestoreCwd() : saved(wxGetCwd()) {}
	~RestoreCwd() { wxSetWorkingDirectory(saved); }
};

struct RestoreHost {
	~RestoreHost()
	{
		if (ibApplicationInstance::Get() != nullptr)
			ibApplicationInstance::DestroyAppDataEnv();
	}
};

} // namespace

TEST(ClientConnections, AbsentOrZeroMeansOneThousand) {
	WxReady();
	wxFileConfig empty(wxT(""), wxT(""), wxT(""), wxT(""));
	EXPECT_EQ(ibApplicationHost::ReadCount(empty, wxT("backend.conf"), wxT("ClientConnections"), 1, 1000),
		static_cast<std::size_t>(1000));

	const ibString path = wxFileName(wxStandardPaths::Get().GetTempDir(),
		wxT("oes-client-connections.conf")).GetFullPath();
	{
		wxFile file;
		ASSERT_TRUE(file.Create(path, true));
		ASSERT_TRUE(file.Write(ibString(wxT("ClientConnections=40\n"))));
	}
	wxFileConfig forty(wxT(""), wxT(""), wxT(""), path);
	EXPECT_EQ(ibApplicationHost::ReadCount(forty, wxT("backend.conf"), wxT("ClientConnections"), 1, 1000),
		static_cast<std::size_t>(40));
	wxRemoveFile(path);

	{
		wxFile file;
		ASSERT_TRUE(file.Create(path, true));
		ASSERT_TRUE(file.Write(ibString(wxT("ClientConnections=0\n"))));
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
	std::map<ibString, ibClientHost*> hosts;
	hosts[wxT("demo")] = &host;
	ibString refusal;
	ASSERT_TRUE(listener.Start(ibPortRange{ port, 100 }, port, std::move(hosts), refusal))
		<< refusal.ToStdString();

	const std::string url = "ws://127.0.0.1:" + std::to_string(port) + "/demo/client";

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
			if (!SendPing(*clients[i])) {
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
		EXPECT_EQ(reads[i], static_cast<int>(httplib::ws::Binary)) << "connection " << i;
		EXPECT_TRUE(Refused(replies[i])) << "connection " << i;
	}

	clients.clear();
	listener.Stop();
}

TEST(ClientConnections, ReadFromTheFileTheServerReads) {
	WxReady();
	RestoreCwd cwd;
	RestoreHost host;
	const ibString dir = wxFileName(wxStandardPaths::Get().GetTempDir(),
		ibString::Format(wxT("oes-cc-%ld"), static_cast<long>(wxGetProcessId()))).GetFullPath();
	wxMkdir(dir);
	ASSERT_TRUE(wxSetWorkingDirectory(dir));

	// A fresh process each time: the file is read once, when the host is made.
	const auto expect = [&](const ibString& body, std::size_t want) {
		struct Guard {
			~Guard()
			{
				if (ibApplicationInstance::Get() != nullptr)
					ibApplicationInstance::DestroyAppDataEnv();
			}
		} guard;
		wxFile file;
		ASSERT_TRUE(file.Create(dir + wxFILE_SEP_PATH + wxT("backend.conf"), true));
		ASSERT_TRUE(file.Write(body));
		file.Close();
		// The locale catalog may refuse in a headless run. The host is still made, and it has already
		// read this file — that read is what this test is about.
		ibApplicationInstance::CreateAppDataEnv(ibRunMode::eFILE_MODE);
		ASSERT_NE(ibApplicationHost::Get(), nullptr);
		EXPECT_EQ(ibApplicationHost::Get()->GetClientConnections(), want);
		ASSERT_TRUE(ibApplicationInstance::DestroyAppDataEnv());
		EXPECT_EQ(ibApplicationHost::Get(), nullptr);
	};

	expect(wxT("ClientConnections=40\n"), static_cast<std::size_t>(40));
	expect(wxT("ClientConnections=0\n"), ibApplicationHost::kDefaultClientConnections);
	expect(wxT("Locale=en\n"), ibApplicationHost::kDefaultClientConnections);
	expect(wxT("ClientConnections=100000\n"), ibApplicationHost::kMostClientConnections);

	wxRemoveFile(dir + wxFILE_SEP_PATH + wxT("backend.conf"));
	wxRmdir(dir);
}

TEST(ClientListener, RefusesTheConnectionOnceTheCeilingIsBusy) {
	WxReady();
	RestoreCwd cwd;
	RestoreHost process;
	const ibString dir = wxFileName(wxStandardPaths::Get().GetTempDir(),
		ibString::Format(wxT("oes-cc-ceiling-%ld"), static_cast<long>(wxGetProcessId()))).GetFullPath();
	wxMkdir(dir);
	ASSERT_TRUE(wxSetWorkingDirectory(dir));
	{
		wxFile file;
		ASSERT_TRUE(file.Create(dir + wxFILE_SEP_PATH + wxT("backend.conf"), true));
		ASSERT_TRUE(file.Write(ibString(wxT("ClientConnections=2\n"))));
	}
	ASSERT_TRUE(ibApplicationInstance::CreateAppDataEnv(ibRunMode::eFILE_MODE));
	ASSERT_EQ(ibApplicationHost::Get()->GetClientConnections(), static_cast<std::size_t>(2));

	ibClientHost host(nullptr);
	ibClientListener listener;
	unsigned short port = 28180;
	std::string url;
	ASSERT_TRUE(Bind(listener, host, port, url));

	std::unique_ptr<httplib::ws::WebSocketClient> first;
	std::unique_ptr<httplib::ws::WebSocketClient> second;
	ASSERT_TRUE(Hold(first, url));
	ASSERT_TRUE(Hold(second, url));

	httplib::ws::WebSocketClient refused(url);
	refused.set_connection_timeout(8);
	refused.set_read_timeout(8);
	EXPECT_FALSE(refused.connect());

	// The two that got in are still answered: the refusal closed a socket, it did not stop the listener.
	EXPECT_TRUE(Answered(*first));

	first.reset();
	second.reset();
	listener.Stop();
	wxRemoveFile(dir + wxFILE_SEP_PATH + wxT("backend.conf"));
	wxRmdir(dir);
}

TEST(ClientListener, ABurstOfConnectionsAddsNoThread) {
	WxReady();
	ASSERT_EQ(ibApplicationHost::Get(), nullptr);
	const unsigned count = OldClientCeiling() + 4;
	ASSERT_LT(count, 1000u);

	ibClientHost host(nullptr);
	ibClientListener listener;
	unsigned short port = 28190;
	std::string url;
	ASSERT_TRUE(Bind(listener, host, port, url));

	const unsigned before = CountThreads();
	ASSERT_GT(before, 0u);

	// Each one answered, so each is a socket the server is inside of rather than one still in its backlog.
	std::vector<std::unique_ptr<httplib::ws::WebSocketClient>> clients(count);
	for (unsigned i = 0; i < clients.size(); ++i)
		ASSERT_TRUE(Hold(clients[i], url)) << "connection " << i;
	const unsigned during = CountThreads();

	// One thread of noise is room for a helper that came and went. A thread per socket fails by the burst.
	EXPECT_LE(during, before + 1);

	clients.clear();
	listener.Stop();
}

TEST(ClientListener, StopReturnsWhileSocketsAreOpen) {
	WxReady();
	ASSERT_EQ(ibApplicationHost::Get(), nullptr);
	const ibIgnoredSigpipe ignored{};   // the clients close after the listener has stopped

	ibClientHost host(nullptr);
	ibClientListener listener;
	unsigned short port = 28210;
	std::string url;
	ASSERT_TRUE(Bind(listener, host, port, url));

	std::vector<std::unique_ptr<httplib::ws::WebSocketClient>> clients(4);
	for (unsigned i = 0; i < clients.size(); ++i)
		ASSERT_TRUE(Hold(clients[i], url)) << "connection " << i;

	StopWithin(listener);
	clients.clear();
}

// Sending does not buy another fifteen seconds. Once #224 is merged, also cover a socket that opens
// and resumes within those fifteen seconds.
TEST(ClientListener, ASocketThatNeverLogsInClosesFifteenSecondsAfterItOpened) {
	WxReady();
	ASSERT_EQ(ibApplicationHost::Get(), nullptr);
	const ibIgnoredSigpipe ignored{};   // a ping may go after the server has closed

	ibClientHost host(nullptr);
	ibClientListener listener;
	unsigned short port = 28230;
	std::string url;
	ASSERT_TRUE(Bind(listener, host, port, url));

	httplib::ws::WebSocketClient client(url);
	client.set_connection_timeout(8);
	client.set_read_timeout(2);
	ASSERT_TRUE(client.connect());

	const auto opened = std::chrono::steady_clock::now();
	auto nextSend = opened;
	bool closed = false;
	while (std::chrono::steady_clock::now() - opened < std::chrono::seconds(25)) {
		if (std::chrono::steady_clock::now() >= nextSend) {
			if (!SendPing(client)) {
				closed = true;
				break;
			}
			nextSend += std::chrono::seconds(5);
		}
		std::string reply;
		const auto read = client.read(reply);
		if (read == httplib::ws::Binary || read == httplib::ws::Timeout)
			continue;
		closed = true;
		break;
	}

	const auto elapsed = std::chrono::steady_clock::now() - opened;
	EXPECT_TRUE(closed);
	EXPECT_GE(elapsed, std::chrono::seconds(14));
	EXPECT_LE(elapsed, std::chrono::seconds(20));

	listener.Stop();
}

// A message every 200 ms does not move the mark: the deadline counts from the open, so the socket closes at
// about fifteen seconds anyway.
TEST(ClientListener, ASocketThatKeepsSendingClosesFifteenSecondsAfterItOpened) {
	WxReady();
	ASSERT_EQ(ibApplicationHost::Get(), nullptr);
	const ibIgnoredSigpipe ignored{};   // a ping may go after the server has closed

	ibClientHost host(nullptr);
	ibClientListener listener;
	unsigned short port = 28231;
	std::string url;
	ASSERT_TRUE(Bind(listener, host, port, url));

	httplib::ws::WebSocketClient client(url);
	client.set_connection_timeout(8);
	client.set_read_timeout(2);
	ASSERT_TRUE(client.connect());

	const auto opened = std::chrono::steady_clock::now();
	auto nextSend = opened;
	bool closed = false;
	while (std::chrono::steady_clock::now() - opened < std::chrono::seconds(25)) {
		if (std::chrono::steady_clock::now() >= nextSend) {
			if (!SendPing(client)) {
				closed = true;
				break;
			}
			nextSend += std::chrono::milliseconds(200);
		}
		std::string reply;
		const auto read = client.read(reply);
		if (read == httplib::ws::Binary || read == httplib::ws::Timeout)
			continue;
		closed = true;
		break;
	}

	const auto elapsed = std::chrono::steady_clock::now() - opened;
	EXPECT_TRUE(closed);
	EXPECT_GE(elapsed, std::chrono::seconds(14));
	EXPECT_LE(elapsed, std::chrono::seconds(20));

	listener.Stop();
}
