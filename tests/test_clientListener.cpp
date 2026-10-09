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

#include "backend/appData.h"
#include "backend/appHost.h"
#include "clientListener.h"
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

const char kPing[] = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}";

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
	if (!client.send(kPing))
		return false;
	std::string reply;
	if (client.read(reply) != httplib::ws::Text)
		return false;
	return reply.find("no method named") != std::string::npos;
}

// Connected, and a reply has come back, so a pool thread is inside the socket rather than idle.
bool Hold(std::unique_ptr<httplib::ws::WebSocketClient>& client, const std::string& url)
{
	client = std::make_unique<httplib::ws::WebSocketClient>(url);
	client->set_connection_timeout(8);
	client->set_read_timeout(8);
	if (!client->connect())
		return false;
	return Answered(*client);
}

bool Bind(ibClientListener& listener, ibClientHost& host, unsigned short& port, std::string& url)
{
	std::map<wxString, ibClientHost*> hosts;
	hosts[wxT("demo")] = &host;
	wxString refusal;
	if (!listener.Start(wxT("127.0.0.1"), port, true, std::move(hosts), refusal))
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
	wxString saved;
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

TEST(ClientConnections, ReadFromTheFileTheServerReads) {
	WxReady();
	RestoreCwd cwd;
	RestoreHost host;
	const wxString dir = wxFileName(wxStandardPaths::Get().GetTempDir(),
		wxString::Format(wxT("oes-cc-%ld"), static_cast<long>(wxGetProcessId()))).GetFullPath();
	wxMkdir(dir);
	ASSERT_TRUE(wxSetWorkingDirectory(dir));

	// A fresh process each time: the file is read once, when the host is made.
	const auto expect = [&](const wxString& body, std::size_t want) {
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
	const wxString dir = wxFileName(wxStandardPaths::Get().GetTempDir(),
		wxString::Format(wxT("oes-cc-ceiling-%ld"), static_cast<long>(wxGetProcessId()))).GetFullPath();
	wxMkdir(dir);
	ASSERT_TRUE(wxSetWorkingDirectory(dir));
	{
		wxFile file;
		ASSERT_TRUE(file.Create(dir + wxFILE_SEP_PATH + wxT("backend.conf"), true));
		ASSERT_TRUE(file.Write(wxString(wxT("ClientConnections=2\n"))));
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

TEST(ClientListener, RetiresTheThreadsABurstNoLongerNeeds) {
	WxReady();
	ASSERT_EQ(ibApplicationHost::Get(), nullptr);
	const unsigned base = static_cast<unsigned>(CPPHTTPLIB_THREAD_POOL_COUNT);
	const unsigned surplus = 4;
	ASSERT_LT(base + surplus, 1000u);

	ibClientHost host(nullptr);
	ibClientListener listener;
	unsigned short port = 28190;
	std::string url;
	ASSERT_TRUE(Bind(listener, host, port, url));

	const unsigned before = CountThreads();
	ASSERT_GT(before, 0u);

	std::vector<std::unique_ptr<httplib::ws::WebSocketClient>> clients(base + surplus);
	for (unsigned i = 0; i < clients.size(); ++i)
		ASSERT_TRUE(Hold(clients[i], url)) << "connection " << i;
	const unsigned during = CountThreads();
	EXPECT_GE(during, before + surplus);

	clients.clear();
	// The extra threads wait the same few seconds httplib waits, then leave the pool. They are joined on the
	// next enqueue — a thread does not join itself — which is when they stop counting.
	std::this_thread::sleep_for(std::chrono::seconds(CPPHTTPLIB_THREAD_POOL_IDLE_TIMEOUT + 2));
	std::unique_ptr<httplib::ws::WebSocketClient> probe;
	ASSERT_TRUE(Hold(probe, url));
	probe.reset();

	const unsigned after = CountThreads();
	// One thread of noise is room for a helper that came and went. A pool that kept the burst fails by the surplus.
	EXPECT_LE(after + surplus, during + 1);

	listener.Stop();
}

TEST(ClientListener, AThreadTheSystemWillNotCreateIsARefusal) {
	WxReady();
	ASSERT_EQ(ibApplicationHost::Get(), nullptr);
	const unsigned base = static_cast<unsigned>(CPPHTTPLIB_THREAD_POOL_COUNT);

	ibClientHost host(nullptr);
	ibClientListener listener;
	unsigned short port = 28200;
	std::string url;
	ASSERT_TRUE(Bind(listener, host, port, url));

	std::vector<std::unique_ptr<httplib::ws::WebSocketClient>> clients(base);
	for (unsigned i = 0; i < clients.size(); ++i)
		ASSERT_TRUE(Hold(clients[i], url)) << "connection " << i;

	ibClientListenerFailNextThreads(1);
	std::unique_ptr<httplib::ws::WebSocketClient> refused;
	EXPECT_FALSE(Hold(refused, url));
	ibClientListenerFailNextThreads(0);

	StopWithin(listener);
}

TEST(ClientListener, StopReturnsWhileSocketsAreOpen) {
	WxReady();
	ASSERT_EQ(ibApplicationHost::Get(), nullptr);

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

// An idle thread with a job already waiting is not a free thread. The pool is held still so the two
// arrivals cannot be reordered by a worker taking the first one.
struct HoldIdleThreads {
	HoldIdleThreads() { ibClientListenerHoldIdleThreads(true); }
	~HoldIdleThreads() { ibClientListenerHoldIdleThreads(false); }
};

TEST(ClientListener, AnIdleThreadThatAlreadyHasAJobIsNotFree) {
	WxReady();
	RestoreCwd cwd;
	RestoreHost process;
	const wxString dir = wxFileName(wxStandardPaths::Get().GetTempDir(),
		wxString::Format(wxT("oes-cc-queued-%ld"), static_cast<long>(wxGetProcessId()))).GetFullPath();
	wxMkdir(dir);
	ASSERT_TRUE(wxSetWorkingDirectory(dir));
	{
		wxFile file;
		ASSERT_TRUE(file.Create(dir + wxFILE_SEP_PATH + wxT("backend.conf"), true));
		ASSERT_TRUE(file.Write(wxString(wxT("ClientConnections=1\n"))));
	}
	ASSERT_TRUE(ibApplicationInstance::CreateAppDataEnv(ibRunMode::eFILE_MODE));
	ASSERT_EQ(ibApplicationHost::Get()->GetClientConnections(), static_cast<std::size_t>(1));

	ibClientHost host(nullptr);
	ibClientListener listener;
	unsigned short port = 28220;
	std::string url;
	ASSERT_TRUE(Bind(listener, host, port, url));

	HoldIdleThreads hold;
	std::atomic<int> first{ -1 };
	std::thread opener([&] {
		httplib::ws::WebSocketClient client(url);
		client.set_connection_timeout(3);
		first = client.connect() ? 1 : 0;
	});

	const auto parked = std::chrono::steady_clock::now();
	while (first.load() < 0
		&& std::chrono::steady_clock::now() - parked < std::chrono::milliseconds(500))
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	// The handshake has not completed: the one idle thread is still idle, and the job is queued for it.
	ASSERT_LT(first.load(), 0);

	httplib::ws::WebSocketClient second(url);
	second.set_connection_timeout(3);
	const auto began = std::chrono::steady_clock::now();
	EXPECT_FALSE(second.connect());
	EXPECT_LT(std::chrono::steady_clock::now() - began, std::chrono::milliseconds(500));

	ibClientListenerHoldIdleThreads(false);
	opener.join();
	listener.Stop();
	ibApplicationInstance::DestroyAppDataEnv();
	wxRemoveFile(dir + wxFILE_SEP_PATH + wxT("backend.conf"));
	wxRmdir(dir);
}

// Sending does not buy another fifteen seconds. Once #224 is merged, also cover a socket that opens
// and resumes within those fifteen seconds.
TEST(ClientListener, ASocketThatNeverLogsInClosesFifteenSecondsAfterItOpened) {
	WxReady();
	ASSERT_EQ(ibApplicationHost::Get(), nullptr);

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
			if (!client.send("x")) {
				closed = true;
				break;
			}
			nextSend += std::chrono::seconds(5);
		}
		std::string reply;
		const auto read = client.read(reply);
		if (read == httplib::ws::Text || read == httplib::ws::Timeout)
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
