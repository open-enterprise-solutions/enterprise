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
#include <wx/init.h>

#include "backend/appData.h"
#include "backend/appHost.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/utils/sessionToken.h"
#include "core/exception.h"

#include "frmserver/client/clientHost.h"

#include "protocol/communicator.h"
#include "protocol/connection.h"
#include "protocol/protocolNode.h"

namespace {

// Absent or 0 is 120, which is too long for a test. The file is the process's, written before any base is opened
// (the host reads it once), and removed when this process goes.
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
	ibConnectionPool* const pool = ibApplicationInstance::GetConnectionPool();
	if (pool == nullptr)
		return wxString();
	const std::shared_ptr<ibDatabaseLayer> conn = pool->Checkout();
	if (!conn)
		return wxString();
	ibStatementGuard statement(conn, conn->PrepareStatement(wxT("SELECT tokenHash FROM sys_session WHERE session = ?")));
	if (!statement)
		return wxString();
	statement->SetParamString(1, sessionId);
	ibResultSetGuard rows(conn, statement->RunQueryWithResults());
	if (!rows.get() || !rows->Next())
		return wxString();
	return rows->GetResultString(1);
}

bool SessionRowExists(const wxString& sessionId)
{
	ibConnectionPool* const pool = ibApplicationInstance::GetConnectionPool();
	if (pool == nullptr)
		return false;
	const std::shared_ptr<ibDatabaseLayer> conn = pool->Checkout();
	if (!conn)
		return false;
	ibStatementGuard statement(conn, conn->PrepareStatement(wxT("SELECT session FROM sys_session WHERE session = ?")));
	if (!statement)
		return false;
	statement->SetParamString(1, sessionId);
	ibResultSetGuard rows(conn, statement->RunQueryWithResults());
	return rows.get() && rows->Next();
}

struct Base {
	wxInitializer                          wx;
	wxString                               dir;
	ibApplicationInstance*                 app = nullptr;
	std::unique_ptr<ibClientHost>          host;

	void Open()
	{
		ASSERT_TRUE(wx.IsOk());
		dir = wxFileName::CreateTempFileName(wxT("oes-failover"));
		wxRemoveFile(dir);
		ASSERT_TRUE(wxFileName::Mkdir(dir));

		ibFileInstanceRequest request;
		request.m_name = wxT("failover");
		request.m_directory = dir;
		request.m_create = true;
		try {
			app = ibApplicationInstance::CreateAppDataEnv(request);
		}
		catch (const ibCoreException& err) {
			FAIL() << err.GetErrorDescription();
		}
		ASSERT_NE(app, nullptr);
		host = std::make_unique<ibClientHost>(app);
		ASSERT_NE(ibApplicationHost::Get(), nullptr);
		ASSERT_EQ(ibApplicationHost::Get()->GetResumeSeconds(), 2u);
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

ibProtocolNode PasswordLogin()
{
	ibProtocolNode params;
	params.SetValue(ibProtocolName::User, wxString())
		.SetValue(ibProtocolName::Password, wxString())
		.SetValue(ibProtocolName::Protocol, ibProtocolVersion);
	return params;
}

} // namespace

class SessionFailover : public ::testing::Test {
protected:
	static void SetUpTestSuite()
	{
		g_base = new Base;
		g_base->Open();
		if (g_base->host == nullptr) {
			g_base->Close();
			delete g_base;
			g_base = nullptr;
		}
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
		if (g_base == nullptr || g_base->host == nullptr)
			GTEST_FAIL() << "the base did not open";
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

	ibProtocolNode loginResult;
	loginResult.SetValue(ibProtocolName::Client, wxString(wxT("client-1")))
		.SetValue(ibProtocolName::Protocol, 2)
		.SetValue(ibProtocolName::Token, wxString(wxT("tok")))
		.SetValue(ibProtocolName::Frame, 1)
		.SetValue(ibProtocolName::Title, wxString(wxT("Home")));
	loginResult.AddItem(ibProtocolName::Features).SetValue(ibProtocolName::FeatureResume, wxString()); // wrong shape
	// Features is a list of strings. AddItem makes a node; the reader wants string items. Build the list by hand.
	loginResult = ibProtocolNode();
	loginResult.SetValue(ibProtocolName::Client, wxString(wxT("client-1")))
		.SetValue(ibProtocolName::Protocol, 2)
		.SetValue(ibProtocolName::Token, wxString(wxT("tok")))
		.SetValue(ibProtocolName::Frame, 1)
		.SetValue(ibProtocolName::Title, wxString(wxT("Home")));

	raw->fail = { false, true, false, false, false };
	raw->answers = {
		Answer(1, loginResult),
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

	// Features must be a JSON array of strings. Answer() writes the node; set the list on a copy the writer understands.
	{
		ibProtocolNode listed;
		listed.SetValue(ibProtocolName::Client, wxString(wxT("client-1")))
			.SetValue(ibProtocolName::Protocol, 2)
			.SetValue(ibProtocolName::Token, wxString(wxT("tok")))
			.SetValue(ibProtocolName::Frame, 1)
			.SetValue(ibProtocolName::Title, wxString(wxT("Home")));
		listed.AddItem(ibProtocolName::Features, 0); // a number is not the feature. Replaced below if the API allows strings.
		raw->answers[0] = Answer(1, listed);
	}

	ibCommunicator communicator(std::move(script));
	ibProtocolNode result;
	ibProtocolRefusal refusal = ibProtocolRefusal::None;
	wxString error;
	// The scripted feature list is fixed up after we see how AddItem writes a string — see the assertion on m_resume
	// by whether the drop is resumed. Build the login answer as JSON so Features is ["resume"].
	raw->answers[0] =
		"{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"Client\":\"client-1\",\"Protocol\":2,\"Token\":\"tok\","
		"\"Features\":[\"resume\"],\"Frame\":1,\"Title\":\"Home\"}}";

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
