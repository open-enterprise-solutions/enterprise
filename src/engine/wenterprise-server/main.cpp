// OES Web Runtime — standalone HTTP server
//
// Step 3: brings up appData via wfrontendInit{File,Server}, sprinkles
// in wx initialisation for the backend, and exposes the same three
// smoke-test endpoints (/, /ping, /login, /logout) on top of a real
// database connection.

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

// wx first so its ssize_t alias is picked up before cpp-httplib's own
// typedef, otherwise MSVC sees two incompatible ssize_t definitions.
#include <wx/app.h>
#include <wx/init.h>
#include <wx/socket.h>
#include <wx/types.h>

// Trick cpp-httplib into skipping its own ssize_t typedef when it comes
// after wx. The macro is recognised by the header's MSVC guard.
#if defined(_WIN32) && !defined(_SSIZE_T_DEFINED)
#	define _SSIZE_T_DEFINED
#endif

#include "../../3rdparty/cpp-httplib/httplib.h"
#include "wfrontend.h"
#include "../../3rdparty/nlohmann/json.hpp"
#include "backend/backend_exception.h"
#include "backend/databaseLayer/databaseLayerException.h"
#include "frontend/diagnostics/oesConsole.h"

#if defined(_WIN32)
#	include <winsock2.h>
#	include <ws2tcpip.h>
#	include <windows.h>
#else
#	include <csignal>
#endif

namespace {

// File-scope handle to the running server so console-ctrl / signal handlers
// can break listen_after_bind cleanly. Once svr.stop() returns, main falls
// through to wfrontendShutdown which drops the process's sys_session row.
// Without this, a console window close / Ctrl+C would kill the process
// mid-listen and leave the row around until another wes sweep cleans it.
httplib::Server* g_svr = nullptr;

#if defined(_WIN32)
void LogShutdownLine(const char* msg) {
	// Goes to both stderr (visible while console still exists) and the
	// session-diag log (survives after console-close, where stderr vanishes).
	std::cerr << msg << std::endl;
	if (FILE* f = std::fopen("F:/projects/oes-work/session-diag.log", "a")) {
		std::fprintf(f, "[wes pid=%ld] %s\n", (long)::GetCurrentProcessId(), msg);
		std::fclose(f);
	}
}

BOOL WINAPI ConsoleCtrlHandler(DWORD dwCtrlType) {
	switch (dwCtrlType) {
	case CTRL_C_EVENT:
	case CTRL_BREAK_EVENT:
		// Console still live — main has time to clean up after
		// listen_after_bind returns. Just break the listener.
		LogShutdownLine("[ctrl] Ctrl+C/Break — svr.stop() + let main cleanup");
		if (g_svr) g_svr->stop();
		return TRUE;

	case CTRL_CLOSE_EVENT:
	case CTRL_LOGOFF_EVENT:
	case CTRL_SHUTDOWN_EVENT:
		// Windows force-terminates the process in 5s once this handler
		// returns. listen_after_bind sometimes doesn't unblock on stop()
		// fast enough on Windows (accept() on a closed socket doesn't
		// always return quickly). Run the cleanup synchronously inside
		// the handler so the sys_session DELETE reaches the DB before
		// Windows kills us, then ExitProcess instead of returning.
		LogShutdownLine("[ctrl] close event — direct in-handler shutdown");
		if (g_svr) g_svr->stop();
		wfrontendShutdown();
		LogShutdownLine("[ctrl] shutdown complete, ExitProcess(0)");
		ExitProcess(0);
		return TRUE;

	default:
		return FALSE;
	}
}
#else
void PosixSignalHandler(int /*sig*/) {
	if (g_svr) g_svr->stop();
}
#endif


struct CmdArgs {
	// HTTP bind. `port = 0` means "let the OS pick any free port"
	// (bind_to_any_port). Operator sees the actual bound port in the
	// startup banner. Explicit `--port=N` forces N; conflict kills
	// startup rather than silently coexisting on the same port.
	std::string host = "0.0.0.0";
	int         port = 0;

	// Optional: write a tiny manifest (one key=value per line) with the
	// resolved host / port / url once bind settles. Designer's
	// "Start debugging → Web client" spawns wes with --port=0 and polls
	// this file to learn which port the OS actually picked, so the
	// follow-up wxLaunchDefaultBrowser hits the right URL.
	std::string manifest;

	// Database connection — either file or server.
	std::string file;                 // --file=path.fdb
	std::string server;               // --server=host
	std::string dbPort = "3050";      // --dbport
	std::string srvUser;              // --user
	std::string srvPassword;          // --password
	std::string database;             // --db

	// IB (configuration) user credentials used for the backend's own
	// system session — i.e. the service account submitted to
	// session->Open() at wes startup. Empty in file mode: appData uses
	// the embedded-engine auth and the sys_user check only kicks in when
	// the configuration explicitly has user accounts.
	std::string ibUser;
	std::string ibPassword;

	std::string locale     = "en";

	// URL prefix: defaults to file base name or --db name.
	std::string urlPrefix;

	// --debug flag: enables the in-process debug server so designer can
	// attach via TCP and step through scripts running in this wes
	// (technical session + every per-tab WebClient session). Off by
	// default — production wes runs without a listener.
	bool debugEnable = false;
};

bool StartsWith(const std::string& s, const char* prefix)
{
	const std::size_t n = std::strlen(prefix);
	return s.size() >= n && s.compare(0, n, prefix) == 0;
}

CmdArgs ParseArgs(int argc, char** argv)
{
	CmdArgs a;
	for (int i = 1; i < argc; ++i) {
		const std::string arg(argv[i]);
		if      (StartsWith(arg, "--host="))      a.host        = arg.substr(7);
		else if (StartsWith(arg, "--port="))      a.port        = std::atoi(arg.substr(7).c_str());
		else if (StartsWith(arg, "--file="))      a.file        = arg.substr(7);
		else if (StartsWith(arg, "--server="))    a.server      = arg.substr(9);
		else if (StartsWith(arg, "--dbport="))    a.dbPort      = arg.substr(9);
		else if (StartsWith(arg, "--user="))      a.srvUser     = arg.substr(7);
		else if (StartsWith(arg, "--password="))  a.srvPassword = arg.substr(11);
		else if (StartsWith(arg, "--db="))        a.database    = arg.substr(5);
		else if (StartsWith(arg, "--ibuser="))    a.ibUser      = arg.substr(9);
		else if (StartsWith(arg, "--ibpwd="))     a.ibPassword  = arg.substr(8);
		else if (StartsWith(arg, "--locale="))    a.locale      = arg.substr(9);
		else if (StartsWith(arg, "--url="))       a.urlPrefix   = arg.substr(6);
		else if (StartsWith(arg, "--manifest="))  a.manifest    = arg.substr(11);
		else if (arg == "--debug")                a.debugEnable = true;
		else if (arg == "--help" || arg == "-h") {
			std::cout <<
				"Usage: wenterprise-server [options]\n"
				"  --host=<addr>       HTTP bind address (default 0.0.0.0)\n"
				"  --port=<N>          HTTP port (default 8080)\n"
				"  --url=<segment>     URL prefix segment after /w/ (default derived from --file / --db)\n"
				"\n"
				"  File-mode DB (choose one mode):\n"
				"    --file=<path>     Path to the configuration database file\n"
				"\n"
				"  Server-mode DB:\n"
				"    --server=<host>   DB server host\n"
				"    --dbport=<N>      DB server port (default 3050)\n"
				"    --user=<n>        DB server user\n"
				"    --password=<s>    DB server password\n"
				"    --db=<name>       Database name\n"
				"\n"
				"  Configuration (IB) credentials:\n"
				"    --ibuser=<n>      (default 'admin')\n"
				"    --ibpwd=<s>\n"
				"\n"
				"    --locale=<code>   (default 'en')\n";
			std::exit(0);
		}
	}

	// Default URL prefix from --db or --file basename.
	if (a.urlPrefix.empty()) {
		if (!a.database.empty()) {
			a.urlPrefix = a.database;
		}
		else if (!a.file.empty()) {
			const auto slash = a.file.find_last_of("/\\");
			const std::string base = slash == std::string::npos ? a.file : a.file.substr(slash + 1);
			const auto dot = base.find_last_of('.');
			a.urlPrefix = dot == std::string::npos ? base : base.substr(0, dot);
		}
		else {
			a.urlPrefix = "default";
		}
	}
	return a;
}

std::string Field(const httplib::Request& req, const std::string& key)
{
	auto it = req.params.find(key);
	return it != req.params.end() ? it->second : std::string();
}

// Extract the session id preferred by the client. Web tabs generate
// their own random id in sessionStorage and forward it via the
// `X-OES-Session` header, so every browser tab — even multiple in
// the same browser — gets a distinct server session. Cookie lookup
// is the legacy fallback for non-tab-aware clients.
std::string SessionIdFromReq(const httplib::Request& req)
{
	const std::string header = req.get_header_value("X-OES-Session");
	if (!header.empty())
		return header;

	// <img src>, <script src>, <link href> and anchor navigations can't
	// attach custom headers — they carry only cookies. When a per-tab
	// endpoint (e.g. /tab/N/icon) is reachable via plain GET the client
	// appends ?sid=<tabSid>; fall back to that when the header is absent.
	if (req.has_param("sid")) {
		const std::string sid = req.get_param_value("sid");
		if (!sid.empty())
			return sid;
	}

	const std::string cookie = req.get_header_value("Cookie");
	const auto pos = cookie.find("oes_session=");
	if (pos == std::string::npos)
		return std::string();
	// oes_session is a dashed UUID (36 chars: 8-4-4-4-12). Slice up to
	// the next '; ' delimiter instead of assuming a fixed length — gives
	// us one parser whether the cookie is the first or the tail entry
	// and survives a future id-format change.
	const std::size_t start = pos + 12;
	const std::size_t end   = cookie.find(';', start);
	return (end == std::string::npos)
		? cookie.substr(start)
		: cookie.substr(start, end - start);
}

// Extract session id, or emit 401 "no session" and return false. Every
// session-bound endpoint does the same 2-line check; this factors it
// into one inline guard so handlers read as `if (!RequireSessionId…) return;`.
bool RequireSessionId(const httplib::Request& req, httplib::Response& res, std::string& id)
{
	id = SessionIdFromReq(req);
	if (!id.empty())
		return true;
	res.status = 401;
	res.set_content("no session\n", "text/plain");
	return false;
}

// Probe: is someone already accepting connections on host:port? cpp-httplib
// binds with SO_REUSEADDR on Windows, so a second wenterprise-server on the
// same port won't fail to bind — Windows happily round-robins incoming
// connections between the two listeners, so a request to /w/Configuration/
// can hit the fb_test251 server and get a 404. Fail fast before starting.
bool PortAlreadyListening(const std::string& host, int port)
{
#if defined(_WIN32)
	SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
	if (s == INVALID_SOCKET) return false;

	const char* addr = (host == "0.0.0.0" || host == "::") ? "127.0.0.1" : host.c_str();
	sockaddr_in sa{};
	sa.sin_family = AF_INET;
	sa.sin_port = htons(static_cast<u_short>(port));
	if (inet_pton(AF_INET, addr, &sa.sin_addr) != 1) {
		closesocket(s);
		return false;
	}
	// Short timeout — we only want to know whether a local listener
	// accepts the TCP handshake. Anything beyond ~200ms almost
	// certainly means nobody's there and we got caught in a firewall
	// drop; better to let listen() fail than to block startup.
	DWORD timeout = 500;
	setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
	setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));

	const int r = connect(s, reinterpret_cast<sockaddr*>(&sa), sizeof(sa));
	closesocket(s);
	return r == 0;
#else
	(void)host; (void)port;
	return false;
#endif
}

bool InitBackend(const CmdArgs& args)
{
	if (!args.file.empty()) {
		return wfrontendInitFile(args.file, args.ibUser, args.ibPassword, args.locale, args.debugEnable);
	}
	if (!args.server.empty()) {
		return wfrontendInitServer(args.server, args.dbPort,
			args.srvUser, args.srvPassword, args.database,
			args.ibUser, args.ibPassword, args.locale, args.debugEnable);
	}
	std::cerr << "Either --file=<path> or --server=<host> --db=<name> is required (see --help)."
		<< std::endl;
	return false;
}

} // namespace

#ifdef _WIN32
// Windows narrow argv is ANSI (CP125x / CP86x depending on locale) — any
// non-ASCII path or value would be mojibake-ed before wxString::FromUTF8
// ever sees it. Convert from the process's wide command line to UTF-8
// so the rest of the program sees clean UTF-8 std::strings.
#include <windows.h>
#include <shellapi.h>

static std::vector<std::string> g_utf8ArgvStore;
static std::vector<char*>       g_utf8ArgvPtrs;

static void BuildUtf8Argv(int& argc, char**& argv)
{
	LPWSTR cmdLine = GetCommandLineW();
	int wArgc = 0;
	LPWSTR* wArgv = CommandLineToArgvW(cmdLine, &wArgc);
	if (wArgv == nullptr) return;  // fall back to narrow argv from CRT

	g_utf8ArgvStore.reserve(wArgc);
	g_utf8ArgvPtrs.reserve(wArgc + 1);
	for (int i = 0; i < wArgc; ++i) {
		const int need = WideCharToMultiByte(CP_UTF8, 0, wArgv[i], -1,
			nullptr, 0, nullptr, nullptr);
		std::string s(need > 0 ? need - 1 : 0, '\0');
		if (need > 1) {
			WideCharToMultiByte(CP_UTF8, 0, wArgv[i], -1,
				s.data(), need, nullptr, nullptr);
		}
		g_utf8ArgvStore.push_back(std::move(s));
	}
	LocalFree(wArgv);

	for (auto& s : g_utf8ArgvStore) g_utf8ArgvPtrs.push_back(s.data());
	g_utf8ArgvPtrs.push_back(nullptr);
	argc = static_cast<int>(g_utf8ArgvStore.size());
	argv = g_utf8ArgvPtrs.data();
}
#endif

int main(int argc, char** argv)
{
#ifdef _WIN32
	BuildUtf8Argv(argc, argv);
#endif
	const CmdArgs args = ParseArgs(argc, argv);

	// wxInitializer + wxSocketBase::Initialize + ibCrashGuard::Install
	// in one shot. wes is headless — no wxApp, faults need the persistent
	// minidump + std::set_terminate + signal handlers from crashGuard so
	// they don't disappear silently with the process.
	ibOesConsoleBoot boot(wxT("wenterprise-server"), argc, argv);
	if (!boot.IsOk()) {
		std::cerr << "wxWidgets failed to initialise" << std::endl;
		return 1;
	}

	// Bind the HTTP server FIRST so we know the real port before the
	// backend comes up — InitBackend's call chain creates the
	// wenterprise-server's own ibSession (registry.Connect), and that
	// session stamps `sys_session.address` from wfrontendServerAddress().
	// If we called wfrontendSetServerAddress with args.port=0 (default
	// OS-picks-port mode) the row would land with "host:0" and designer
	// would show a bad URL. Two-phase bind solves this: determine the
	// port here, stamp the address, then bring up the backend.
	// URL prefix is whatever the operator / spawning process specified
	// via --url, --db, or --file basename (see ParseArgs). Consistent
	// between CLI and designer-spawn — designer passes --url=<name>
	// derived the same way.
	const std::string prefix = "/w/" + args.urlPrefix;

	httplib::Server svr;
	g_svr = &svr;
#if defined(_WIN32)
	SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
#else
	std::signal(SIGINT,  PosixSignalHandler);
	std::signal(SIGTERM, PosixSignalHandler);
#endif
	// TCP_NODELAY — disable Nagle/delayed-ACK on accepted sockets.
	// Default was OFF → localhost round-trip picked up ~200ms each.
	svr.set_tcp_nodelay(true);
	// Disable keep-alive: serve one request per TCP connection. The
	// browser's 2s poll hit a cpp-httplib keep-alive issue on Windows
	// where the server thread's blocking select() doesn't wake up on
	// a new request from the same connection — every poll tick waited
	// the full read_timeout (5s→30s) before returning. Cold connect
	// on 127.0.0.1 is ~200ms; that's the price we pay for now, but
	// it's bounded and consistent versus the 5-30s stall.
	svr.set_keep_alive_max_count(1);
	// Short read/write timeouts are fine once keep-alive is off —
	// each connection is single-request, so the read-idle window
	// doesn't exist.
	svr.set_read_timeout(5);
	svr.set_write_timeout(5);

	// Top-level exception handler — catches anything that escapes one
	// of the per-route lambdas below. Pre-2026-05-26 the lambdas had
	// no defensive wrap and an ibBackendException unwinding from a
	// session-bound script handler crashed the wes process (cpp-httplib
	// swallows raw escapes but our session state is left half-torn).
	// With ThrowDatabaseException now actually throwing, the surface
	// of paths that can reach here grew — every DB call inside a
	// handler can now produce a structured exception. We surface a
	// JSON 500 with the most-specific kind/code we can extract so
	// client diagnostics (and admin dashboards) see something useful
	// instead of the generic empty cpp-httplib body.
	svr.set_exception_handler([](const httplib::Request& /*req*/,
	                              httplib::Response& res,
	                              std::exception_ptr ep) {
		std::string body;
		try { std::rethrow_exception(ep); }
		catch (const ibDatabaseLayerException& e) {
			body = std::string("{\"error\":\"database\",\"code\":")
				+ std::to_string(e.GetDriverErrorCode())
				+ ",\"sqlstate\":\"" + e.GetSqlState().ToUTF8().data()
				+ "\",\"retryable\":" + (e.IsRetryable() ? "true" : "false")
				+ ",\"message\":" + nlohmann::json(e.GetErrorDescription().ToUTF8().data()).dump()
				+ "}";
		}
		catch (const ibBackendDatabaseException& e) {
			body = std::string("{\"error\":\"database\",\"retryable\":")
				+ (e.IsRetryable() ? "true" : "false")
				+ ",\"message\":" + nlohmann::json(e.GetErrorDescription().ToUTF8().data()).dump()
				+ "}";
		}
		catch (const ibBackendException& e) {
			body = std::string("{\"error\":\"backend\",\"message\":")
				+ nlohmann::json(e.GetErrorDescription().ToUTF8().data()).dump()
				+ "}";
		}
		catch (const std::exception& e) {
			body = std::string("{\"error\":\"internal\",\"message\":")
				+ nlohmann::json(e.what()).dump()
				+ "}";
		}
		catch (...) {
			body = "{\"error\":\"unknown\"}";
		}
		res.status = 500;
		res.set_content(body, "application/json; charset=utf-8");
	});

	svr.Get(prefix + "/", [prefix](const httplib::Request& req, httplib::Response& res) {
		// Windows IPv6-first "localhost" fallback adds ~200ms cold
		// connect per request, which stacks up on every tab switch /
		// click. If the browser came in via `Host: localhost...`,
		// bounce it to the same URL with `127.0.0.1` — an explicit
		// IPv4 literal skips the ::1 try. One-time 302 on GET /; the
		// new origin then services all subsequent resource fetches
		// with millisecond connect times.
		const std::string host = req.get_header_value("Host");
		if (host.size() >= 9 && host.compare(0, 9, "localhost") == 0
			&& (host.size() == 9 || host[9] == ':')) {
			std::string target = "http://127.0.0.1" + host.substr(9) + prefix + "/";
			res.status = 302;
			res.set_header("Location", target);
			return;
		}

		// F5/reload: keep the existing session if the cookie is still
		// valid — destroying in-line here races with the parallel polls
		// the browser fires on the same reload (e.g. /active landing on
		// the stale cookie while GET / is still processing the destroy),
		// which produced intermittent "Pick a form on the left" flashes
		// and occasional server crashes. Exception: when the existing
		// session has zero tabs (user closed them all), swap it for a
		// fresh one so OnStart re-runs and default forms re-appear —
		// otherwise F5 on an empty session just re-shows the placeholder
		// forever.
		// Tab-local id: client's X-OES-Session header (sessionStorage)
		// wins, cookie is the legacy fallback. If the tab has a
		// sessionStorage id that the server doesn't know yet (fresh
		// tab opened in an existing browser), CreateSessionWithId
		// registers it.
		std::string id = SessionIdFromReq(req);
		if (id.empty()) {
			id = wfrontendCreateSession();
			std::cerr << "[GET /] created new session id=" << id << std::endl;
		}
		else if (wfrontendSessionExists(id)) {
			const std::size_t tc = wfrontendSessionTabCount(id);
			std::cerr << "[GET /] reusing session id=" << id
				<< " tabCount=" << tc << std::endl;
			if (tc == 0) {
				wfrontendDestroySession(id);
				std::cerr << "[GET /] destroyed empty session, re-minting" << std::endl;
				id = wfrontendCreateSessionWithId(id);
			}
		}
		else {
			std::cerr << "[GET /] session id=" << id << " unknown — creating" << std::endl;
			id = wfrontendCreateSessionWithId(id);
		}

		// No HttpOnly: harmless here and makes DevTools inspection easier.
		res.set_header("Set-Cookie", "oes_session=" + id + "; Path=/");
		// HTML is generated in-binary; force the browser to pick up
		// freshly rebuilt server output instead of serving a stale
		// cached copy (common when iterating on the inline JS).
		res.set_header("Cache-Control", "no-store, no-cache, must-revalidate");

		// Client HTML/CSS/JS lives in wfrontend.dll so Apache/IIS/CGI
		// hosts can serve the same bytes from a single source of truth.
		res.set_content(wfrontendClientHTML(), "text/html; charset=utf-8");
	});

	svr.Get(prefix + "/ping", [](const httplib::Request&, httplib::Response& res) {
		res.set_content("pong", "text/plain");
	});

	svr.Get(prefix + "/menu", [](const httplib::Request&, httplib::Response& res) {
		res.set_content(wfrontendMenuJSON(), "application/json; charset=utf-8");
	});

	svr.Get(prefix + "/forms", [](const httplib::Request&, httplib::Response& res) {
		res.set_content(wfrontendFormsJSON(), "application/json; charset=utf-8");
	});

	svr.Get(prefix + "/diag/ctors", [](const httplib::Request&, httplib::Response& res) {
		res.set_content(wfrontendCtorsJSON(), "application/json; charset=utf-8");
	});

	svr.Get(prefix + "/session", [](const httplib::Request& req, httplib::Response& res) {
		const std::string id = SessionIdFromReq(req);
		if (id.empty()) {
			res.set_content(R"({"authenticated":false,"tabs":[]})",
				"application/json; charset=utf-8");
			return;
		}
		res.set_content(wfrontendSessionInfoJSON(id),
			"application/json; charset=utf-8");
	});

	// POST /action/<controlID>: fire the script event bound to the
	// control (e.g., a button's OnButtonPressed). Returns the updated
	// form JSON — the host is re-walked after the script runs, so
	// property changes / UpdateForm calls surface in the response.
	svr.Post(prefix + R"(/action/(\d+))", [](const httplib::Request& req, httplib::Response& res) {
		std::string id;
		if (!RequireSessionId(req, res, id)) return;
		const int controlID = std::atoi(req.matches[1].str().c_str());
		res.set_content(wfrontendFireAction(id, controlID),
			"application/json; charset=utf-8");
	});

	// POST /fire/<controlID>/<kind> — generic kind-aware dispatch.
	// kind is routed into ibWebWindow::HandleRequest on the target
	// control (e.g. "buttonSelect"/"buttonOpen"/"buttonClear" for a
	// textctrl's side buttons). Returns the updated form JSON like
	// /action. The /action endpoint stays for backward compatibility
	// and is equivalent to /fire/<id>/click.
	svr.Post(prefix + R"(/fire/(\d+)/(\w+))", [](const httplib::Request& req, httplib::Response& res) {
		std::string id;
		if (!RequireSessionId(req, res, id)) return;
		const int controlID = std::atoi(req.matches[1].str().c_str());
		const std::string kind = req.matches[2].str();
		res.set_content(wfrontendFireKind(id, controlID, kind),
			"application/json; charset=utf-8");
	});

	// POST /change/<controlID> value=<utf8 text>  — commit a textctrl
	// edit (sent on blur/Enter from the browser). Returns the updated
	// form JSON — server coerces through the backing ibValue type and
	// fires OnChange; re-walk surfaces downstream script mutations.
	svr.Post(prefix + R"(/change/(\d+))", [](const httplib::Request& req, httplib::Response& res) {
		std::string id;
		if (!RequireSessionId(req, res, id)) return;
		const int controlID = std::atoi(req.matches[1].str().c_str());
		const std::string value = req.get_param_value("value");
		res.set_content(wfrontendFireTextChange(id, controlID, value),
			"application/json; charset=utf-8");
	});

	// POST /toggle/<controlID> checked=0|1  — toggle a checkbox. Fires
	// wxEVT_CHECKBOX; the shared OnClickedCheckbox handler reads the
	// new state from event.GetInt() and runs the OnCheckboxClicked
	// script. Returns the updated form JSON.
	svr.Post(prefix + R"(/toggle/(\d+))", [](const httplib::Request& req, httplib::Response& res) {
		std::string id;
		if (!RequireSessionId(req, res, id)) return;
		const int controlID = std::atoi(req.matches[1].str().c_str());
		const std::string checkedStr = req.get_param_value("checked");
		const bool checked = (checkedStr == "1" || checkedStr == "true");
		res.set_content(wfrontendFireToggle(id, controlID, checked),
			"application/json; charset=utf-8");
	});

	// POST /modal-reply/<modalID> result=<int> — resolve a backend-side
	// ShowModalMessage with the chosen wx button code. modalID is the
	// id surfaced in /session JSON; result is wxOK / wxYES / wxNO /
	// wxCANCEL (or 0 for "no choice / Esc"). Replies with "1" on
	// success, "0" if the modal id is no longer queued.
	svr.Post(prefix + R"(/modal-reply/([\w-]+))",
		[](const httplib::Request& req, httplib::Response& res) {
		std::string id;
		if (!RequireSessionId(req, res, id)) return;
		const std::string modalId = req.matches[1].str();
		const int result = std::atoi(req.get_param_value("result").c_str());
		const bool ok = wfrontendModalReply(id, modalId, result);
		res.set_content(ok ? "1" : "0", "text/plain");
	});

	// GET /functions — returns the "All functions" tree (groups of
	// metadata objects). Mirrors desktop's ibDialogFunctionAll content.
	// Process-wide, no session needed (metadata is shared) — but the
	// access check inside uses activeMetaData->AccessRight_ModeAllFunction
	// which routes through the current user's roles. Requires session
	// for the role context.
	svr.Get(prefix + "/functions", [](const httplib::Request& req, httplib::Response& res) {
		std::string id;
		if (!RequireSessionId(req, res, id)) return;
		res.set_content(wfrontendAllFunctionsJSON(),
			"application/json; charset=utf-8");
	});

	// GET /interfaces — sidebar navigation: top-level metadata Interfaces
	// (subsystems) plus their default-section items. Web sidebar shows
	// these as collapsible sections; empty list means the configuration
	// has no interfaces (sidebar stays with the plain FORMS list).
	svr.Get(prefix + "/interfaces", [](const httplib::Request& req, httplib::Response& res) {
		std::string id;
		if (!RequireSessionId(req, res, id)) return;
		res.set_content(wfrontendInterfacesJSON(),
			"application/json; charset=utf-8");
	});

	// POST /open-meta/<metaID> [cmd=<ibInterfaceCommandType>] — open the
	// form for a metadata object by its id. cmd defaults to 100 (Default,
	// list/browse). Pass 150 from a Create-section item to land in the
	// new-record flow, 151 for List, 152 for Select. Returns active host
	// JSON so the client refreshes in one round-trip.
	svr.Post(prefix + R"(/open-meta/(\d+))",
		[](const httplib::Request& req, httplib::Response& res) {
		std::string id;
		if (!RequireSessionId(req, res, id)) return;
		const int metaID = std::atoi(req.matches[1].str().c_str());
		const std::string cmdStr = req.get_param_value("cmd");
		const int cmdType = cmdStr.empty() ? 100 : std::atoi(cmdStr.c_str());
		res.set_content(wfrontendOpenMetaObject(id, metaID, cmdType),
			"application/json; charset=utf-8");
	});

	// GET /active: read-only snapshot of the current active tab's
	// visual host. The browser poll loop uses this to pick up
	// timer-driven state changes between user actions — no rebuild,
	// no dispatch, just the current tree as JSON.
	svr.Get(prefix + "/active", [](const httplib::Request& req, httplib::Response& res) {
		std::string id;
		if (!RequireSessionId(req, res, id)) return;
		// 401 when the session ID is unknown (server restart, idle-timeout
		// eviction, or explicit destroy). Client's poll loop turns this
		// into a "session lost" banner; without the distinct 401 the
		// empty "{}" body would be indistinguishable from a session with
		// no active host.
		if (!wfrontendSessionExists(id)) {
			res.status = 401;
			res.set_content("session lost\n", "text/plain");
			return;
		}
		res.set_content(wfrontendActiveHostJSON(id),
			"application/json; charset=utf-8");
	});

	// GET /stream: Server-Sent Events. Browser opens one long-lived
	// EventSource connection per tab; server pushes `data: <json>` frames
	// whenever ibWebApplication::MarkDirty bumps the session's sequence
	// (Dispatch, timer tick, tab switch/close, CreateAndUpdateVisualHost
	// from script). Replaces the 2s polling loop — UI updates land within
	// tens of ms of the state change instead of 0–2s polling jitter. A
	// heartbeat (`: ping\n\n`) is emitted on idle timeout to keep proxies
	// from collapsing the connection. The polling loop stays on the
	// client as a fallback when EventSource is unavailable or the SSE
	// connection errors.
	svr.Get(prefix + "/stream", [](const httplib::Request& req, httplib::Response& res) {
		const std::string sid = SessionIdFromReq(req);
		if (sid.empty()) { res.status = 401; res.set_content("no session\n","text/plain"); return; }

		// Proxy-buffering hints — nginx sees X-Accel-Buffering, Apache
		// mod_proxy respects Cache-Control: no-cache plus the flushed
		// chunks. Without these, intermediaries can accumulate bytes
		// and delay delivery until the buffer fills, defeating SSE's
		// low-latency promise.
		res.set_header("Cache-Control", "no-cache");
		res.set_header("X-Accel-Buffering", "no");

		// State carried across the provider's invocations: the last
		// sequence this client acknowledged. shared_ptr because the
		// provider closure is copied by value when httplib stashes it.
		auto lastSeen = std::make_shared<std::uint64_t>(0);

		res.set_chunked_content_provider(
			"text/event-stream; charset=utf-8",
			[sid, lastSeen](size_t /*offset*/, httplib::DataSink& sink) -> bool {
				if (!sink.is_writable())
					return false;

				// Block up to 25s for the session's seq to advance past
				// lastSeen. Timeout = we send a heartbeat so the peer
				// knows we're alive and intermediaries don't close us.
				const auto upd = wfrontendLiveWait(sid, *lastSeen, 25000);

				if (upd.seq == *lastSeen) {
					static const char kPing[] = ": ping\n\n";
					return sink.write(kPing, sizeof(kPing) - 1);
				}
				*lastSeen = upd.seq;

				// SSE frame: "data: <json>\n\n". JSON here is pretty-
				// printed (embedded newlines); spec allows multi-line
				// data by prefixing every line with "data: ".
				std::string body;
				body.reserve(upd.json.size() + 32);
				body += "data: ";
				for (char c : upd.json) {
					body += c;
					if (c == '\n') body += "data: ";
				}
				body += "\n\n";
				return sink.write(body.data(), body.size());
			});
	});

	// POST /tab/<i>: switch the session's active tab to index <i> and
	// return the new active host's JSON so the browser can swap body.
	svr.Post(prefix + R"(/tab/(\d+))", [](const httplib::Request& req, httplib::Response& res) {
		std::string id;
		if (!RequireSessionId(req, res, id)) return;
		const int tabIndex = std::atoi(req.matches[1].str().c_str());
		res.set_content(wfrontendActivateTab(id, tabIndex),
			"application/json; charset=utf-8");
	});

	// GET /tab/<i>/icon: PNG bytes of the tab's icon. Used by the
	// browser <img> in the tab strip. 404 when the session / tab /
	// icon is missing so the client can fall back to text-only.
	svr.Get(prefix + R"(/tab/(\d+)/icon)", [](const httplib::Request& req, httplib::Response& res) {
		std::string id;
		if (!RequireSessionId(req, res, id)) return;
		const int tabIndex = std::atoi(req.matches[1].str().c_str());
		std::string png = wfrontendTabIconPNG(id, tabIndex);
		if (png.empty()) { res.status = 404; return; }
		// PNG bytes are stable per tab ctor, so a short cache keeps
		// refreshTabs from re-fetching the same image on every poll.
		res.set_header("Cache-Control", "max-age=300");
		res.set_content(std::move(png), "image/png");
	});

	// DELETE /tab/<i>: close that tab. Returns the new active host's
	// JSON, or "{}" if no tabs remain.
	svr.Delete(prefix + R"(/tab/(\d+))", [](const httplib::Request& req, httplib::Response& res) {
		std::string id;
		if (!RequireSessionId(req, res, id)) return;
		const int tabIndex = std::atoi(req.matches[1].str().c_str());
		std::cerr << "[HTTP] DELETE /tab/" << tabIndex
			<< " session=" << id
			<< " UA=" << req.get_header_value("User-Agent").substr(0, 40)
			<< std::endl;
		res.set_content(wfrontendCloseTab(id, tabIndex),
			"application/json; charset=utf-8");
	});

	// /form/<metaID>: open the form with the given metadata id inside
	// this session and return its ibWebWindow subtree as JSON. metaID
	// comes from /menu (e.g. Catalog1 id=1030 there). Requires the
	// session to be authenticated — form creation runs the session's
	// module manager and touches appData state.
	svr.Get(prefix + R"(/form/(\d+))", [](const httplib::Request& req, httplib::Response& res) {
		const std::string id = SessionIdFromReq(req);
		if (id.empty()) {
			res.status = 401;
			res.set_content("no session — GET / first\n", "text/plain");
			return;
		}
		const int metaID = std::atoi(req.matches[1].str().c_str());
		res.set_content(wfrontendOpenForm(id, metaID),
			"application/json; charset=utf-8");
	});

	// GET /debug-status — process-wide debug flag (was --debug on?) +
	// per-session paused flag (is the session currently parked at a
	// breakpoint?). Client polls this on its live tick to render the
	// top-right "Debug" / "Paused" badge.
	svr.Get(prefix + "/debug-status",
		[](const httplib::Request& req, httplib::Response& res) {
			const std::string sid = SessionIdFromReq(req);
			const bool dbg    = wfrontendDebugMode();
			const bool paused = !sid.empty() && wfrontendSessionPaused(sid);
			res.set_content(
				std::string("{\"debugMode\":") + (dbg ? "true" : "false") +
				",\"paused\":" + (paused ? "true" : "false") + "}",
				"application/json; charset=utf-8");
		});

	// GET /auth-info — whether the configuration has sys_user rows.
	// Client uses it on boot to decide between auto-login (open access)
	// and showing a credentials form. No session required — purely
	// metadata about the target configuration.
	svr.Get(prefix + "/auth-info",
		[](const httplib::Request&, httplib::Response& res) {
			res.set_content(
				std::string("{\"hasUsers\":") +
				(wfrontendHasUsers() ? "true" : "false") + "}",
				"application/json; charset=utf-8");
		});

	svr.Post(prefix + "/login", [](const httplib::Request& req, httplib::Response& res) {
		const std::string user = Field(req, "user");
		const std::string pass = Field(req, "password");
		// Empty user is OK when the configuration has no sys_user rows
		// (anonymous / default-user auto-login). When users ARE present,
		// an empty user just won't find a match and login fails later.
		if (user.empty() && wfrontendHasUsers()) {
			res.status = 400;
			res.set_content("missing 'user' field", "text/plain");
			return;
		}

		std::string id = SessionIdFromReq(req);
		if (id.empty()) {
			res.status = 401;
			res.set_content("no session — GET / first to obtain one\n", "text/plain");
			return;
		}
		// Tab's sessionStorage id (X-OES-Session header) is generated by
		// JS independently from the server's cookie — on a fresh tab the
		// first XHR can arrive with an id the server has never registered.
		// Mirror GET /'s create-on-unknown behaviour so POST /login doesn't
		// 401 just because the tab happened to beat GET / in registering.
		if (!wfrontendSessionExists(id))
			id = wfrontendCreateSessionWithId(id);

		if (!wfrontendLogin(id, user, pass)) {
			res.status = 401;
			res.set_content("login failed\n", "text/plain");
			return;
		}
		res.set_content("ok (" + id + " / " + user + ")\n", "text/plain");
	});

	svr.Post(prefix + "/logout", [](const httplib::Request& req, httplib::Response& res) {
		const std::string id = SessionIdFromReq(req);
		if (!id.empty())
			wfrontendDestroySession(id);
		res.set_header("Set-Cookie", "oes_session=; Path=/; Max-Age=0");
		res.set_content("bye\n", "text/plain");
	});

	// GET /admin/diag — JSON snapshot of pool / worker / session state.
	// Cheap: reads atomic counters + the registry's last-refreshed
	// snapshot. Designed for monitoring agents and the Active Users
	// dialog; callable freely without rate-limiting at typical loads.
	svr.Get(prefix + "/admin/diag",
		[](const httplib::Request&, httplib::Response& res) {
			res.set_content(wfrontendDiagJSON(), "application/json");
		});

	// POST /admin/sessions/<guid>/kick — write "kick" into
	// sys_session.signal for the row matching <guid>. The owning
	// wes process sees it on its next JobCheckSignal tick (~3s) and
	// tears the session down. Works cluster-wide because all wes
	// instances share the same sys_session table; the signal column
	// is the cross-process control channel.
	svr.Post(prefix + R"(/admin/sessions/([0-9a-fA-F\-]+)/kick)",
		[](const httplib::Request& req, httplib::Response& res) {
			const std::string guid = req.matches[1].str();
			const bool ok = wfrontendKickSessionByGuid(guid);
			res.status = ok ? 202 : 500;
			res.set_content(ok ? "queued\n" : "failed\n", "text/plain");
		});

	// POST /admin/sessions/<guid>/reload — write "reload" into signal.
	// Process-wide eviction on the wes that owns that row: every
	// web-client session it knows is Destroy'd, clients land on /login.
	// Guid just names WHICH process to poke; any of its owned rows
	// triggers the same fan-out.
	svr.Post(prefix + R"(/admin/sessions/([0-9a-fA-F\-]+)/reload)",
		[](const httplib::Request& req, httplib::Response& res) {
			const std::string guid = req.matches[1].str();
			const bool ok = wfrontendReloadSessionByGuid(guid);
			res.status = ok ? 202 : 500;
			res.set_content(ok ? "queued\n" : "failed\n", "text/plain");
		});

	// GET /admin/locks — JSON array of held sys_lock rows cluster-wide.
	// One entry per held lock; see wfrontendLocksJSON shape. Cheap to
	// call (single SELECT, no joins).
	svr.Get(prefix + "/admin/locks",
		[](const httplib::Request&, httplib::Response& res) {
			res.set_content(wfrontendLocksJSON(), "application/json");
		});

	// DELETE /admin/locks/<lockGuid> — admin force-release for the
	// named row. Use when a session went zombie before releasing, or
	// the user can't unblock through the normal UI. Returns 200 on
	// success, 500 on error (already-gone is success — best-effort).
	svr.Delete(prefix + R"(/admin/locks/([0-9a-fA-F\-]+))",
		[](const httplib::Request& req, httplib::Response& res) {
			const std::string guid = req.matches[1].str();
			const bool ok = wfrontendForceReleaseLockByGuid(guid);
			res.status = ok ? 200 : 500;
			res.set_content(ok ? "released\n" : "failed\n", "text/plain");
		});

	std::cout << wfrontendVersion() << std::endl;

	// Two-phase bind — we need the real port BEFORE InitBackend so
	// the technical session the backend creates stamps
	// sys_session.address with the true "host:boundPort" string.
	// bind_to_any_port returns the OS-picked port when args.port==0;
	// bind_to_port takes an explicit one.
	int boundPort = 0;
	if (args.port == 0) {
		boundPort = svr.bind_to_any_port(args.host.c_str());
		if (boundPort == 0) {
			std::cerr << "Failed to bind to any free port on " << args.host << std::endl;
			return 1;
		}
	}
	else {
		// Explicit --port=N. On Windows cpp-httplib binds with SO_REUSEADDR
		// so a second listener wouldn't fail here; the operator asked for a
		// specific port, so don't silently drift. Pre-check via our probe.
		if (PortAlreadyListening(args.host, args.port)) {
			std::cerr << "Port " << args.port << " is already in use on " << args.host
			          << ". Drop --port to let the OS pick a free one, or pass another --port=N."
			          << std::endl;
			return 1;
		}
		if (!svr.bind_to_port(args.host.c_str(), args.port)) {
			std::cerr << "Failed to bind " << args.host << ":" << args.port << std::endl;
			return 1;
		}
		boundPort = args.port;
	}

	// Stamp address with the real port BEFORE InitBackend so the
	// technical-session INSERT records "host:boundPort" in sys_session.address.
	wfrontendSetServerAddress(args.host, boundPort);

	// AccessMode (Server) is set by appData's ctor inside InitBackend
	// based on the eWEB_ENTERPRISE_MODE runMode.
	if (!InitBackend(args)) {
		const std::string err = wfrontendLastError();
		std::cerr << "Failed to open the database";
		if (!err.empty()) std::cerr << ": " << err;
		std::cerr << std::endl;
		return 1;
	}

	// Unify backend's ForceExit() with wes's main loop: instead of
	// posting to wxTheApp (which has no event loop in the httplib
	// blocking-listen path), call svr.stop() so listen_after_bind
	// returns and main proceeds to wfrontendShutdown — same orderly
	// path as Ctrl+C.
	wfrontendSetProcessExitHook([]() {
		if (g_svr) g_svr->stop();
	});

	// 0.0.0.0 is a wildcard bind, not a routable address — browsers
	// can't connect to it. Advertise localhost so the URL is clickable
	// while still binding all interfaces.
	const std::string displayHost =
		(args.host == "0.0.0.0" || args.host == "::") ? "localhost" : args.host;
	std::cout << "OES wenterprise-server listening on http://"
		<< displayHost << ":" << boundPort << prefix << "/"
		<< "  (bind: " << args.host << ")" << std::endl;

	// Write the bind manifest (if requested) ONLY after InitBackend
	// succeeded and right before listen_after_bind runs. Otherwise
	// Designer's "Start debugging → Web client" opens the browser at
	// a URL the server hasn't yet started accepting (manifest
	// written during bind, but listen only runs after InitBackend) —
	// user sees "No connection". Placing the write here means designer
	// polls, sees it, opens the browser, and listen_after_bind below
	// is already serving.
	if (!args.manifest.empty()) {
		const std::string displayHostForManifest =
			(args.host == "0.0.0.0" || args.host == "::") ? "localhost" : args.host;
		if (FILE* f = fopen(args.manifest.c_str(), "w")) {
			fprintf(f, "host=%s\n",   args.host.c_str());
			fprintf(f, "port=%d\n",   boundPort);
			fprintf(f, "prefix=%s\n", prefix.c_str());
			fprintf(f, "url=http://%s:%d%s/\n",
				displayHostForManifest.c_str(), boundPort, prefix.c_str());
			fclose(f);
		}
	}

	const bool ok = svr.listen_after_bind();

#if defined(_WIN32)
	LogShutdownLine("[main] listen_after_bind returned, entering wfrontendShutdown");
#endif
	wfrontendShutdown();
#if defined(_WIN32)
	LogShutdownLine("[main] wfrontendShutdown returned");
#endif

	if (!ok) {
		std::cerr << "listen_after_bind failed on " << args.host << ":" << boundPort << std::endl;
		return 1;
	}
	return 0;
}
