#include "webServer.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

#include <wx/file.h>
#include <wx/fileconf.h>
#include <wx/filename.h>
#include "core/stringTokenizer.h"
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
#include "core/diagnostics/crashGuard.h"   // the stop, one fact: who asks, the serving loop that reads it
#include "core/fileSystem.h"   // ibDirExists
#include "core/startProcess.h" // ibProcessExists

namespace {

// The stop done, which is what a console closing waits for (appserver/appServer.cpp says why).
std::atomic<bool> s_stopped{ false };

#ifdef __WXMSW__
BOOL WINAPI OnConsoleClose(DWORD type)
{
	if (type != CTRL_CLOSE_EVENT && type != CTRL_LOGOFF_EVENT && type != CTRL_SHUTDOWN_EVENT)
		return FALSE;
	ibCrashGuard::MarkStopRequested();
	for (int waited = 0; waited < 4500 && !s_stopped.load(); waited += 50)
		::Sleep(50);
	return TRUE;
}
#endif

// How long a POST to an application server may wait for its answer — the call's work, until it settles.
constexpr std::chrono::seconds kPostWait(300);

std::string Utf8(const ibString& text)
{
	return std::string(text.utf8_str());
}

// A path of web.conf, made absolute against the web folder.
ibString Beside(const ibString& folder, const ibString& path)
{
	if (path.IsEmpty())
		return path;
	wxFileName name(path);
	if (name.IsRelative())
		name.MakeAbsolute(folder);
	return name.GetFullPath();
}

// A PEM file, whole; false with why.
bool ReadPem(const ibString& path, std::string& text, ibString& error)
{
	wxFile file;
	if (!ibFileExists(path) || !file.Open(path)) {
		error = ibString::Format(ibT("%s is not there or not readable"), path);
		return false;
	}
	const wxFileOffset length = file.Length();
	text.assign(static_cast<std::size_t>(length > 0 ? length : 0), '\0');
	if (length > 0 && file.Read(&text[0], static_cast<size_t>(length)) != length) {
		error = ibString::Format(ibT("%s could not be read"), path);
		return false;
	}
	return true;
}

// THE SITE PACKED — www.wpk, a zip of the site's folder (the build packs src/engine/webserver/www into it): each file
// by its path in the site, as an address names it; false with why.
bool ReadPack(const ibString& path, std::map<std::string, std::string>& files, ibString& error)
{
	wxFileInputStream input(path);
	wxZipInputStream zip(input, wxConvUTF8);
	if (!input.IsOk() || !zip.IsOk()) {
		error = ibString::Format(ibT("%s could not be read"), path);
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
		error = ibString::Format(ibT("%s holds no file"), path);
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
// through, told what the base says to them, and they leave with it. Packets both ways (OES-RPC), the base's own: the
// socket hands them over as they are (and deflates one of some size, when the browser asked).
class ibFileRelay : public ibWebSocketHandler {
public:

	ibFileRelay(const ibProtocolFileBase& base, std::shared_ptr<ibWebSocket> socket) :
		m_base(base), m_socket(std::move(socket))
	{
		const std::weak_ptr<ibWebSocket> told = m_socket;
		m_base.Listen(this, [told](const ibMemoryBuffer& packet) {
			if (const std::shared_ptr<ibWebSocket> socket = told.lock())
				socket->Send(packet.GetData(), packet.GetDataLen(), ibWebSocketSend::Deflate);
		});
	}

	virtual void OnMessage(const ibString&) override
	{
		m_socket->Close(ibWebSocketClose::Unsupported, "the protocol speaks packets");
	}

	virtual void OnBinary(std::string data) override
	{
		const ibMemoryBuffer packet(data.data(), data.size());
		ibMemoryBuffer answer;
		ibString error;
		if (!m_base.Call(packet, this, m_socket->GetRequest().m_address, answer, error)) {
			ibWebServerSay(ibJournalMark::Error, ibT("a browser's call did not reach its base: %s"), error);
			m_socket->Close(ibWebSocketClose::ServerError, "the base is not open");
			return;
		}
		if (!answer.IsEmpty())
			m_socket->Send(answer.GetData(), answer.GetDataLen(), ibWebSocketSend::Deflate);
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
//*                                 The bases' addresses                                    *
//*******************************************************************************************

// THE REQUESTS TO THE BASES' ADDRESSES — threads of their own, so a service that takes its time holds none of the
// threads the browsers' clients are carried on; how many at once (past it — 503, try again); and how long one waits
// (past it — 504). A request answered by the wait still runs to its end on its thread: what it asked of the base is
// the base's, and its answer goes nowhere.
class ibServeQueue {
public:

	ibServeQueue(std::size_t workers, std::size_t calls, std::chrono::seconds wait) :
		m_calls(std::max<std::size_t>(calls, 1)), m_wait(wait)
	{
		for (std::size_t i = 0; i < std::max<std::size_t>(workers, 1); ++i)
			m_threads.emplace_back([this]() { Work(); });
		m_watch = std::thread([this]() { WatchWaits(); });
	}

	~ibServeQueue()
	{
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_stopping = true;
		}
		m_wake.notify_all();
		for (std::thread& thread : m_threads)
			thread.join();
		m_watch.join();
		// Whoever still waits is told the server is going.
		for (const std::shared_ptr<ibAnswer>& answer : m_waiting)
			answer->Give(Status(503));
	}

	ibServeQueue(const ibServeQueue&) = delete;
	ibServeQueue& operator=(const ibServeQueue&) = delete;

	// The work queued, its answer given once — by the work, or by the wait. False: as many as are let in already.
	bool Post(std::function<ibHttpResponse()> work, ibHttpResponder respond)
	{
		auto answer = std::make_shared<ibAnswer>();
		answer->m_respond = std::move(respond);
		answer->m_deadline = std::chrono::steady_clock::now() + m_wait;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (m_stopping || m_busy >= m_calls)
				return false;
			++m_busy;
			m_jobs.push_back(ibJob{ std::move(work), answer });
			m_waiting.push_back(answer);
		}
		m_wake.notify_one();
		return true;
	}

	static ibHttpResponse Status(int status)
	{
		ibHttpResponse response;
		response.m_status = status;
		if (status == 503)
			response.m_headers.emplace_back("Retry-After", "5");
		return response;
	}

private:

	// ONE ANSWER, GIVEN ONCE — whichever comes first.
	struct ibAnswer {
		std::mutex                            m_mutex;
		bool                                  m_given = false;
		ibHttpResponder                       m_respond;
		std::chrono::steady_clock::time_point m_deadline;

		bool IsGiven()
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			return m_given;
		}

		void Give(ibHttpResponse response)
		{
			ibHttpResponder respond;
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				if (m_given)
					return;
				m_given = true;
				respond = std::move(m_respond);
			}
			respond(std::move(response));
		}
	};

	struct ibJob {
		std::function<ibHttpResponse()> m_work;
		std::shared_ptr<ibAnswer>       m_answer;
	};

	void Work()
	{
		for (;;) {
			ibJob job;
			{
				std::unique_lock<std::mutex> lock(m_mutex);
				m_wake.wait(lock, [this]() { return m_stopping || !m_jobs.empty(); });
				if (m_stopping)
					return;
				job = std::move(m_jobs.front());
				m_jobs.pop_front();
			}
			// Answered by the wait while it was queued — not asked at all.
			if (!job.m_answer->IsGiven())
				job.m_answer->Give(job.m_work());
			std::lock_guard<std::mutex> lock(m_mutex);
			--m_busy;
			std::erase(m_waiting, job.m_answer);
		}
	}

	// The waits past their time answered — a quarter of a second at most late.
	void WatchWaits()
	{
		std::unique_lock<std::mutex> lock(m_mutex);
		while (!m_wake.wait_for(lock, std::chrono::milliseconds(250), [this]() { return m_stopping; })) {
			const auto now = std::chrono::steady_clock::now();
			std::vector<std::shared_ptr<ibAnswer>> late;
			for (const std::shared_ptr<ibAnswer>& answer : m_waiting) {
				if (answer->m_deadline <= now)
					late.push_back(answer);
			}
			if (late.empty())
				continue;
			lock.unlock();
			for (const std::shared_ptr<ibAnswer>& answer : late)
				answer->Give(Status(504));
			lock.lock();
		}
	}

	const std::size_t          m_calls;
	const std::chrono::seconds m_wait;

	std::mutex                             m_mutex;
	std::condition_variable                m_wake;
	bool                                   m_stopping = false;
	std::size_t                            m_busy = 0;      // queued and running
	std::deque<ibJob>                      m_jobs;
	std::vector<std::shared_ptr<ibAnswer>> m_waiting;       // queued and running, to be answered by the wait
	std::vector<std::thread>               m_threads;
	std::thread                            m_watch;
};

// ONE PUBLICATION'S ADDRESSES — the base asked for its list once (`addresses`), which also tells this connection every
// change; each entry's path mounted under the publication, and what comes there sent to the base (`serve`). A file
// base is asked in this process, by a connection of its own; an application server's over a socket that listens (one)
// and sockets that serve (as many as the requests at once, kept for the next).
class ibServedPublication : public std::enable_shared_from_this<ibServedPublication> {
public:

	ibServedPublication(const ibString& name, ibHttpServer& http, ibServeQueue& queue) :
		m_name(name), m_http(http), m_queue(queue)
	{
	}

	// A file base opened here…
	void ServeFileBase(const ibProtocolFileBase* base)
	{
		m_base = base;
		const std::weak_ptr<ibServedPublication> told = weak_from_this();
		m_base->Listen(this, [told](const ibMemoryBuffer& packet) {
			ibProtocolNode said;
			if (const std::shared_ptr<ibServedPublication> self = told.lock(); self && ibProtocolNode::Read(packet, said))
				self->Told(said);
		});
		Ask();
	}

	// …or an application server's base, at its address.
	void ServeServer(const ibString& address, const ibString& trusted)
	{
		m_address = address;
		m_trusted = trusted;
		Watch();
	}

	// THE LISTENING SOCKET KEPT — opened again when it dropped, and the list asked for again: the subscription was the
	// socket's. Every few seconds from the serving loop; a file base has none to lose.
	void Watch()
	{
		if (m_base != nullptr)
			return;
		ibString error;
		bool opened = false;
		if (m_listener == nullptr) {
			auto listener = std::make_unique<ibProtocolConnectionServer>();
			if (listener->Open(m_address, m_trusted, error)) {
				m_listener = std::move(listener);
				opened = true;
			}
		}
		else if (!m_listener->IsOpen())
			opened = m_listener->Reconnect(error);
		else
			return;
		if (!opened) {
			if (!m_unreached)
				ibWebServerSay(ibJournalMark::Warning, ibT("publication '%s': its base's addresses are not listened to: %s"),
					m_name, error);
			m_unreached = true;
			return;
		}
		m_unreached = false;
		const std::weak_ptr<ibServedPublication> told = weak_from_this();
		m_listener->Listen([told](const ibProtocolNode& said) {
			if (const std::shared_ptr<ibServedPublication> self = told.lock())
				self->Told(said);
		});
		Ask();
	}

	// Nothing mounted, nothing listened to — before the base goes.
	void Stop()
	{
		std::map<std::string, ibString> mounted;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			mounted.swap(m_mounted);
		}
		for (const auto& entry : mounted)
			m_http.Unmount(entry.first);
		if (m_base != nullptr)
			m_base->Disconnect(this);
		if (m_listener != nullptr) {
			m_listener->Listen(nullptr);
			m_listener->Close();
		}
		std::lock_guard<std::mutex> lock(m_mutex);
		for (const auto& connection : m_idle)
			connection->Close();
		m_idle.clear();
	}

	// A REQUEST TO AN ENTRY, handed to the base and its answer back — on a thread of the queue.
	ibHttpResponse Serve(const ibString& id, const std::string& prefix, const ibHttpRequest& request)
	{
		ibProtocolNode call;
		call.SetValue(ibProtocol::ibRpc::Id, 1LL)
			.SetValue(ibProtocol::ibRpc::Method, ibString(ibProtocol::MethodName(ibProtocol::ibMethod::Serve)));
		ibProtocolNode params = call.Child(ibProtocol::ibRpc::Params);
		params.SetValue(ibProtocol::ibServed::Address, id)
			.SetValue(ibProtocol::ibServed::Prefix, ibString::FromUTF8(prefix))
			.SetValue(ibProtocol::ibServed::Method, ibString::FromUTF8(request.m_method))
			.SetValue(ibProtocol::ibServed::Path, ibString::FromUTF8(request.m_path))
			.SetValue(ibProtocol::ibServed::Query, ibString::FromUTF8(request.m_query));
		for (const auto& header : request.m_headers) {
			ibProtocolNode item = params.AddItem(ibProtocol::ibServed::Headers);
			item.SetValue(ibProtocol::ibServed::Name, ibString::FromUTF8(header.first))
				.SetValue(ibProtocol::ibServed::Value, ibString::FromUTF8(header.second));
		}
		params.SetValue(ibProtocol::ibServed::Body, ibMemoryBuffer(request.m_body.data(), request.m_body.size()));

		ibProtocolNode answer;
		ibString error;
		if (!Exchange(call, request.m_address, answer, error)) {
			ibWebServerSay(ibJournalMark::Warning, ibT("publication '%s': a request to %s did not reach its base: %s"),
				m_name, ibString::FromUTF8(request.m_path), error);
			return ibServeQueue::Status(502);
		}
		const ibProtocolNode result = answer.FindChild(ibProtocol::ibRpc::Result);
		if (result.IsEmpty()) {
			ibWebServerSay(ibJournalMark::Warning, ibT("publication '%s': its base refused a request to %s: %s"), m_name,
				ibString::FromUTF8(request.m_path),
				answer.FindChild(ibProtocol::ibRpc::Error).GetString(ibProtocol::ibRpc::Message));
			return ibServeQueue::Status(502);
		}
		ibHttpResponse response;
		response.m_status = static_cast<int>(result.GetInt(ibProtocol::ibServed::Status, 502));
		for (const ibProtocolNode& header : result.GetList(ibProtocol::ibServed::Headers))
			response.m_headers.emplace_back(Utf8(header.GetString(ibProtocol::ibServed::Name)),
				Utf8(header.GetString(ibProtocol::ibServed::Value)));
		const ibMemoryBuffer body = result.GetBinary(ibProtocol::ibServed::Body);
		response.m_body.assign(static_cast<const char*>(body.GetData()), body.GetDataLen());
		return response;
	}

private:

	// The list asked for — answered at once, and the connection subscribed by the asking.
	void Ask()
	{
		ibProtocolNode call;
		call.SetValue(ibProtocol::ibRpc::Id, 1LL)
			.SetValue(ibProtocol::ibRpc::Method, ibString(ibProtocol::MethodName(ibProtocol::ibMethod::Addresses)))
			.SetValue(ibProtocol::ibRpc::Params, ibProtocolNode());
		const std::uint64_t told = m_told.load();
		ibProtocolNode answer;
		ibString error;
		if (!Exchange(call, ibEmptyString, answer, error, true)) {
			ibWebServerSay(ibJournalMark::Warning, ibT("publication '%s': its base's addresses were not read: %s"), m_name, error);
			return;
		}
		const ibProtocolNode result = answer.FindChild(ibProtocol::ibRpc::Result);
		if (result.IsEmpty()) {
			ibWebServerSay(ibJournalMark::Warning, ibT("publication '%s': its base did not give its addresses: %s"), m_name,
				answer.FindChild(ibProtocol::ibRpc::Error).GetString(ibProtocol::ibRpc::Message));
			return;
		}
		// A change told while the answer was on its way is newer than the answer.
		if (m_told.load() == told)
			Take(result);
	}

	// What the base says unasked — the list, whole, each time it changes.
	void Told(const ibProtocolNode& said)
	{
		if (said.GetString(ibProtocol::ibRpc::Method) != ibString(ibProtocol::ibRpc::Addresses))
			return;
		++m_told;
		Take(said.FindChild(ibProtocol::ibRpc::Params));
	}

	// THE LIST MOUNTED — what it no longer has unmounted, what is new mounted; an entry that keeps its path and its id
	// stays as it is.
	void Take(const ibProtocolNode& holder)
	{
		std::map<std::string, ibString> wanted;
		for (const ibProtocolNode& entry : holder.GetList(ibProtocol::ibServed::Addresses)) {
			const ibString path = entry.GetString(ibProtocol::ibServed::Path);
			const ibString id = entry.GetString(ibProtocol::ibServed::Id);
			if (!path.IsEmpty() && !id.IsEmpty())
				wanted["/" + Utf8(m_name) + "/" + Utf8(path) + "/"] = id;
		}
		std::lock_guard<std::mutex> lock(m_mutex);
		for (auto it = m_mounted.begin(); it != m_mounted.end();) {
			const auto found = wanted.find(it->first);
			if (found == wanted.end() || found->second != it->second) {
				m_http.Unmount(it->first);
				ibWebServerSay(ibJournalMark::Info, ibT("publication '%s': %s is served no more"), m_name,
					ibString::FromUTF8(it->first));
				it = m_mounted.erase(it);
			}
			else
				++it;
		}
		for (const auto& [mount, id] : wanted) {
			if (m_mounted.contains(mount))
				continue;
			const std::weak_ptr<ibServedPublication> held = weak_from_this();
			ibServeQueue& queue = m_queue;
			const std::string prefix = mount.substr(0, mount.size() - 1);
			m_http.Mount(mount, [held, &queue, id, prefix](const ibHttpRequest& request, ibHttpResponder respond) {
				const std::shared_ptr<ibServedPublication> self = held.lock();
				if (self == nullptr) {
					respond(ibServeQueue::Status(404));
					return;
				}
				// The request is the handler's only while it runs: the queue's copy is its own.
				ibHttpResponder answer = respond;
				if (!queue.Post([self, id, prefix, request]() { return self->Serve(id, prefix, request); }, std::move(answer)))
					respond(ibServeQueue::Status(503));
			});
			m_mounted[mount] = id;
			ibWebServerSay(ibJournalMark::Info, ibT("publication '%s': serving %s"), m_name, ibString::FromUTF8(mount));
		}
	}

	// One request to the base and its answer. `listening` — on the connection told the changes; else a file base's
	// connectionless call, or a serving socket of the server's.
	bool Exchange(const ibProtocolNode& call, const ibString& address, ibProtocolNode& answer, ibString& error,
		bool listening = false)
	{
		ibMemoryBuffer packet;
		call.Write(packet);
		if (m_base != nullptr) {
			ibMemoryBuffer bytes;
			if (!m_base->Call(packet, listening ? this : nullptr, address, bytes, error))
				return false;
			if (!ibProtocolNode::Read(bytes, answer)) {
				error = ibT("the answer is no packet");
				return false;
			}
			return true;
		}
		ibProtocol::ibRefusal refusal = ibProtocol::ibRefusal::None;
		if (listening)
			return m_listener != nullptr && m_listener->Exchange(packet, answer, refusal, error);

		// A serving socket — one kept from before, else a new one; kept again when it is still open.
		std::unique_ptr<ibProtocolConnectionServer> connection;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (!m_idle.empty()) {
				connection = std::move(m_idle.back());
				m_idle.pop_back();
			}
		}
		if (connection == nullptr || (!connection->IsOpen() && !connection->Reconnect(error))) {
			connection = std::make_unique<ibProtocolConnectionServer>();
			if (!connection->Open(m_address, m_trusted, error))
				return false;
		}
		const bool done = connection->Exchange(packet, answer, refusal, error);
		if (connection->IsOpen()) {
			std::lock_guard<std::mutex> lock(m_mutex);
			m_idle.push_back(std::move(connection));
		}
		return done;
	}

	const ibString m_name;
	ibHttpServer&  m_http;
	ibServeQueue&  m_queue;

	const ibProtocolFileBase*                   m_base = nullptr;   // a file base — else:
	ibString                                    m_address;          // …the application server's base
	ibString                                    m_trusted;
	std::unique_ptr<ibProtocolConnectionServer> m_listener;         // touched by Watch and Stop, on the serving loop
	bool                                        m_unreached = false;
	std::atomic<std::uint64_t>                  m_told{ 0 };        // the changes told so far

	std::mutex                                               m_mutex;
	std::map<std::string, ibString>                          m_mounted;   // the mount and the entry it serves
	std::vector<std::unique_ptr<ibProtocolConnectionServer>> m_idle;      // serving sockets kept for the next request
};

//*******************************************************************************************
//*                                      The config                                         *
//*******************************************************************************************

bool ibWebConfig::Read(const ibString& folder, ibString& error)
{
	m_folder = folder;
	m_root = Beside(folder, ibT("www"));
	const ibString path = folder + ibFILE_SEP_PATH + ibT("web.conf");
	if (!ibFileExists(path))
		return true;

	// Read as written: a Windows path keeps its backslashes.
	wxFileConfig conf(ibEmptyString, ibEmptyString, path, ibEmptyString,
		wxCONFIG_USE_LOCAL_FILE | wxCONFIG_USE_NO_ESCAPE_CHARACTERS);

	const auto port = [&conf, &path, &error](const ibString& key, std::optional<std::uint16_t>& value) {
		if (!conf.HasEntry(key))
			return true;   // left as it was: HTTP on its default, HTTPS as the certificate says (below)
		const ibString written = ibString(conf.Read(key, ibEmptyString)).Strip(ibString::both);
		if (written.IsEmpty()) {
			value.reset();
			return true;
		}
		unsigned long number = 0;
		if (!written.ToULong(&number) || number > 65535) {
			error = ibString::Format(ibT("%s: %s=%s is not a port"), path, key, written);
			return false;
		}
		value = static_cast<std::uint16_t>(number);
		return true;
	};

	m_host = ibString(conf.Read(wxT("Host"), m_host)).Strip(ibString::both);
	if (!port(ibT("HttpPort"), m_httpPort) || !port(ibT("HttpsPort"), m_httpsPort))
		return false;
	m_certificate = Beside(folder, ibString(conf.Read(wxT("Certificate"), ibEmptyString)).Strip(ibString::both));
	m_privateKey = Beside(folder, ibString(conf.Read(wxT("PrivateKey"), ibEmptyString)).Strip(ibString::both));
	// A certificate named and no HTTPS port said: HTTPS on the web's own (ibPort::WebSecure). Written empty — none.
	if (!conf.HasEntry(wxT("HttpsPort")) && !m_certificate.IsEmpty())
		m_httpsPort = ibPort::WebSecure.m_first;
	const ibString root = ibString(conf.Read(wxT("Root"), ibEmptyString)).Strip(ibString::both);
	if (!root.IsEmpty())
		m_root = Beside(folder, root);
	ibStringTokenizer origins(conf.Read(wxT("Origins"), ibEmptyString), ibT(","));
	while (origins.HasMoreTokens()) {
		const ibString origin = origins.GetNextToken().Strip(ibString::both);
		if (!origin.IsEmpty())
			m_origins.push_back(origin);
	}
	conf.Read(wxT("Workers"), &m_workers, 0L);
	conf.Read(wxT("ServiceWorkers"), &m_serviceWorkers, m_serviceWorkers);
	conf.Read(wxT("ServiceCalls"), &m_serviceCalls, m_serviceCalls);
	conf.Read(wxT("ServiceWait"), &m_serviceWait, m_serviceWait);

	wxString group;   // wxFileConfig's own: it writes the group's name into it
	long cookie = 0;
	std::vector<ibString> groups;
	for (bool more = conf.GetFirstGroup(group, cookie); more; more = conf.GetNextGroup(group, cookie))
		groups.push_back(group);
	for (const ibString& name : groups) {
		conf.SetPath(ibT("/") + name);
		ibWebPublication publication;
		publication.m_name = name;
		publication.m_kind = ibString(conf.Read(wxT("Kind"), ibEmptyString)).Strip(ibString::both).Lower();
		publication.m_directory = Beside(folder, ibString(conf.Read(wxT("Directory"), ibEmptyString)).Strip(ibString::both));
		publication.m_locale = ibString(conf.Read(wxT("Locale"), ibEmptyString)).Strip(ibString::both);
		conf.Read(wxT("Debug"), &publication.m_debug, false);
		publication.m_address = ibString(conf.Read(wxT("Address"), ibEmptyString)).Strip(ibString::both);
		const ibString trusted = ibString(conf.Read(wxT("Trusted"), ibEmptyString)).Strip(ibString::both);
		if (!trusted.IsEmpty())
			publication.m_trusted = Beside(folder, trusted);
		conf.SetPath(wxT("/"));
		if (publication.m_kind != ibT("firebird") && publication.m_kind != ibT("server")) {
			error = ibString::Format(ibT("%s: [%s] Kind=%s - a publication is firebird (a file base) or server"),
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
	m_config(config),
	m_serveQueue(std::make_unique<ibServeQueue>(static_cast<std::size_t>(std::max(config.m_serviceWorkers, 1L)),
		static_cast<std::size_t>(std::max(config.m_serviceCalls, 1L)), std::chrono::seconds(std::max(config.m_serviceWait, 1L))))
{
}

void ibWebServer::Serve(const ibWebPublication& publication, const ibProtocolFileBase* base)
{
	auto served = std::make_shared<ibServedPublication>(publication.m_name, m_http, *m_serveQueue);
	m_served[publication.m_name] = served;
	if (base != nullptr)
		served->ServeFileBase(base);
	else
		served->ServeServer(publication.m_address, publication.m_trusted);
}

// Out of line: the file base is only declared in the header.
ibWebServer::~ibWebServer()
{
	Stop();
}

bool ibWebServer::Publish(const ibWebPublication& publication)
{
	const ibString& name = publication.m_name;
	if (name.IsEmpty() || name.Find(ibT('/')) != wxNOT_FOUND) {
		ibWebServerSay(ibJournalMark::Error, ibT("'%s' is not a publication's name - it is a segment of the path"), name);
		return false;
	}
	if (m_bases.contains(name)) {
		ibWebServerSay(ibJournalMark::Error, ibT("publication '%s' is named twice"), name);
		return false;
	}
	const std::string path = "/" + Utf8(name) + "/client";

	if (publication.m_kind == ibT("firebird")) {
		// THE FILE BASE, OPENED HERE — the request ibFileBaseOpen reads.
		ibProtocolNode request;
		request.SetValue(ibProtocol::ibFileBase::Directory, ibString(publication.m_directory))
			.SetValue(ibProtocol::ibFileBase::Name, ibString(name));
		if (!publication.m_locale.IsEmpty())
			request.SetValue(ibProtocol::ibFileBase::Locale, ibString(publication.m_locale));
		if (publication.m_debug)
			request.SetValue(ibProtocol::ibFileBase::Debug, true);
		auto base = std::make_unique<ibProtocolFileBase>();
		ibString error;
		if (!base->Open(request.Write(), error)) {
			ibWebServerSay(ibJournalMark::Error, ibT("publication '%s': the file base at %s did not open: %s"), name,
				publication.m_directory, error);
			return false;
		}
		const ibProtocolFileBase* const held = base.get();
		m_bases[name] = std::move(base);

		m_http.WebSocket(path, [held](const std::shared_ptr<ibWebSocket>& socket) {
			return std::make_shared<ibFileRelay>(*held, socket);
		});
		m_http.Post(path, [held](const ibHttpRequest& request, ibHttpResponder respond) {
			// A request on its own — no connection, as an application server answers a POST: JSON-RPC text, the base's
			// packet made of it and its answer's written back as text (its types named by the words it carries).
			ibHttpResponse response;
			ibProtocolNode asked, answered;
			if (!ibProtocolNode::Read(request.GetText(), asked)) {
				response.m_status = 400;
				respond(std::move(response));
				return;
			}
			ibMemoryBuffer packet, answer;
			ibString error;
			asked.Write(packet);
			if (!held->Call(packet, nullptr, request.m_address, answer, error))
				response.m_status = 503;
			else if (answer.IsEmpty())
				response.m_status = 204;   // a notification — no answer wanted
			else if (!ibProtocolNode::Read(answer, answered))
				response.m_status = 502;
			else {
				// The types named where they stand, as the JSON view names them — not a list of them beside.
				answered.Remove(ibProtocol::ibRpc::Types);
				answered.SetValue(ibProtocol::ibRpc::Version, ibString(ibT("2.0")));
				response.SetText(answered.Write(), "application/json");
			}
			respond(std::move(response));
		});
		Serve(publication, held);
		return true;
	}

	// AN APPLICATION SERVER'S BASE — reached over its port, for each browser.
	ibString host, base;
	int port = 0;
	bool secure = false;
	if (!ibProtocolConnectionServer::ParseAddress(publication.m_address, host, port, base, secure)) {
		ibWebServerSay(ibJournalMark::Error, ibT("publication '%s': '%s' is not the address of a base: oes://server[:port]/<base>, oess://… or a publication's link, http(s)://…"),
			name, publication.m_address);
		return false;
	}
	// OVER TLS — whom the web server trusts there (ClientTls), made once for every browser's socket to that server.
	std::shared_ptr<const ibTlsContext> tls;
	ibString refused;
	if (secure && !ibProtocolConnectionServer::ClientTls(publication.m_trusted, tls, refused)) {
		ibWebServerSay(ibJournalMark::Error, ibT("publication '%s': %s"), name, refused);
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
				ibWebServerSay(ibJournalMark::Warning, ibT("publication '%s': %s"), name, reason);
			});
	});
	m_http.Post(path, [&io, server, serverPort, tls, target, name](const ibHttpRequest& request, ibHttpResponder respond) {
		ibHttpPost(io, server, serverPort, tls, target, request.m_body, "application/json", kPostWait,
			[respond, name](int status, std::string body, const std::string& error) {
				ibHttpResponse response;
				if (status == 0) {
					ibWebServerSay(ibJournalMark::Warning, ibT("publication '%s': %s"), name, ibString::FromUTF8(error));
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
	Serve(publication, nullptr);
	return true;
}

int ibWebServer::Run()
{
	// Told to stop from here on — while the bases are still being opened too.
	ibCrashGuard::InstallStopSignals();
#ifdef __WXMSW__
	::SetConsoleCtrlHandler(OnConsoleClose, TRUE);   // after the signals: asked first, it passes on what is theirs
#endif
	// A SERVER A DEBUGGER STARTED LIVES WHILE IT IS ATTACHED: the debugger letting go or ending the session ends this
	// process. One started by itself only loses the session, and serves on.
	if (m_config.m_owner != 0)
		ibOnStopProcess(ibCrashGuard::MarkStopRequested);

	// However Run ends — served and stopped, or refused at any step — a console closing waits no longer.
	struct ibStoppedMark { ~ibStoppedMark() { s_stopped.store(true); } } stoppedMark;

	std::size_t published = 0;
	for (const ibWebPublication& publication : m_config.m_publications) {
		if (ibCrashGuard::IsStopRequested())
			break;
		if (Publish(publication))
			++published;
	}

	// THE SITE — its folder when there is one (an edit is served at the next request), else its pack beside web.conf,
	// what a release ships. The same files at / and at each publication's address: a publication's page is the site's
	// index.html, its socket beside it.
	const ibString pack = m_config.m_folder + ibFILE_SEP_PATH + ibT("www.wpk");
	std::map<std::string, std::string> packed;
	ibString site;
	if (!m_config.m_root.IsEmpty() && ibDirExists(m_config.m_root)) {
		site = m_config.m_root;
	}
	else if (ibFileExists(pack)) {
		ibString packError;
		if (ReadPack(pack, packed, packError))
			site = pack;
		else
			ibWebServerSay(ibJournalMark::Error, ibT("%s - the site is not served"), packError);
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
		ibWebServerSay(ibJournalMark::Error, ibT("nothing to serve: no publication opened, and no site at %s or %s"),
			m_config.m_root, pack);
		Stop();
		return 1;
	}

	ibHttpServerOptions options;
	options.m_host = Utf8(m_config.m_host);
	options.m_httpPort = m_config.m_httpPort;
	options.m_httpsPort = m_config.m_httpsPort;
	ibString pemError;
	if ((!m_config.m_certificate.IsEmpty() && !ReadPem(m_config.m_certificate, options.m_certificate, pemError))
		|| (!m_config.m_privateKey.IsEmpty() && !ReadPem(m_config.m_privateKey, options.m_privateKey, pemError))) {
		ibWebServerSay(ibJournalMark::Error, ibT("%s - nothing is served"), pemError);
		Stop();
		return 1;
	}
	for (const ibString& origin : m_config.m_origins)
		options.m_origins.push_back(Utf8(origin));
	options.m_workers = m_config.m_workers > 0 ? static_cast<std::size_t>(m_config.m_workers) : 0;

	std::string refusal;
	if (!m_http.Start(options, refusal)) {
		ibWebServerSay(ibJournalMark::Error, ibT("the port did not open: %s - nothing is served"), ibString::FromUTF8(refusal));
		Stop();
		return 1;
	}

	// WHERE A BROWSER GOES — each port, and each publication on it.
	const ibString host = m_config.m_host.IsEmpty() ? ibString(wxGetFullHostName()) : m_config.m_host;
	std::vector<ibString> origins;
	if (m_http.GetHttpsPort() != 0)
		origins.push_back(ibString::Format(ibT("https://%s:%u"), host, static_cast<unsigned>(m_http.GetHttpsPort())));
	if (m_http.GetHttpPort() != 0)
		origins.push_back(ibString::Format(ibT("http://%s:%u"), host, static_cast<unsigned>(m_http.GetHttpPort())));
	for (const ibString& origin : origins) {
		if (!site.IsEmpty())
			ibWebServerSay(ibJournalMark::Info, ibT("site %s: %s/"), site, origin);
		for (const auto& entry : m_bases) {
			ibString socket = origin;
			socket.Replace(ibT("http"), ibT("ws"), false);
			if (!site.IsEmpty())
				ibWebServerSay(ibJournalMark::Info, ibT("publication '%s': %s/%s/, its socket %s/%s/client"), entry.first,
					origin, entry.first, socket, entry.first);
			else
				ibWebServerSay(ibJournalMark::Info, ibT("publication '%s': %s/%s/client"), entry.first, socket, entry.first);
		}
	}
	ibWebServerSay(ibJournalMark::Info, ibT("serving %u of %u publication(s) - Ctrl+C to stop"),
		static_cast<unsigned>(published), static_cast<unsigned>(m_config.m_publications.size()));

	// THE PAGE FOR WHOEVER STARTED IT (ibStartWebServer): the reader takes the line once its end is there. No page —
	// the publication did not open, or no site is served — and nothing is written.
	if (!m_config.m_manifest.IsEmpty() && !site.IsEmpty() && !origins.empty() && !m_config.m_publications.empty()
		&& m_bases.contains(m_config.m_publications.back().m_name)) {
		wxFile file;
		if (!file.Create(m_config.m_manifest, true) || !file.Write(ibString::Format(ibT("url=%s/%s/\n"), origins.front(),
			m_config.m_publications.back().m_name), wxConvUTF8))
			ibWebServerSay(ibJournalMark::Error, ibT("the manifest %s was not written"), m_config.m_manifest);
	}

	// A DESIGNER'S DEBUG RUN GOES WITH THE DESIGNER, closed or fallen: nobody else knows this one is there, and it holds
	// the base and the executable's libraries from whoever comes next.
	for (unsigned tick = 1; !ibCrashGuard::IsStopRequested(); ++tick) {
		std::this_thread::sleep_for(std::chrono::milliseconds(250));
		// An application server's socket that tells the addresses, opened again when it dropped — every two seconds.
		if (tick % 8 == 0) {
			for (const auto& entry : m_served)
				entry.second->Watch();
		}
		if (m_config.m_owner != 0 && !ibProcessExists(m_config.m_owner)) {
			ibWebServerSay(ibJournalMark::Info, ibT("the process %ld that started this server has ended"), m_config.m_owner);
			ibCrashGuard::MarkStopRequested();
		}
	}

	ibWebServerSay(ibJournalMark::Info, ibT("stopping"));
	Stop();
	ibWebServerSay(ibJournalMark::Info, ibT("stopped"));
	return 0;
}

void ibWebServer::Stop()
{
	m_http.Stop();
	// The addresses unmounted and no longer listened to; the requests still running end — each its base's answer, or
	// its socket closed — and then the bases go.
	for (auto& entry : m_served)
		entry.second->Stop();
	m_serveQueue.reset();
	m_served.clear();
	// Each file base's clients go with it — each its own session's teardown — and then the base.
	for (auto& entry : m_bases) {
		if (entry.second)
			entry.second->Close();
	}
	m_bases.clear();
}
