#include "mcpServer.h"

#include <mutex>
#include <string>
#include <vector>

#include <wx/config.h>

#include "core/exception.h"           // ibCoreException — the listener's refusal
#include "core/net/httpServer.h"       // the port
#include "core/net/webSocketFrame.h"   // ibIsUtf8 — a body that is not UTF-8 is refused before it is sent
#include "core/server/listener.h"      // the options: plain on this machine's own addresses only
#include "core/crypto/sessionToken.h"  // TextEqual - the token compared in constant time; Generate - the token made
#include "core/net/ports.h"            // the port each mode starts on
#include "frmclient/backend/appData.h" // DesignerMode — whose settings
#include "frmclient/mainFrame/mainFrame.h"

namespace {

// A request is a JSON document, not a file upload. Anything past this is a
// client that has lost its place — refused before it is allocated, the way the
// debug server refuses an absurd packet length.
const std::size_t kMaxRequestBytes = 8u * 1024u * 1024u;

// The designer's and the application's apart: both can be open on one machine, each on its own port.
wxString SettingsGroup()
{
	return appData->DesignerMode() ? wxT("/Assistant/Designer/") : wxT("/Assistant/Application/");
}

// The bottom bar's lamp. The endpoint, not just the word: when an assistant cannot connect, the first question is always
// which address and port it should have used, and the answer is then already on screen. Empty when it is off — its
// absence says the same thing.
void ShowLamp(ibFrontendMainFrame* frame, const ibString& endpoint)
{
	if (frame->GetStatusBar() != nullptr)
		frame->SetStatusText(endpoint.IsEmpty() ? wxString() : wxString::Format(wxT("Assistant  %s"), wxString(endpoint)),
			ibDocBottomStatusBar::FieldAssistant);
}

}

//---------------------------------------------------------------------------
// the listener
//---------------------------------------------------------------------------
class ibMcpServer::ibMcpListener {
public:

	ibMcpListener(ibFrontendMainFrame* frame, const ibMcpSettings& settings)
		: m_frame(frame), m_settings(settings)
	{
	}

	~ibMcpListener()
	{
		Stop();
	}

	// LISTENING WHEN THIS RETURNS, started on the caller's thread, deliberately. A person
	// pressed a button and is owed an answer now: "that port is taken" must come back from
	// Start(), not appear in a log a second later while the button says it worked.
	bool Start(ibString& refusal)
	{
		// THE TRANSPORT IS THE PROCESS'S (core/server/listener.h), plain and so on this machine's own addresses
		// only - an assistant edits a configuration, and a token sent in the clear across a network is a token
		// given away.
		ibHttpServerOptions options;
		try {
			options = ibListenerOptions(m_settings.m_address, m_settings.m_port, ibTlsFiles());
		}
		catch (const ibCoreException& err) {
			refusal = ibString::Format(_("Assistant access did not start: %s"), err.GetErrorDescription());
			return false;
		}
		options.m_maxBodyBytes = kMaxRequestBytes;

		// ⚠ HOW LONG A CONNECTION MAY SIT IDLE between two calls. A server's minute is not this one's:
		// the pause between two calls from an assistant is a pause for THINKING. httplib closed a
		// kept-alive connection after five seconds of it, under a client that had every intention of
		// using it again, and the next call read as a dropped session rather than as a reconnect — a
		// refusal that arrived INSTANTLY, on a call that takes 371 ms.
		//
		// This server exists to be addressed occasionally and thought about in between. Idle is its
		// normal state, not a sign the caller has gone. (How long a call may TAKE is not bounded here: a
		// verb that touches metadata waits its turn, and the engine answers on its own after five minutes
		// — whoever times out should be the one that can explain why.)
		options.m_idleSeconds = 10 * 60;

		// One assistant, one person: an I/O thread and a handful of workers — a verb waiting for the
		// server holds one.
		options.m_threads = 1;
		options.m_workers = 8;

		Route();

		// ⭐ THE CONSEQUENCE FIRST, THEN THE CAUSE — and the likely holder named. "The port is already
		// taken" alone leaves the reader to work out that the assistant is therefore NOT LISTENING, and
		// the symptom on the other side is a client that cannot connect, which looks exactly like the
		// access being switched off (2026-09-23: two designers open, the second one silently without a
		// server). Another window already open is the ordinary reason, not a fault.
		//
		// 🛑 One listener per port is said to the operating system by the server (SO_EXCLUSIVEADDRUSE on
		// Windows): without it the second window binds the port too, and which base an assistant edits
		// is whichever answered.
		std::string error;
		if (!m_server.Start(options, error)) {
			refusal = ibString::Format(
				_("Assistant access did not start: %s.\n\n"
				  "If another window is open, it holds the port - only one can. "
				  "Close it, or give this one a port of its own in the assistant access settings."),
				ibString::FromUTF8(error));
			return false;
		}

		return true;
	}

	// The port closed and the connections let go — the stream's among them — before this returns, so
	// a restart on the same port cannot race the old listener.
	void Stop()
	{
		m_server.Stop();

		std::lock_guard<std::mutex> lock(m_streamMutex);
		m_stream.reset();
	}

	// One event out. Silent when nobody is listening — which is the ordinary
	// state, not a failure.
	//
	// Sent under the lock: what two threads say reaches the stream in the order it was said.
	void WriteToStream(const ibString& json)
	{
		std::lock_guard<std::mutex> lock(m_streamMutex);

		if (!m_stream)
			return;   // nobody is listening — dropped

		m_stream->Send(json.ToUtf8());
	}

private:

	// THE ONE LISTENING, told its stream is gone — so the listener stops holding it. A stream replaced
	// by a newer one finds another held, and leaves it be.
	class ibStreamRelay : public ibEventStreamHandler {
	public:

		ibStreamRelay(ibMcpListener& listener, std::shared_ptr<ibEventStream> stream)
			: m_listener(listener), m_stream(std::move(stream))
		{
		}

		virtual void OnClose() override
		{
			std::lock_guard<std::mutex> lock(m_listener.m_streamMutex);
			if (m_listener.m_stream == m_stream)
				m_listener.m_stream.reset();
		}

	private:
		ibMcpListener&                 m_listener;
		std::shared_ptr<ibEventStream> m_stream;
	};

	// ⭐ THE ONE QUESTION ASKED OF EVERY REQUEST. `Authorization: Bearer <token>`, compared whole.
	//
	// ⚠ AN EMPTY TOKEN REFUSES EVERYTHING. It is tempting to read "no token configured" as "no
	// checking wanted", and that reading is how a security setting ends up switched off by
	// omission on the one installation nobody looked at. The token is minted when the settings are
	// saved, so blank means something went wrong rather than something was chosen — and the honest
	// answer to that is no.
	bool IsAuthorised(const ibHttpRequest& req) const
	{
		const ibString expected = m_settings.m_token;
		if (expected.IsEmpty())
			return false;

		const std::string header = req.GetHeader("authorization");
		if (header.empty())
			return false;

		const ibString given = ibString::FromUTF8(header.c_str(), header.size());

		// The scheme is part of it: a client that sends the bare token is misconfigured in a way
		// worth telling apart from one that sends the wrong one.
		if (!given.StartsWith(ibT("Bearer ")))
			return false;

		// In constant time, as the session's token is compared: a comparison that stops at the first wrong
		// byte tells a caller timing it how many bytes were right.
		return ibSessionToken::TextEqual(std::string(given.Mid(7).Trim(true).Trim(false).utf8_str()),
			std::string(expected.utf8_str()));
	}

	// ⭐⭐ THE REFUSAL SAYS WHAT IS MISSING AND WHERE IT LIVES — because this is the FIRST thing a
	// new client ever meets here. The token is not guessable and is deliberately not derivable, so
	// saying WHERE it is shown gives away nothing: without the person's own settings page in front of
	// you, the sentence is useless. What it saves is the half hour spent deciding whether the server
	// is even up.
	static void RefuseUnauthorised(ibHttpResponse& res)
	{
		res.m_status = 401;
		// The scheme, as the HTTP spec asks — a client that reads headers can act on this one
		// without parsing a body it has no reason to expect.
		res.m_headers.emplace_back("WWW-Authenticate", "Bearer realm=\"OES Enterprise MCP\"");
		res.SetContent("{\"jsonrpc\":\"2.0\",\"error\":{\"code\":-32001,"
			"\"message\":\"Unauthorized: this server requires an Authorization: Bearer <token> "
			"header on every request, including the event stream.\","
			"\"data\":{\"where\":\"The token is minted with the settings and shown on the assistant "
			"access settings page, with a Copy button and a ready-made mcpServers block beside it. "
			"It is persisted, so it does not change between starts.\","
			"\"scheme\":\"Bearer\"}}}", "application/json");
	}

	// ⭐ WHERE THE CALLER IS SPEAKING FROM — a DIFFERENT question from who they are, and the
	// transport requires both. A browser on any web page can POST to 127.0.0.1; DNS rebinding
	// makes that page's own origin look local to the network stack, and nothing about the packet
	// says it came from a page rather than from a tool. The Origin header is what the browser
	// attaches and cannot be forged by script, so refusing an unknown one is the only defence a
	// local server has against being driven by a site the person merely visited.
	//
	// PRESENCE IS THE DISCRIMINATOR: an ordinary client (a CLI, an editor plugin) sends no Origin
	// at all, and the spec asks for a refusal only when the header IS present and wrong — so this
	// costs nothing for the callers that are not browsers. A loopback origin is allowed because
	// that is a page served by this machine's own tooling.
	static bool IsOriginAllowed(const ibHttpRequest& req)
	{
		const std::string origin = req.GetHeader("origin");
		if (origin.empty())
			return true;   // not a browser — nothing to check

		const ibString value = ibString::FromUTF8(origin.c_str(), origin.size()).Lower();
		return value.StartsWith(ibT("http://127.0.0.1"))
			|| value.StartsWith(ibT("https://127.0.0.1"))
			|| value.StartsWith(ibT("http://localhost"))
			|| value.StartsWith(ibT("https://localhost"))
			|| value == ibT("null");   // a file:// page, which is local by construction
	}

	// ⭐ WHO IS SPEAKING AND WHERE FROM, asked of both verbs and before the body is read. True — refused,
	// and the answer is in `res`. A listen stream hands over MORE than any single call does, so it is the
	// last place to relax either.
	bool Refuse(const ibHttpRequest& req, ibHttpResponse& res) const
	{
		// The address is not an authorisation: a loopback port is open to every process on the
		// machine, and this one works as the person whose session it lives in.
		if (!IsAuthorised(req)) {
			RefuseUnauthorised(res);
			return true;
		}
		if (!IsOriginAllowed(req)) {
			res.m_status = 403;
			res.SetContent("{\"jsonrpc\":\"2.0\",\"error\":{\"code\":-32000,"
				"\"message\":\"Origin not allowed\"}}", "application/json");
			return true;
		}
		return false;
	}

	// ⭐ THE TRANSPORT'S WHOLE JOB: what was heard goes to the server as it is — the body and the headers
	// MCP mirrors it into, all read there — and the answer comes back with the status the server gave it.
	// A body empty asks the headers alone. False — the server could not be asked, and the answer is in `res`.
	bool Carry(const ibHttpRequest& req, const ibString& body, ibHttpResponse& res, ibString& answer) const
	{
		const auto headerOf = [&req](const char* name) -> ibString {
			const std::string v = req.GetHeader(name);
			return v.empty() ? ibString() : ibString::FromUTF8(v.c_str(), v.size());
		};

		ibProtocolNode params, result;
		params.SetValue(ibProtocol::ibMcp::Command, static_cast<int>(ibProtocol::ibMcp::ibCommand::Message))
			.SetValue(ibProtocol::ibMcp::Message, body)
			.SetValue(ibProtocol::ibMcp::ProtocolVersion, headerOf("mcp-protocol-version"))
			.SetValue(ibProtocol::ibMcp::Method, headerOf("mcp-method"))
			.SetValue(ibProtocol::ibMcp::Name, headerOf("mcp-name"));

		ibProtocol::ibRefusal refusal = ibProtocol::ibRefusal::None;
		ibString error;
		if (!m_frame->Mcp(params, result, refusal, error)) {
			res.m_status = 503;
			res.SetContent("{\"jsonrpc\":\"2.0\",\"error\":{\"code\":-32000,"
				"\"message\":\"The platform did not answer: the connection to the base was lost, or the person's "
				"roles no longer give the assistant. A call under way may have been carried out, so check its "
				"result before repeating it. The next call opens the connection again.\"}}", "application/json");
			return false;
		}
		res.m_status = static_cast<int>(result.GetInt(ibProtocol::ibMcp::Status, 200));
		answer = result.GetString(ibProtocol::ibMcp::Message);
		return true;
	}

	// The two verbs of the protocol, on one path.
	void Route()
	{
		// A POST IS A QUESTION: one JSON-RPC message in, one answer out.
		m_server.Post("/", [this](const ibHttpRequest& req, ibHttpResponder respond) {

			ibHttpResponse res;
			if (Refuse(req, res)) {
				respond(std::move(res));
				return;
			}

			// 🛑 THE BYTES ARE UTF-8, OR THE CALL IS REFUSED. GetText reads bytes that are not UTF-8 all the same,
			// and a body a shell sent in its code page (curl's arguments on Windows, 2026-10-30) arrived as
			// letters nobody typed - to be stored in a module or said to the person without a word.
			if (!ibIsUtf8(req.m_body)) {
				res.m_status = 400;
				res.SetContent("{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32700,"
					"\"message\":\"The request is not UTF-8. Send its body as UTF-8 bytes - from a file or a pipe: "
					"a shell hands its arguments over in its own code page.\"}}", "application/json");
				respond(std::move(res));
				return;
			}

			ibString answer;
			if (Carry(req, req.GetText(), res, answer) && !answer.IsEmpty())
				res.SetText(answer, "application/json");
			respond(std::move(res));
		});

		// A GET IS AN OFFER TO LISTEN. It asks nothing — it hands the server
		// somewhere to speak, and is kept open until one side goes away.
		m_server.EventStream("/", [this](const std::shared_ptr<ibEventStream>& stream, ibHttpResponse& refusal)
			-> std::shared_ptr<ibEventStreamHandler> {

			if (Refuse(stream->GetRequest(), refusal))
				return nullptr;

			// The headers alone asked of the server: the revision it is spoken in is its to accept.
			ibString answer;
			if (!Carry(stream->GetRequest(), ibString(), refusal, answer))
				return nullptr;
			if (refusal.m_status >= 400) {
				refusal.SetText(answer, "application/json");
				return nullptr;
			}

			// ONE LISTENER AT A TIME, still. A second offer replaces the first
			// rather than joining it: two clients each half-hearing a conversation
			// is worse than one hearing all of it, and the person at the window
			// is talking to ONE assistant. The older stream is ended.
			std::shared_ptr<ibEventStream> replaced;
			{
				std::lock_guard<std::mutex> lock(m_streamMutex);
				replaced = std::move(m_stream);
				m_stream = stream;
			}
			if (replaced)
				replaced->Close();

			return std::make_shared<ibStreamRelay>(*this, stream);
		});
	}

	ibFrontendMainFrame* const m_frame;
	const ibMcpSettings        m_settings;

	ibHttpServer  m_server;   // plain, as server/listener.h says; started in Start

	// The way out — the one stream listening, or none. Said to by what the server says unasked.
	std::mutex                     m_streamMutex;
	std::shared_ptr<ibEventStream> m_stream;
};

//---------------------------------------------------------------------------
// the access
//---------------------------------------------------------------------------

ibMcpServer::ibMcpServer() = default;
ibMcpServer::~ibMcpServer() = default;

void ibMcpServer::Receive()
{
	ibProtocolNode params, shown;
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	params.SetValue(ibProtocol::ibSchema::Schema, static_cast<long long>(ibProtocol::ibSchema::ibKind::McpServer));
	ibProtocol::ibRefusal refusal = ibProtocol::ibRefusal::None;
	ibString error;
	m_shown = frame != nullptr && frame->Call(ibProtocol::ibMethod::Schema, params, shown, refusal, error) ? shown : ibProtocolNode();
}

bool ibMcpServer::IsAllowed() const
{
	return m_shown.GetBool(ibProtocol::ibSchema::ibMcpServer::Allowed);
}

ibMcpSettings ibMcpServer::GetSettings() const
{
	wxConfigBase* const config = wxConfigBase::Get();
	const wxString group = SettingsGroup();

	ibMcpSettings settings;
	settings.m_enabled = config->ReadBool(group + wxT("Enabled"), settings.m_enabled);
	settings.m_address = config->Read(group + wxT("Address"), wxString(settings.m_address));
	settings.m_port = static_cast<unsigned short>(config->ReadLong(group + wxT("Port"),
		appData->DesignerMode() ? ibPort::Assistant.m_first : ibPort::Assistant.Last()));
	settings.m_token = config->Read(group + wxT("Token"), wxString());

	// Kept as it is made: a token made again next time would refuse every client told this one.
	if (settings.m_token.IsEmpty()) {
		const std::vector<unsigned char> raw = ibSessionToken::Generate();
		settings.m_token = ibSessionToken::Hex(raw.data(), raw.size());
		config->Write(group + wxT("Token"), wxString(settings.m_token));
		config->Flush();
	}
	return settings;
}

void ibMcpServer::SaveSettings(const ibMcpSettings& settings)
{
	wxConfigBase* const config = wxConfigBase::Get();
	const wxString group = SettingsGroup();
	config->Write(group + wxT("Enabled"), settings.m_enabled);
	config->Write(group + wxT("Address"), wxString(settings.m_address));
	config->Write(group + wxT("Port"), static_cast<long>(settings.m_port));
	config->Write(group + wxT("Token"), wxString(settings.m_token));
	config->Flush();
}

bool ibMcpServer::Start(ibString& refusal)
{
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	if (frame == nullptr)
		return false;

	if (IsRunning()) {
		refusal = _("The server is already running");
		return false;
	}

	const ibMcpSettings wanted = GetSettings();
	if (!wanted.m_enabled) {
		// Says WHERE to turn it on, not just that it is off: a refusal that
		// leaves a person hunting through menus has only done half its job.
		refusal = _("Assistant access is switched off.\n\nTurn it on in the assistant access settings.");
		return false;
	}
	if (!wanted.IsOk()) {
		refusal = _("The address or the port is not set");
		return false;
	}

	// ⭐ BOUND, THEN THE PORT: nobody is answered "not connected" by a port that just opened, and a port that cannot be
	// taken leaves no binding behind. Told before the port opens: what is said then has nobody to reach, and is dropped.
	ibProtocol::ibRefusal refused = ibProtocol::ibRefusal::None;
	auto listener = std::make_unique<ibMcpListener>(frame, wanted);
	ibMcpListener* const stream = listener.get();
	if (!frame->ListenMcp([stream](const ibProtocolNode& news) {
		stream->WriteToStream(news.GetString(ibProtocol::ibMcp::Message));
	}, refused, refusal))
		return false;
	if (!listener->Start(refusal)) {
		ibString ignored;
		frame->ListenMcp(nullptr, refused, ignored);
		return false;
	}

	m_listener = std::move(listener);
	m_endpoint = ibString::Format(ibT("http://%s:%u/"), wanted.m_address, (unsigned)wanted.m_port);
	ShowLamp(frame, m_endpoint);
	return true;
}

void ibMcpServer::Stop()
{
	if (m_listener == nullptr)
		return;

	// The port first — nothing new is heard, and whatever is being carried is done; then the connection.
	m_listener->Stop();
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	if (frame != nullptr) {
		ibProtocol::ibRefusal refused = ibProtocol::ibRefusal::None;
		ibString ignored;
		frame->ListenMcp(nullptr, refused, ignored);
		ShowLamp(frame, ibString());
	}
	m_listener.reset();
	m_endpoint.clear();
}

bool ibMcpServer::IsRunning() const
{
	return m_listener != nullptr;
}

ibString ibMcpServer::GetEndpoint() const
{
	return m_endpoint;
}
