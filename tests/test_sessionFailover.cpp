// A thin client's session outlives a dropped connection (docs/public/session-failover.md, stage 1).
//
// Three cases against a file base, through the same door a socket uses (ibClientHost::Call of the JSON-RPC text),
// and one against the communicator alone: its ids stay monotonic when the socket drops and it logs in by token.

#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>

#include <wx/filename.h>
#include <wx/image.h>    // wxInitAllImageHandlers — a new base loads icons, and wx decodes nothing until registered
#include <wx/init.h>
#include <wx/log.h>      // wxLogStderr — a warning must not become a modal box and hang a headless run

#include "backend/appData.h"
#include "backend/appHost.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/databaseLayer.h"
#ifdef OES_USE_FIREBIRD
#include "backend/databaseLayer/firebird/firebirdDatabaseLayer.h"
#endif
#include "backend/utils/sessionToken.h"
#include "core/exception.h"

#include "frmserver/client/clientHost.h"

#include "protocol/communicator.h"
#include "protocol/connection.h"
#include "protocol/protocolNode.h"

namespace {

// Left out, Resume is 120, which is too long for a test. 0 is off. The file is the process's, written before any
// base is opened (the host reads it once), and removed when this process goes.
struct ResumeFile {
	bool created = false;
	ResumeFile()
	{
		std::ifstream existing("backend.conf");
		if (existing.good())
			return;
		std::ofstream out("backend.conf");
		out << "Resume=2\n";
		created = out.good();
	}
	~ResumeFile()
	{
		if (created)
			std::remove("backend.conf");
	}
};

ResumeFile g_resumeFile;

struct Rpc {
	bool           ok = false;
	ibProtocolNode result;
	long long      code = 0;
	wxString       message;
};

Rpc ReadRpc(const wxString& text)
{
	Rpc rpc;
	ibProtocolNode root;
	if (!ibProtocolNode::Read(std::string(text.utf8_str()), root)) {
		rpc.message = text;
		return rpc;
	}
	const ibProtocolNode failed = root.FindChild(ibProtocolName::RpcError);
	if (failed.IsNode()) {
		rpc.code = failed.GetInt(ibProtocolName::RpcCode);
		rpc.message = failed.GetString(ibProtocolName::RpcMessage);
		return rpc;
	}
	rpc.result = root.FindChild(ibProtocolName::RpcResult);
	rpc.ok = rpc.result.IsNode();
	if (!rpc.ok)
		rpc.message = text;
	return rpc;
}

wxString Request(long long id, const char* method, const ibProtocolNode& params)
{
	ibProtocolNode request;
	request.SetValue(ibProtocolName::RpcVersion, "2.0")
		.SetValue(ibProtocolName::RpcId, id)
		.SetValue(ibProtocolName::RpcMethod, method)
		.SetValue(ibProtocolName::RpcParams, params);
	return wxString::FromUTF8(request.Write());
}

wxString TokenHashOf(const wxString& token)
{
	unsigned char raw[ibSessionToken::kBytes];
	if (!ibSessionToken::ParseHex(token, raw))
		return wxString();
	return ibSessionToken::HashHex(raw, ibSessionToken::kBytes);
}

wxString StoredHash(const wxString& sessionId)
{
	ibConnectionScope scope = ibConnectionPool::GetFreeConnection();
	if (!scope)
		return wxString();
	ibStatementGuard statement(scope.shared(), scope->PrepareStatement(wxT("SELECT tokenHash FROM sys_session WHERE session = ?")));
	if (!statement)
		return wxString();
	statement->SetParamString(1, sessionId);
	ibResultSetGuard rows(scope.shared(), statement->RunQueryWithResults());
	if (!rows.get() || !rows->Next())
		return wxString();
	return rows->GetResultString(1);
}

bool SessionRowExists(const wxString& sessionId)
{
	ibConnectionScope scope = ibConnectionPool::GetFreeConnection();
	if (!scope)
		return false;
	ibStatementGuard statement(scope.shared(), scope->PrepareStatement(wxT("SELECT session FROM sys_session WHERE session = ?")));
	if (!statement)
		return false;
	statement->SetParamString(1, sessionId);
	ibResultSetGuard rows(scope.shared(), statement->RunQueryWithResults());
	return rows.get() && rows->Next();
}

struct Base {
	wxInitializer                          wx;
	wxString                               dir;
	wxString                               reason;
	bool                                   failed = false;   // the client is here and the base still did not open
	ibApplicationInstance*                 app = nullptr;
	std::unique_ptr<ibClientHost>          host;

	void Open()
	{
		if (!wx.IsOk()) {
			reason = wxT("wxBase init failed");
			return;
		}
		// The same two a headless runtime test sets: icons the configuration loads, and no modal log.
		wxInitAllImageHandlers();
		if (wxLog::GetActiveTarget() != nullptr)
			delete wxLog::SetActiveTarget(new wxLogStderr());

		dir = wxFileName::CreateTempFileName(wxT("oes-failover"));
		wxRemoveFile(dir);
		if (!wxFileName::Mkdir(dir)) {
			failed = true;
			reason = wxT("the base directory was not made");
			return;
		}

		ibFileInstanceRequest request;
		request.m_name = wxT("failover");
		request.m_directory = dir;
		request.m_create = true;
		try {
			app = ibApplicationInstance::CreateAppDataEnv(request);
		}
		catch (const ibCoreException& err) {
			reason = err.GetErrorDescription();
		}
		if (app == nullptr) {
			// No client on this machine is a skip, the way the other Firebird tests skip. A client that
			// loaded and still did not open the base is a failure.
			bool client = false;
#ifdef OES_USE_FIREBIRD
			client = ibDatabaseLayerFirebird::IsAvailable();
#endif
			if (client) {
				failed = true;
				if (reason.IsEmpty())
					reason = wxT("the base did not open");
			}
			else
				reason = wxT("no Firebird client on this machine");
			return;
		}
		host = std::make_unique<ibClientHost>(app);
		if (ibApplicationHost::Get() == nullptr || ibApplicationHost::Get()->GetResumeSeconds() != 2u) {
			failed = true;
			reason = wxT("Resume was not 2 — backend.conf was not the one this test wrote");
		}
	}

	void Close()
	{
		host.reset();
		if (app != nullptr)
			ibApplicationInstance::DestroyAppDataEnv();
		app = nullptr;
		if (!dir.IsEmpty())
			std::filesystem::remove_all(std::string(dir.utf8_str()));
	}

	Rpc Call(const wxString& text, const void* connection)
	{
		return ReadRpc(host->Call(text, connection, wxT("test")));
	}
};

Base* g_base = nullptr;

ibProtocolNode PasswordLogin(bool resume = true)
{
	ibProtocolNode params;
	params.SetValue(ibProtocolName::User, wxString())
		.SetValue(ibProtocolName::Password, wxString())
		.SetValue(ibProtocolName::Protocol, ibProtocolVersion);
	if (resume)
		params.AddItem(ibProtocolName::Features, wxString::FromUTF8(ibProtocolName::FeatureResume));
	return params;
}

bool ListsResume(const ibProtocolNode& result)
{
	for (const ibProtocolNode& feature : result.GetList(ibProtocolName::Features)) {
		if (feature.AsString() == wxString::FromUTF8(ibProtocolName::FeatureResume))
			return true;
	}
	return false;
}

} // namespace

class SessionFailover : public ::testing::Test {
protected:
	static void SetUpTestSuite()
	{
		g_base = new Base;
		g_base->Open();
	}
	static void TearDownTestSuite()
	{
		if (g_base == nullptr)
			return;
		g_base->Close();
		delete g_base;
		g_base = nullptr;
	}
	void SetUp() override
	{
		if (g_base == nullptr)
			GTEST_FAIL() << "the base was not started";
		if (g_base->failed)
			GTEST_FAIL() << g_base->reason;
		if (g_base->host == nullptr)
			GTEST_SKIP() << g_base->reason;
	}
};

TEST_F(SessionFailover, DropAndReturnByToken)
{
	int connection = 0;
	const Rpc login = g_base->Call(Request(1, "login", PasswordLogin()), &connection);
	ASSERT_TRUE(login.ok) << login.message;
	const wxString client = login.result.GetString(ibProtocolName::Client);
	const wxString token = login.result.GetString(ibProtocolName::Token);
	const long long frame = login.result.GetInt(ibProtocolName::Frame);
	ASSERT_FALSE(client.IsEmpty());
	ASSERT_FALSE(token.IsEmpty());
	ASSERT_NE(frame, 0);
	ASSERT_TRUE(login.result.Has(ibProtocolName::Frame));

	const wxString hash = TokenHashOf(token);
	ASSERT_FALSE(hash.IsEmpty());
	EXPECT_NE(hash, token);
	EXPECT_EQ(StoredHash(client), hash);

	g_base->host->Disconnect(&connection);

	ibProtocolNode frameParams;
	frameParams.SetValue(ibProtocolName::Client, client);
	const Rpc refused = g_base->Call(Request(2, "frame", frameParams), &connection);
	EXPECT_FALSE(refused.ok);
	EXPECT_EQ(refused.code, 401);

	int returned = 0;
	ibProtocolNode tokenParams;
	tokenParams.SetValue(ibProtocolName::Token, token).SetValue(ibProtocolName::Protocol, ibProtocolVersion);
	const Rpc again = g_base->Call(Request(3, "login", tokenParams), &returned);
	ASSERT_TRUE(again.ok) << again.message;
	EXPECT_EQ(again.result.GetString(ibProtocolName::Client), client);
	EXPECT_FALSE(again.result.Has(ibProtocolName::Frame));
	EXPECT_FALSE(again.result.Has(ibProtocolName::Patch));
	EXPECT_TRUE(again.result.GetString(ibProtocolName::Token).IsEmpty());

	ibProtocolNode since;
	since.SetValue(ibProtocolName::Client, client).SetValue(ibProtocolName::Since, frame);
	const Rpc patched = g_base->Call(Request(4, "frame", since), &returned);
	ASSERT_TRUE(patched.ok) << patched.message;
	EXPECT_TRUE(patched.result.FindChild(ibProtocolName::Patch).IsNode());
	EXPECT_EQ(patched.result.GetInt(ibProtocolName::Since), frame);
	EXPECT_FALSE(patched.result.Has(ibProtocolName::Title));
	EXPECT_EQ(patched.result.GetInt(ibProtocolName::Frame), frame + 1);
}

TEST_F(SessionFailover, LostCallIsNotExecutedTwice)
{
	int connection = 0;
	const Rpc login = g_base->Call(Request(10, "login", PasswordLogin()), &connection);
	ASSERT_TRUE(login.ok) << login.message;
	const wxString client = login.result.GetString(ibProtocolName::Client);
	const long long frame = login.result.GetInt(ibProtocolName::Frame);

	ibProtocolNode params;
	params.SetValue(ibProtocolName::Client, client).SetValue(ibProtocolName::Since, frame);
	const wxString firstText = g_base->host->Call(Request(11, "frame", params), &connection, wxT("test"));
	const Rpc first = ReadRpc(firstText);
	ASSERT_TRUE(first.ok) << first.message;
	const long long drawn = first.result.GetInt(ibProtocolName::Frame);

	const wxString againText = g_base->host->Call(Request(11, "frame", params), &connection, wxT("test"));
	EXPECT_EQ(againText, firstText);

	const Rpc third = g_base->Call(Request(12, "frame", params), &connection);
	ASSERT_TRUE(third.ok) << third.message;
	// The repeated id did not draw a frame. A new id draws the next one, not the one after that.
	EXPECT_EQ(third.result.GetInt(ibProtocolName::Frame), drawn + 1);
}

TEST_F(SessionFailover, ExpiredWindowClosesTheSession)
{
	int connection = 0;
	const Rpc login = g_base->Call(Request(20, "login", PasswordLogin()), &connection);
	ASSERT_TRUE(login.ok) << login.message;
	const wxString client = login.result.GetString(ibProtocolName::Client);
	const wxString token = login.result.GetString(ibProtocolName::Token);
	ASSERT_TRUE(SessionRowExists(client));

	g_base->host->Disconnect(&connection);
	const std::size_t seconds = ibApplicationHost::Get()->GetResumeSeconds();
	ASSERT_LE(seconds, 5u);
	std::this_thread::sleep_for(std::chrono::seconds(seconds + 2));

	int returned = 0;
	ibProtocolNode tokenParams;
	tokenParams.SetValue(ibProtocolName::Token, token);
	const Rpc again = g_base->Call(Request(21, "login", tokenParams), &returned);
	EXPECT_FALSE(again.ok);
	EXPECT_EQ(again.code, 401);
	EXPECT_FALSE(SessionRowExists(client));
}

TEST_F(SessionFailover, WrongTokenIsRefusedAndTheSessionStays)
{
	int connection = 0;
	const Rpc login = g_base->Call(Request(30, "login", PasswordLogin()), &connection);
	ASSERT_TRUE(login.ok) << login.message;
	const wxString client = login.result.GetString(ibProtocolName::Client);
	ASSERT_TRUE(SessionRowExists(client));

	int other = 0;
	ibProtocolNode bad;
	bad.SetValue(ibProtocolName::Token, wxString(wxT("not-a-token")));
	const Rpc malformed = g_base->Call(Request(31, "login", bad), &other);
	EXPECT_FALSE(malformed.ok);
	EXPECT_EQ(malformed.code, 401);

	ibProtocolNode unknown;
	unknown.SetValue(ibProtocolName::Token, wxString(wxT("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef")));
	const Rpc refused = g_base->Call(Request(32, "login", unknown), &other);
	EXPECT_FALSE(refused.ok);
	EXPECT_EQ(refused.code, 401);
	EXPECT_TRUE(SessionRowExists(client));

	ibProtocolNode frameParams;
	frameParams.SetValue(ibProtocolName::Client, client);
	const Rpc still = g_base->Call(Request(33, "frame", frameParams), &connection);
	EXPECT_TRUE(still.ok) << still.message;
}

TEST_F(SessionFailover, LiveSessionIsNotTakenByToken)
{
	int connection = 0;
	const Rpc login = g_base->Call(Request(40, "login", PasswordLogin()), &connection);
	ASSERT_TRUE(login.ok) << login.message;
	const wxString client = login.result.GetString(ibProtocolName::Client);
	const wxString token = login.result.GetString(ibProtocolName::Token);
	ASSERT_FALSE(token.IsEmpty());

	int intruder = 0;
	ibProtocolNode tokenParams;
	tokenParams.SetValue(ibProtocolName::Token, token);
	const Rpc stolen = g_base->Call(Request(41, "login", tokenParams), &intruder);
	EXPECT_FALSE(stolen.ok);
	EXPECT_EQ(stolen.code, 401);

	ibProtocolNode frameParams;
	frameParams.SetValue(ibProtocolName::Client, client);
	const Rpc still = g_base->Call(Request(42, "frame", frameParams), &connection);
	ASSERT_TRUE(still.ok) << still.message;
	const Rpc intruderFrame = g_base->Call(Request(43, "frame", frameParams), &intruder);
	EXPECT_FALSE(intruderFrame.ok);
	EXPECT_EQ(intruderFrame.code, 401);
}

TEST_F(SessionFailover, DesignerGetsNoToken)
{
	int connection = 0;
	ibProtocolNode params = PasswordLogin();
	params.SetValue(ibProtocolName::Mode, static_cast<int>(ibProtocolMode::Designer));
	const Rpc login = g_base->Call(Request(55, "login", params), &connection);
	ASSERT_TRUE(login.ok) << login.message;
	EXPECT_TRUE(login.result.GetString(ibProtocolName::Token).IsEmpty());
	EXPECT_FALSE(ListsResume(login.result));
	const wxString client = login.result.GetString(ibProtocolName::Client);
	g_base->host->Disconnect(&connection);
	EXPECT_FALSE(SessionRowExists(client));
}

TEST_F(SessionFailover, NoTokenWithoutASocket)
{
	const Rpc login = g_base->Call(Request(50, "login", PasswordLogin()), nullptr);
	ASSERT_TRUE(login.ok) << login.message;
	EXPECT_TRUE(login.result.GetString(ibProtocolName::Token).IsEmpty());
	EXPECT_FALSE(ListsResume(login.result));
	EXPECT_TRUE(login.result.Has(ibProtocolName::Frame));
}

TEST_F(SessionFailover, RepeatedIdWithoutOptInRunsAgain)
{
	int connection = 0;
	const Rpc login = g_base->Call(Request(60, "login", PasswordLogin(false)), &connection);
	ASSERT_TRUE(login.ok) << login.message;
	EXPECT_FALSE(login.result.GetString(ibProtocolName::Token).IsEmpty());
	const wxString client = login.result.GetString(ibProtocolName::Client);
	const long long frame = login.result.GetInt(ibProtocolName::Frame);

	ibProtocolNode params;
	params.SetValue(ibProtocolName::Client, client).SetValue(ibProtocolName::Since, frame);
	const Rpc first = g_base->Call(Request(61, "frame", params), &connection);
	ASSERT_TRUE(first.ok) << first.message;
	const Rpc again = g_base->Call(Request(61, "frame", params), &connection);
	ASSERT_TRUE(again.ok) << again.message;
	EXPECT_EQ(again.result.GetInt(ibProtocolName::Frame), first.result.GetInt(ibProtocolName::Frame) + 1);
}

TEST_F(SessionFailover, OlderIdAndADifferentCallAreRefused)
{
	int connection = 0;
	const Rpc login = g_base->Call(Request(70, "login", PasswordLogin()), &connection);
	ASSERT_TRUE(login.ok) << login.message;
	const wxString client = login.result.GetString(ibProtocolName::Client);
	const long long frame = login.result.GetInt(ibProtocolName::Frame);

	ibProtocolNode params;
	params.SetValue(ibProtocolName::Client, client).SetValue(ibProtocolName::Since, frame);
	const Rpc first = g_base->Call(Request(71, "frame", params), &connection);
	ASSERT_TRUE(first.ok) << first.message;
	const long long drawn = first.result.GetInt(ibProtocolName::Frame);

	ibProtocolNode next = params;
	const Rpc second = g_base->Call(Request(72, "frame", next), &connection);
	ASSERT_TRUE(second.ok) << second.message;

	const Rpc replay = g_base->Call(Request(71, "frame", params), &connection);
	EXPECT_FALSE(replay.ok);
	EXPECT_EQ(replay.code, 400);

	ibProtocolNode other = params;
	other.SetValue(ibProtocolName::Since, frame + 50);
	const Rpc mismatch = g_base->Call(Request(72, "frame", other), &connection);
	EXPECT_FALSE(mismatch.ok);
	EXPECT_EQ(mismatch.code, 400);

	const Rpc onward = g_base->Call(Request(73, "frame", params), &connection);
	ASSERT_TRUE(onward.ok) << onward.message;
	EXPECT_EQ(onward.result.GetInt(ibProtocolName::Frame), drawn + 2);
}

TEST_F(SessionFailover, DuplicateIdRunsOnce)
{
	int connection = 0;
	const Rpc login = g_base->Call(Request(80, "login", PasswordLogin()), &connection);
	ASSERT_TRUE(login.ok) << login.message;
	const wxString client = login.result.GetString(ibProtocolName::Client);
	const long long frame = login.result.GetInt(ibProtocolName::Frame);

	ibProtocolNode params;
	params.SetValue(ibProtocolName::Client, client).SetValue(ibProtocolName::Since, frame);
	const wxString text = Request(81, "frame", params);
	wxString first;
	wxString second;
	std::thread one([&]() { first = g_base->host->Call(text, &connection, wxT("test")); });
	std::thread two([&]() { second = g_base->host->Call(text, &connection, wxT("test")); });
	one.join();
	two.join();
	const Rpc a = ReadRpc(first);
	const Rpc b = ReadRpc(second);
	ASSERT_TRUE(a.ok) << a.message;
	ASSERT_TRUE(b.ok) << b.message;
	EXPECT_EQ(first, second);
	EXPECT_EQ(a.result.GetInt(ibProtocolName::Frame), frame + 1);

	const Rpc third = g_base->Call(Request(81, "frame", params), &connection);
	ASSERT_TRUE(third.ok) << third.message;
	EXPECT_EQ(ReadRpc(g_base->host->Call(text, &connection, wxT("test"))).result.GetInt(ibProtocolName::Frame), frame + 1);

	const Rpc next = g_base->Call(Request(82, "frame", params), &connection);
	ASSERT_TRUE(next.ok) << next.message;
	EXPECT_EQ(next.result.GetInt(ibProtocolName::Frame), frame + 2);
}

TEST_F(SessionFailover, AnotherConnectionDoesNotReadTheCachedAnswer)
{
	int connection = 0;
	const Rpc login = g_base->Call(Request(90, "login", PasswordLogin()), &connection);
	ASSERT_TRUE(login.ok) << login.message;
	const wxString client = login.result.GetString(ibProtocolName::Client);

	ibProtocolNode params;
	params.SetValue(ibProtocolName::Client, client);
	const wxString text = Request(91, "frame", params);
	const Rpc first = g_base->Call(text, &connection);
	ASSERT_TRUE(first.ok) << first.message;

	int other = 0;
	const wxString stolenText = g_base->host->Call(text, &other, wxT("test"));
	const Rpc stolen = ReadRpc(stolenText);
	EXPECT_FALSE(stolen.ok);
	EXPECT_EQ(stolen.code, 401);

	const wxString cachedText = g_base->host->Call(text, &connection, wxT("test"));
	const Rpc cached = ReadRpc(cachedText);
	EXPECT_TRUE(cached.ok) << cached.message;
	EXPECT_NE(stolenText, cachedText);
	EXPECT_EQ(cached.result.GetInt(ibProtocolName::Frame), first.result.GetInt(ibProtocolName::Frame));
}

namespace {

// A connection that fails one exchange, then accepts the login and the resent call.
class ScriptedConnection : public ibProtocolConnection {
public:
	std::vector<std::string> sent;
	std::vector<bool>        fail;
	std::vector<std::string> answers;
	int                      reconnects = 0;

	bool Exchange(const std::string& request, std::string& answer, ibProtocolRefusal& refusal, wxString& error) override
	{
		sent.push_back(request);
		if (fail.empty()) {
			refusal = ibProtocolRefusal::Failed;
			error = wxT("no script");
			return false;
		}
		const bool drop = fail.front();
		fail.erase(fail.begin());
		if (drop) {
			refusal = ibProtocolRefusal::NoSession;
			error = wxT("the connection to the server is closed");
			return false;
		}
		answer = answers.front();
		answers.erase(answers.begin());
		return true;
	}
	bool Reconnect(wxString&) override
	{
		++reconnects;
		return true;
	}
	void Listen(std::function<void(const std::string&)>) override {}
};

std::string Answer(long long id, const ibProtocolNode& result)
{
	ibProtocolNode root;
	root.SetValue(ibProtocolName::RpcVersion, "2.0")
		.SetValue(ibProtocolName::RpcId, id)
		.SetValue(ibProtocolName::RpcResult, result);
	return root.Write();
}

long long IdOf(const std::string& text)
{
	ibProtocolNode node;
	EXPECT_TRUE(ibProtocolNode::Read(text, node));
	return node.GetInt(ibProtocolName::RpcId);
}

} // namespace

TEST(SessionFailoverClient, IdsStayMonotonicAcrossReconnect)
{
	auto script = std::make_unique<ScriptedConnection>();
	ScriptedConnection* const raw = script.get();

	raw->fail = { false, true, false, false, false };
	// Features is a list of strings. The node writer has no string-list, so the password login's answer is the wire.
	raw->answers = {
		"{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"Client\":\"client-1\",\"Protocol\":2,\"Token\":\"tok\","
			"\"Features\":[\"resume\"],\"Frame\":1,\"Title\":\"Home\"}}",
		Answer(3, [&]() {
			ibProtocolNode resumed;
			resumed.SetValue(ibProtocolName::Client, wxString(wxT("client-1")))
				.SetValue(ibProtocolName::Protocol, 2);
			return resumed;
		}()),
		Answer(2, [&]() {
			ibProtocolNode frame;
			frame.SetValue(ibProtocolName::Frame, 2).SetValue(ibProtocolName::Title, wxString(wxT("Home")));
			return frame;
		}()),
		Answer(4, [&]() {
			ibProtocolNode frame;
			frame.SetValue(ibProtocolName::Frame, 3).SetValue(ibProtocolName::Title, wxString(wxT("Home")));
			return frame;
		}()),
	};

	ibCommunicator communicator(std::move(script));
	ibProtocolNode result;
	ibProtocolRefusal refusal = ibProtocolRefusal::None;
	wxString error;

	ASSERT_TRUE(communicator.Login(wxT("user"), wxT("secret"), ibProtocolMode::Runtime, result, refusal, error)) << error;

	ibProtocolNode params;
	ASSERT_TRUE(communicator.Call(ibProtocolMethod::Frame, params, result, refusal, error)) << error;
	EXPECT_EQ(raw->reconnects, 1);
	ASSERT_GE(raw->sent.size(), 4u);
	// 0 password login, 1 the frame that dropped, 2 login {Token}, 3 the same frame again.
	EXPECT_EQ(IdOf(raw->sent[1]), 2);
	EXPECT_EQ(IdOf(raw->sent[2]), 3);
	EXPECT_EQ(IdOf(raw->sent[3]), 2);
	EXPECT_NE(raw->sent[2].find("\"Token\""), std::string::npos);

	ASSERT_TRUE(communicator.Call(ibProtocolMethod::Frame, params, result, refusal, error)) << error;
	EXPECT_EQ(IdOf(raw->sent.back()), 4);
}

TEST(SessionFailoverResume, AbsentIs120AndZeroIsOff)
{
	EXPECT_EQ(ibApplicationHost::ResumeSeconds(false, 0), 120u);
	EXPECT_EQ(ibApplicationHost::ResumeSeconds(true, 0), 0u);
	EXPECT_EQ(ibApplicationHost::ResumeSeconds(true, 2), 2u);
	EXPECT_EQ(ibApplicationHost::ResumeSeconds(true, -1), 120u);
	EXPECT_EQ(ibApplicationHost::ResumeSeconds(true, 30 * 60), static_cast<std::size_t>(30 * 60));
	EXPECT_EQ(ibApplicationHost::ResumeSeconds(true, 30 * 60 + 1), static_cast<std::size_t>(30 * 60));
}
