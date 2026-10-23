#include "webServer.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <thread>

#include <wx/file.h>
#include <wx/fileconf.h>
#include <wx/filename.h>
#include <wx/tokenzr.h>
#include <wx/utils.h>   // wxGetFullHostName — the server named in an address
#include <wx/wfstream.h>
#include <wx/zipstrm.h>

#ifdef __WXMSW__
#include <windows.h>   // SetConsoleCtrlHandler — the console window closing
#endif

#include "protocol/connectionFile.h"
#include "protocol/connectionServer.h"   // the oes:// address and the path of a base's clients — the protocol's
#include "protocol/protocolNode.h"       // the request a file base is opened by

#include "core/net/httpClient.h"
#include "core/net/webSocketFrame.h"
#include "core/net/webSocketRelay.h"
#include "core/fileSystem.h"   // ibDirExists

namespace {

// WHERE THE SERVER STANDS — one value, read by the main thread and the signals alike (appserver/appServer.cpp says
// why the console's closing waits for the stop).
enum class ibServeState { Serving, Stopping, Stopped };
std::atomic<ibServeState> s_state{ ibServeState::Serving };

void OnStopSignal(int)
{
	ibServeState serving = ibServeState::Serving;
	s_state.compare_exchange_strong(serving, ibServeState::Stopping);
}

#ifdef __WXMSW__
BOOL WINAPI OnConsoleClose(DWORD type)
{
	if (type != CTRL_CLOSE_EVENT && type != CTRL_LOGOFF_EVENT && type != CTRL_SHUTDOWN_EVENT)
		return FALSE;
	OnStopSignal(0);
	for (int waited = 0; waited < 4500 && s_state.load() != ibServeState::Stopped; waited += 50)
		::Sleep(50);
	return TRUE;
}
#endif

// How long a POST to an application server may wait for its answer — the call's work, until it settles.
constexpr std::chrono::seconds kPostWait(300);

std::string Utf8(const wxString& text)
{
	return std::string(text.utf8_str());
}

// A path of web.conf, made absolute against the web folder.
wxString Beside(const wxString& folder, const wxString& path)
{
	if (path.IsEmpty())
		return path;
	wxFileName name(path);
	if (name.IsRelative())
		name.MakeAbsolute(folder);
	return name.GetFullPath();
}

// A PEM file, whole; false with why.
bool ReadPem(const wxString& path, std::string& text, wxString& error)
{
	wxFile file;
	if (!ibFileExists(path) || !file.Open(path)) {
		error = wxString::Format(wxT("%s is not there or not readable"), path);
		return false;
	}
	const wxFileOffset length = file.Length();
	text.assign(static_cast<std::size_t>(length > 0 ? length : 0), '\0');
	if (length > 0 && file.Read(&text[0], static_cast<size_t>(length)) != length) {
		error = wxString::Format(wxT("%s could not be read"), path);
		return false;
	}
	return true;
}

// THE SITE PACKED — www.wpk, a zip of the site's folder (the build packs src/engine/webserver/www into it): each file
// by its path in the site, as an address names it; false with why.
bool ReadPack(const wxString& path, std::map<std::string, std::string>& files, wxString& error)
{
	wxFileInputStream input(path);
	wxZipInputStream zip(input, wxConvUTF8);
	if (!input.IsOk() || !zip.IsOk()) {
		error = wxString::Format(wxT("%s could not be read"), path);
		return false;
	}
	for (std::unique_ptr<wxZipEntry> entry(zip.GetNextEntry()); entry; entry.reset(zip.GetNextEntry())) {
		if (entry->IsDir())
			continue;
		// As the address has it, whoever packed it: a backslash of Windows' archiver, a leading ./ of CMake's.
		std::string name = Utf8(entry->GetInternalName());
		std::replace(name.begin(), name.end(), '\\', '/');
		if (name.compare(0, 2, "./") == 0)
			name.erase(0, 2);
		std::string data;
		char buffer[16 * 1024];
		while (zip.CanRead()) {
			zip.Read(buffer, sizeof(buffer));
			if (zip.LastRead() == 0)
				break;
			data.append(buffer, zip.LastRead());
		}
		files[name] = std::move(data);
	}
	if (files.empty()) {
		error = wxString::Format(wxT("%s holds no file"), path);
		return false;
	}
	return true;
}

// The browser asked for answers of some size deflated, as it connected — what the socket to an application server asks
// for it too, so they come through as they are.
bool WantsDeflate(const ibHttpRequest& request)
{
	return request.GetParameter("compress") == "deflate";
}

// A BROWSER'S SOCKET TO A FILE BASE — the base is the server here: the relay is the connection its clients log in
// through, told what the base says to them, and they leave with it. The text is the engine's both ways: the base's
// own, and the socket reads it and writes it (and deflates it, when the browser asked).
class ibFileRelay : public ibWebSocketHandler {
public:

	ibFileRelay(const ibProtocolFileBase& base, std::shared_ptr<ibWebSocket> socket) :
		m_base(base), m_socket(std::move(socket))
	{
		const std::weak_ptr<ibWebSocket> told = m_socket;
		m_base.Listen(this, [told](const ibString& text) {
			if (const std::shared_ptr<ibWebSocket> socket = told.lock())
				socket->Send(text);
		});
	}

	virtual void OnMessage(const ibString& text) override
	{
		ibString answer;
		wxString error;
		if (!m_base.Call(text, this, m_socket->GetRequest().m_address, answer, error)) {
			ibWebServerSay(ibJournalMark::Error, wxT("a browser's call did not reach its base: %s"), error);
			m_socket->Close(ibWebSocketClose::ServerError, "the base is not open");
			return;
		}
		if (!answer.IsEmpty())
			m_socket->Send(answer);
	}

	virtual void OnBinary(std::string) override
	{
		m_socket->Close(ibWebSocketClose::Unsupported, "the protocol speaks text");
	}

	virtual void OnClose() override
	{
		m_base.Disconnect(this);
	}

private:
	const ibProtocolFileBase&    m_base;
	std::shared_ptr<ibWebSocket> m_socket;
};

} // namespace

//*******************************************************************************************
//*                                      The config                                         *
//*******************************************************************************************

bool ibWebConfig::Read(const wxString& folder, wxString& error)
{
	m_folder = folder;
	m_root = Beside(folder, wxT("www"));
	const wxString path = folder + wxFileName::GetPathSeparator() + wxT("web.conf");
	if (!ibFileExists(path))
		return true;

	// Read as written: a Windows path keeps its backslashes.
	wxFileConfig conf(wxEmptyString, wxEmptyString, path, wxEmptyString,
		wxCONFIG_USE_LOCAL_FILE | wxCONFIG_USE_NO_ESCAPE_CHARACTERS);

	const auto port = [&conf, &path, &error](const wxString& key, std::optional<std::uint16_t>& value) {
		if (!conf.HasEntry(key))
			return true;   // left as it was: HTTP on its default, HTTPS as the certificate says (below)
		const wxString written = conf.Read(key, wxEmptyString).Strip(wxString::both);
		if (written.IsEmpty()) {
			value.reset();
			return true;
		}
		unsigned long number = 0;
		if (!written.ToULong(&number) || number > 65535) {
			error = wxString::Format(wxT("%s: %s=%s is not a port"), path, key, written);
			return false;
		}
		value = static_cast<std::uint16_t>(number);
		return true;
	};

	m_host = conf.Read(wxT("Host"), m_host).Strip(wxString::both);
	if (!port(wxT("HttpPort"), m_httpPort) || !port(wxT("HttpsPort"), m_httpsPort))
		return false;
	m_certificate = Beside(folder, conf.Read(wxT("Certificate"), wxEmptyString).Strip(wxString::both));
	m_privateKey = Beside(folder, conf.Read(wxT("PrivateKey"), wxEmptyString).Strip(wxString::both));
	// A certificate named and no HTTPS port said: HTTPS on the web's own (ibPort::WebSecure). Written empty — none.
	if (!conf.HasEntry(wxT("HttpsPort")) && !m_certificate.IsEmpty())
		m_httpsPort = ibPort::WebSecure.m_first;
	const wxString root = conf.Read(wxT("Root"), wxEmptyString).Strip(wxString::both);
	if (!root.IsEmpty())
		m_root = Beside(folder, root);
	wxStringTokenizer origins(conf.Read(wxT("Origins"), wxEmptyString), wxT(","));
	while (origins.HasMoreTokens()) {
		const wxString origin = origins.GetNextToken().Strip(wxString::both);
		if (!origin.IsEmpty())
			m_origins.push_back(origin);
	}
	conf.Read(wxT("Workers"), &m_workers, 0L);

	wxString group;
	long cookie = 0;
	std::vector<wxString> groups;
	for (bool more = conf.GetFirstGroup(group, cookie); more; more = conf.GetNextGroup(group, cookie))
		groups.push_back(group);
	for (const wxString& name : groups) {
		conf.SetPath(wxT("/") + name);
		ibWebPublication publication;
		publication.m_name = name;
		publication.m_kind = conf.Read(wxT("Kind"), wxEmptyString).Strip(wxString::both).Lower();
		publication.m_directory = Beside(folder, conf.Read(wxT("Directory"), wxEmptyString).Strip(wxString::both));
		publication.m_locale = conf.Read(wxT("Locale"), wxEmptyString).Strip(wxString::both);
		conf.Read(wxT("Debug"), &publication.m_debug, false);
		publication.m_address = conf.Read(wxT("Address"), wxEmptyString).Strip(wxString::both);
		const wxString trusted = conf.Read(wxT("Trusted"), wxEmptyString).Strip(wxString::both);
		if (!trusted.IsEmpty())
			publication.m_trusted = Beside(folder, trusted);
		conf.SetPath(wxT("/"));
		if (publication.m_kind != wxT("firebird") && publication.m_kind != wxT("server")) {
			error = wxString::Format(wxT("%s: [%s] Kind=%s - a publication is firebird (a file base) or server"),
				path, name, publication.m_kind);
			return false;
		}
		m_publications.push_back(publication);
	}
	return true;
}

//*******************************************************************************************
//*                                      The server                                         *
//*******************************************************************************************

ibWebServer::ibWebServer(const ibWebConfig& config) :
	m_config(config)
{
}

// Out of line: the file base is only declared in the header.
ibWebServer::~ibWebServer()
{
	Stop();
}

bool ibWebServer::Publish(const ibWebPublication& publication)
{
	const wxString& name = publication.m_name;
	if (name.IsEmpty() || name.Find(wxT('/')) != wxNOT_FOUND) {
		ibWebServerSay(ibJournalMark::Error, wxT("'%s' is not a publication's name - it is a segment of the path"), name);
		return false;
	}
	if (m_bases.count(name) != 0) {
		ibWebServerSay(ibJournalMark::Error, wxT("publication '%s' is named twice"), name);
		return false;
	}
	const std::string path = "/" + Utf8(name) + "/client";

	if (publication.m_kind == wxT("firebird")) {
		// THE FILE BASE, OPENED HERE — the request ibFileBaseOpen reads.
		ibProtocolNode request;
		request.SetValue(ibProtocol::ibFileBase::Directory, ibString(publication.m_directory))
			.SetValue(ibProtocol::ibFileBase::Name, ibString(name));
		if (!publication.m_locale.IsEmpty())
			request.SetValue(ibProtocol::ibFileBase::Locale, ibString(publication.m_locale));
		if (publication.m_debug)
			request.SetValue(ibProtocol::ibFileBase::Debug, true);
		auto base = std::make_unique<ibProtocolFileBase>();
		wxString error;
		if (!base->Open(request.Write(), error)) {
			ibWebServerSay(ibJournalMark::Error, wxT("publication '%s': the file base at %s did not open: %s"), name,
				publication.m_directory, error);
			return false;
		}
		const ibProtocolFileBase* const held = base.get();
		m_bases[name] = std::move(base);

		m_http.WebSocket(path, [held](const std::shared_ptr<ibWebSocket>& socket) {
			return std::make_shared<ibFileRelay>(*held, socket);
		});
		m_http.Post(path, [held](const ibHttpRequest& request, ibHttpResponder respond) {
			// A request on its own — no connection, as an application server answers a POST.
			ibString answer;
			wxString error;
			ibHttpResponse response;
			if (!held->Call(request.GetText(), nullptr, request.m_address, answer, error))
				response.m_status = 503;
			else if (answer.IsEmpty())
				response.m_status = 204;   // a notification — no answer wanted
			else
				response.SetText(answer, "application/json");
			respond(std::move(response));
		});
		return true;
	}

	// AN APPLICATION SERVER'S BASE — reached over its port, for each browser.
	wxString host, base;
	int port = 0;
	bool secure = false;
	if (!ibProtocolConnectionServer::ParseAddress(publication.m_address, host, port, base, secure)) {
		ibWebServerSay(ibJournalMark::Error, wxT("publication '%s': '%s' is not the address of a base: oes://server[:port]/<base> or oess://…"),
			name, publication.m_address);
		return false;
	}
	// OVER TLS — whom the web server trusts there (ClientTls), made once for every browser's socket to that server.
	std::shared_ptr<const ibTlsContext> tls;
	wxString refused;
	if (secure && !ibProtocolConnectionServer::ClientTls(publication.m_trusted, tls, refused)) {
		ibWebServerSay(ibJournalMark::Error, wxT("publication '%s': %s"), name, refused);
		return false;
	}
	const std::string server = Utf8(host);
	const std::uint16_t serverPort = static_cast<std::uint16_t>(port);
	const std::string target = ibProtocolConnectionServer::ClientPath(base);
	asio::io_context& io = m_http.GetContext();
	// A browser's socket to the base — a socket of the web server's own to the server for each (ibWebSocketRelay).
	m_http.WebSocket(path, [&io, server, serverPort, tls, target, name](const std::shared_ptr<ibWebSocket>& socket) {
		const std::string asked = WantsDeflate(socket->GetRequest()) ? target + "?compress=deflate" : target;
		return std::make_shared<ibWebSocketRelay>(io, server, serverPort, tls, asked, socket,
			[name](const ibString& reason) {
				ibWebServerSay(ibJournalMark::Warning, wxT("publication '%s': %s"), name, reason);
			});
	});
	m_http.Post(path, [&io, server, serverPort, tls, target, name](const ibHttpRequest& request, ibHttpResponder respond) {
		ibHttpPost(io, server, serverPort, tls, target, request.m_body, "application/json", kPostWait,
			[respond, name](int status, std::string body, const std::string& error) {
				ibHttpResponse response;
				if (status == 0) {
					ibWebServerSay(ibJournalMark::Warning, wxT("publication '%s': %s"), name, wxString::FromUTF8(error));
					response.m_status = 502;
				}
				else {
					response.m_status = status;
					if (!body.empty())
						response.SetContent(std::move(body), "application/json");
				}
				respond(std::move(response));
			});
	});
	// Not a file base: nothing is held for it but the routes.
	m_bases[name] = nullptr;
	return true;
}

int ibWebServer::Run()
{
	// Told to stop from here on — while the bases are still being opened too.
	std::signal(SIGINT, OnStopSignal);
	std::signal(SIGTERM, OnStopSignal);
#ifdef SIGBREAK
	std::signal(SIGBREAK, OnStopSignal);   // Windows: Ctrl+Break
#endif
#ifdef __WXMSW__
	::SetConsoleCtrlHandler(OnConsoleClose, TRUE);   // after the signals: asked first, it passes on what is theirs
#endif

	// However Run ends — served and stopped, or refused at any step — a console closing waits no longer.
	struct ibStoppedMark { ~ibStoppedMark() { s_state.store(ibServeState::Stopped); } } stoppedMark;

	std::size_t published = 0;
	for (const ibWebPublication& publication : m_config.m_publications) {
		if (s_state.load() != ibServeState::Serving)
			break;
		if (Publish(publication))
			++published;
	}

	// THE SITE — its folder when there is one (an edit is served at the next request), else its pack beside web.conf,
	// what a release ships. The same files at / and at each publication's address: a publication's page is the site's
	// index.html, its socket beside it.
	const wxString pack = m_config.m_folder + wxFileName::GetPathSeparator() + wxT("www.wpk");
	std::map<std::string, std::string> packed;
	wxString site;
	if (!m_config.m_root.IsEmpty() && ibDirExists(m_config.m_root)) {
		site = m_config.m_root;
	}
	else if (ibFileExists(pack)) {
		wxString packError;
		if (ReadPack(pack, packed, packError))
			site = pack;
		else
			ibWebServerSay(ibJournalMark::Error, wxT("%s - the site is not served"), packError);
	}
	if (!site.IsEmpty()) {
		std::vector<std::string> prefixes = { "/" };
		for (const auto& entry : m_bases)
			prefixes.push_back("/" + Utf8(entry.first) + "/");
		for (const std::string& prefix : prefixes) {
			if (packed.empty())
				m_http.Mount(prefix, Utf8(site));
			else
				m_http.Mount(prefix, packed);
		}
		// A publication's address without its slash, sent on to it: the page names its files beside it.
		for (const auto& entry : m_bases) {
			const std::string page = "/" + Utf8(entry.first);
			m_http.Get(page, [page](const ibHttpRequest&, ibHttpResponder respond) {
				ibHttpResponse response;
				response.m_status = 301;
				response.m_headers.emplace_back("Location", page + "/");
				respond(std::move(response));
			});
		}
	}
	if (published == 0 && site.IsEmpty()) {
		ibWebServerSay(ibJournalMark::Error, wxT("nothing to serve: no publication opened, and no site at %s or %s"),
			m_config.m_root, pack);
		Stop();
		return 1;
	}

	ibHttpServerOptions options;
	options.m_host = Utf8(m_config.m_host);
	options.m_httpPort = m_config.m_httpPort;
	options.m_httpsPort = m_config.m_httpsPort;
	wxString pemError;
	if ((!m_config.m_certificate.IsEmpty() && !ReadPem(m_config.m_certificate, options.m_certificate, pemError))
		|| (!m_config.m_privateKey.IsEmpty() && !ReadPem(m_config.m_privateKey, options.m_privateKey, pemError))) {
		ibWebServerSay(ibJournalMark::Error, wxT("%s - nothing is served"), pemError);
		Stop();
		return 1;
	}
	for (const wxString& origin : m_config.m_origins)
		options.m_origins.push_back(Utf8(origin));
	options.m_workers = m_config.m_workers > 0 ? static_cast<std::size_t>(m_config.m_workers) : 0;

	std::string refusal;
	if (!m_http.Start(options, refusal)) {
		ibWebServerSay(ibJournalMark::Error, wxT("the port did not open: %s - nothing is served"), wxString::FromUTF8(refusal));
		Stop();
		return 1;
	}

	// WHERE A BROWSER GOES — each port, and each publication on it.
	const wxString host = m_config.m_host.IsEmpty() ? wxGetFullHostName() : m_config.m_host;
	std::vector<wxString> origins;
	if (m_http.GetHttpsPort() != 0)
		origins.push_back(wxString::Format(wxT("https://%s:%u"), host, static_cast<unsigned>(m_http.GetHttpsPort())));
	if (m_http.GetHttpPort() != 0)
		origins.push_back(wxString::Format(wxT("http://%s:%u"), host, static_cast<unsigned>(m_http.GetHttpPort())));
	for (const wxString& origin : origins) {
		if (!site.IsEmpty())
			ibWebServerSay(ibJournalMark::Info, wxT("site %s: %s/"), site, origin);
		for (const auto& entry : m_bases) {
			wxString socket = origin;
			socket.Replace(wxT("http"), wxT("ws"), false);
			if (!site.IsEmpty())
				ibWebServerSay(ibJournalMark::Info, wxT("publication '%s': %s/%s/, its socket %s/%s/client"), entry.first,
					origin, entry.first, socket, entry.first);
			else
				ibWebServerSay(ibJournalMark::Info, wxT("publication '%s': %s/%s/client"), entry.first, socket, entry.first);
		}
	}
	ibWebServerSay(ibJournalMark::Info, wxT("serving %u of %u publication(s) - Ctrl+C to stop"),
		static_cast<unsigned>(published), static_cast<unsigned>(m_config.m_publications.size()));

	// THE PAGE FOR WHOEVER STARTED IT (ibStartWebServer): the reader takes the line once its end is there. No page —
	// the publication did not open, or no site is served — and nothing is written.
	if (!m_config.m_manifest.IsEmpty() && !site.IsEmpty() && !origins.empty() && !m_config.m_publications.empty()
		&& m_bases.count(m_config.m_publications.back().m_name) != 0) {
		wxFile file;
		if (!file.Create(m_config.m_manifest, true) || !file.Write(wxString::Format(wxT("url=%s/%s/\n"), origins.front(),
			m_config.m_publications.back().m_name), wxConvUTF8))
			ibWebServerSay(ibJournalMark::Error, wxT("the manifest %s was not written"), m_config.m_manifest);
	}

	while (s_state.load() == ibServeState::Serving)
		std::this_thread::sleep_for(std::chrono::milliseconds(250));

	ibWebServerSay(ibJournalMark::Info, wxT("stopping"));
	Stop();
	ibWebServerSay(ibJournalMark::Info, wxT("stopped"));
	return 0;
}

void ibWebServer::Stop()
{
	m_http.Stop();
	// Each file base's clients go with it — each its own session's teardown — and then the base.
	for (auto& entry : m_bases) {
		if (entry.second)
			entry.second->Close();
	}
	m_bases.clear();
}
