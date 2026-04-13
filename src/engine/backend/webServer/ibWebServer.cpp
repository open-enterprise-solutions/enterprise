#include "ibWebServer.h"
#include "appData.h"

#include <json.hpp>

#define CPPHTTPLIB_THREAD_POOL_COUNT 8
#include <httplib.h>

using json = nlohmann::json;

ibWebServer* ibWebServer::ms_instance = nullptr;

//***********************************************************************
//*                         ibWebServer                                 *
//***********************************************************************

ibWebServer::ibWebServer(int port, const wxString& staticDir)
	: m_server(new httplib::Server())
	, m_running(false)
	, m_port(port)
	, m_staticDir(staticDir)
	, m_startTime(std::chrono::steady_clock::now())
{
	m_server->new_task_queue = [] {
		return new httplib::ThreadPool(8, 64);
	};
}

ibWebServer::~ibWebServer()
{
	Stop();
	delete m_server;
	m_server = nullptr;
}

bool ibWebServer::Initialize(int port, const wxString& staticDir)
{
	if (ms_instance != nullptr) {
		return false;
	}

	ms_instance = new ibWebServer(port, staticDir);
	ms_instance->RegisterRoutes();
	ms_instance->Start();

	return ms_instance->IsRunning();
}

void ibWebServer::Destroy()
{
	if (ms_instance != nullptr) {
		ms_instance->Stop();
		delete ms_instance;
		ms_instance = nullptr;
	}
}

void ibWebServer::RegisterRoutes()
{
	// CORS preflight
	m_server->Options(".*", [](const httplib::Request&, httplib::Response& res) {
		res.set_header("Access-Control-Allow-Origin", "*");
		res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
		res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
		res.status = 204;
	});

	// Post-routing: add CORS headers to all responses
	m_server->set_post_routing_handler([](const httplib::Request&, httplib::Response& res) {
		res.set_header("Access-Control-Allow-Origin", "*");
	});

	// GET /api/health
	m_server->Get("/api/health", [this](const httplib::Request&, httplib::Response& res) {
		auto now = std::chrono::steady_clock::now();
		auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now - m_startTime).count();

		json response;
		response["status"] = "ok";
		response["version"] = "0.1.0";
		response["uptime"] = uptime;
		response["port"] = m_port;

		if (appData != nullptr) {
			response["mode"] = appData->ServiceMode() ? "service" : "enterprise";
			response["database"] = db_query != nullptr ? "connected" : "disconnected";
		}
		else {
			response["mode"] = "standalone";
			response["database"] = "disconnected";
		}

		res.set_content(response.dump(), "application/json");
	});

	// Static file serving
	if (!m_staticDir.IsEmpty()) {
		std::string dir = m_staticDir.ToStdString();
		m_server->set_mount_point("/", dir);
	}

	// Exception handler
	m_server->set_exception_handler([](const httplib::Request&, httplib::Response& res, std::exception_ptr ep) {
		json error;
		try {
			std::rethrow_exception(ep);
		}
		catch (const std::exception& e) {
			error["error"] = e.what();
		}
		catch (...) {
			error["error"] = "Unknown error";
		}
		res.status = 500;
		res.set_content(error.dump(), "application/json");
	});

	// Logger
	m_server->set_logger([](const httplib::Request& req, const httplib::Response& res) {
		wxLogMessage(wxT("[HTTP] %s %s -> %d"),
			wxString(req.method), wxString(req.path), res.status);
	});
}

void ibWebServer::Start()
{
	if (m_running.load()) {
		return;
	}

	m_running.store(true);

	m_serverThread = std::thread([this]() {
		if (!m_server->listen("0.0.0.0", m_port)) {
			m_running.store(false);
			wxLogError(wxT("Web server failed to start on port %d"), m_port);
		}
	});

	// Wait briefly to confirm startup
	std::this_thread::sleep_for(std::chrono::milliseconds(100));

	if (!m_server->is_running()) {
		m_running.store(false);
	}
}

void ibWebServer::Stop()
{
	if (!m_running.load()) {
		return;
	}

	m_server->stop();
	m_running.store(false);

	if (m_serverThread.joinable()) {
		m_serverThread.join();
	}
}

void ibWebServer::WaitForShutdown()
{
	if (m_serverThread.joinable()) {
		m_serverThread.join();
	}
}
