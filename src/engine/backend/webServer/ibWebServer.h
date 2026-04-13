#ifndef _IB_WEB_SERVER_H__
#define _IB_WEB_SERVER_H__

#include "backend.h"

#include <thread>
#include <atomic>
#include <chrono>
#include <memory>

namespace httplib { class Server; }

class BACKEND_API ibWebServer {
public:

	static bool Initialize(int port = 8765, const wxString& staticDir = wxEmptyString);
	static void Destroy();
	static ibWebServer* Get() { return ms_instance; }

	void Start();
	void Stop();
	void WaitForShutdown();

	bool IsRunning() const { return m_running.load(); }
	int GetPort() const { return m_port; }

private:

	ibWebServer(int port, const wxString& staticDir);
	~ibWebServer();

	void RegisterRoutes();

	std::unique_ptr<httplib::Server> m_server;
	std::thread m_serverThread;
	std::atomic<bool> m_running;
	int m_port;
	wxString m_staticDir;
	std::chrono::steady_clock::time_point m_startTime;

	static ibWebServer* ms_instance;
};

#define webServer (ibWebServer::Get())

#endif // _IB_WEB_SERVER_H__
