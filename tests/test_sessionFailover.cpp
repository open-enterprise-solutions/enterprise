// A thin client's session outlives a dropped connection (docs/public/client-protocol.md).
//
// Three cases against a file base, through the same door a socket uses (ibClientHost::Call of the JSON-RPC text),
// and one against the communicator alone: its ids stay monotonic when the socket drops and it logs in by token.

#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <string>
#include <thread>

#include "backend/compiler/cache/byteCodeCache.h"
#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/metaObjectMetadata.h"

#include <wx/filename.h>
#include <wx/image.h>    // wxInitAllImageHandlers — a new base loads icons, and wx decodes nothing until registered
#include <wx/init.h>
#include <wx/log.h>      // wxLogStderr — a warning must not become a modal box and hang a headless run

#include "backend/appData.h"
#include "backend/appHost.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/rpc/rpcMessage.h"     // ibRpcParseRequest — a call as the server's port reads it
#ifdef OES_USE_FIREBIRD
#include "backend/databaseLayer/firebird/firebirdDatabaseLayer.h"
#endif
#include "core/crypto/sessionToken.h"
#include "core/exception.h"

#include "frmserver/client/clientHost.h"

#include "protocol/communicator.h"
#include "protocol/connection.h"
#include "protocol/protocolNode.h"

namespace {

// Left out, Resume is 120, which is too long for a test. 0 is off. The host reads backend.conf once, before any base
// is opened, and the working directory's wins over the one beside the program, where the build puts its template.
// So the process works in a folder of this test's own with that one line in it; both go when the process does.
struct ResumeFile {
	std::filesystem::path previous;
	std::filesystem::path folder;
	ResumeFile()
	{
		std::error_code error;
		previous = std::filesystem::current_path(error);
		folder = std::filesystem::temp_directory_path(error) /
			("oes-failover-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		std::filesystem::create_directories(folder, error);
		std::ofstream(folder / "backend.conf") << "Resume=2\n";
		std::filesystem::current_path(folder, error);
	}
	~ResumeFile()
	{
		std::error_code error;
		std::filesystem::current_path(previous, error);
		std::filesystem::remove_all(folder, error);
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
	if (!ibProtocolNode::Read(text, root)) {
		rpc.message = text;
		return rpc;
	}
	const ibProtocolNode failed = root.FindChild(ibProtocol::ibRpc::Error);
	if (failed.IsNode()) {
		rpc.code = failed.GetInt(ibProtocol::ibRpc::Code);
		rpc.message = failed.GetString(ibProtocol::ibRpc::Message);
		return rpc;
	}
	rpc.result = root.FindChild(ibProtocol::ibRpc::Result);
	rpc.ok = rpc.result.IsNode();
	if (!rpc.ok)
		rpc.message = text;
	return rpc;
}

wxString Request(long long id, const char* method, const ibProtocolNode& params)
{
	ibProtocolNode request;
	request.SetValue(ibProtocol::ibRpc::Version, "2.0")
		.SetValue(ibProtocol::ibRpc::Id, id)
		.SetValue(ibProtocol::ibRpc::Method, method)
		.SetValue(ibProtocol::ibRpc::Params, params);
	return request.Write();
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
	params.SetValue(ibProtocol::ibLogin::User, wxString())
		.SetValue(ibProtocol::ibLogin::Password, wxString())
		.SetValue(ibProtocol::ibLogin::Protocol, ibProtocol::Version);
	if (resume)
		params.AddItem(ibProtocol::ibLogin::Features, ibProtocol::ibLogin::FeatureResume);
	return params;
}

bool ListsResume(const ibProtocolNode& result)
{
	for (const ibProtocolNode& feature : result.GetList(ibProtocol::ibLogin::Features)) {
		if (feature.AsString() == ibProtocol::ibLogin::FeatureResume)
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
	const wxString client = login.result.GetString(ibProtocol::ibLogin::Client);
	const wxString token = login.result.GetString(ibProtocol::ibLogin::Token);
	const long long frame = login.result.GetInt(ibProtocol::ibFrame::Frame);
	ASSERT_FALSE(client.IsEmpty());
	ASSERT_FALSE(token.IsEmpty());
	ASSERT_NE(frame, 0);
	ASSERT_TRUE(login.result.Has(ibProtocol::ibFrame::Frame));

	const wxString hash = TokenHashOf(token);
	ASSERT_FALSE(hash.IsEmpty());
	EXPECT_NE(hash, token);
	EXPECT_EQ(StoredHash(client), hash);

	g_base->host->Disconnect(&connection);

	ibProtocolNode frameParams;
	frameParams.SetValue(ibProtocol::ibLogin::Client, client);
	const Rpc refused = g_base->Call(Request(2, "frame", frameParams), &connection);
	EXPECT_FALSE(refused.ok);
	EXPECT_EQ(refused.code, 401);

	int returned = 0;
	ibProtocolNode tokenParams;
	tokenParams.SetValue(ibProtocol::ibLogin::Token, token).SetValue(ibProtocol::ibLogin::Protocol, ibProtocol::Version);
	const Rpc again = g_base->Call(Request(3, "login", tokenParams), &returned);
	ASSERT_TRUE(again.ok) << again.message;
	EXPECT_EQ(again.result.GetString(ibProtocol::ibLogin::Client), client);
	EXPECT_FALSE(again.result.Has(ibProtocol::ibFrame::Frame));
	EXPECT_FALSE(again.result.Has(ibProtocol::ibFrame::Patch));
	EXPECT_TRUE(again.result.GetString(ibProtocol::ibLogin::Token).IsEmpty());

	ibProtocolNode since;
	since.SetValue(ibProtocol::ibLogin::Client, client).SetValue(ibProtocol::ibLogin::Since, frame);
	const Rpc patched = g_base->Call(Request(4, "frame", since), &returned);
	ASSERT_TRUE(patched.ok) << patched.message;
	EXPECT_TRUE(patched.result.FindChild(ibProtocol::ibFrame::Patch).IsNode());
	EXPECT_EQ(patched.result.GetInt(ibProtocol::ibLogin::Since), frame);
	EXPECT_FALSE(patched.result.Has(ibProtocol::ibFrame::Title));
	EXPECT_EQ(patched.result.GetInt(ibProtocol::ibFrame::Frame), frame + 1);
}

TEST_F(SessionFailover, LostCallIsNotExecutedTwice)
{
	int connection = 0;
	const Rpc login = g_base->Call(Request(10, "login", PasswordLogin()), &connection);
	ASSERT_TRUE(login.ok) << login.message;
	const wxString client = login.result.GetString(ibProtocol::ibLogin::Client);
	const long long frame = login.result.GetInt(ibProtocol::ibFrame::Frame);

	ibProtocolNode params;
	params.SetValue(ibProtocol::ibLogin::Client, client).SetValue(ibProtocol::ibLogin::Since, frame);
	const wxString firstText = g_base->host->Call(Request(11, "frame", params), &connection, wxT("test"));
	const Rpc first = ReadRpc(firstText);
	ASSERT_TRUE(first.ok) << first.message;
	const long long drawn = first.result.GetInt(ibProtocol::ibFrame::Frame);

	const wxString againText = g_base->host->Call(Request(11, "frame", params), &connection, wxT("test"));
	EXPECT_EQ(againText, firstText);

	const Rpc third = g_base->Call(Request(12, "frame", params), &connection);
	ASSERT_TRUE(third.ok) << third.message;
	// The repeated id did not draw a frame. A new id draws the next one, not the one after that.
	EXPECT_EQ(third.result.GetInt(ibProtocol::ibFrame::Frame), drawn + 1);
}

TEST_F(SessionFailover, ExpiredWindowClosesTheSession)
{
	int connection = 0;
	const Rpc login = g_base->Call(Request(20, "login", PasswordLogin()), &connection);
	ASSERT_TRUE(login.ok) << login.message;
	const wxString client = login.result.GetString(ibProtocol::ibLogin::Client);
	const wxString token = login.result.GetString(ibProtocol::ibLogin::Token);
	ASSERT_TRUE(SessionRowExists(client));

	g_base->host->Disconnect(&connection);
	const std::size_t seconds = ibApplicationHost::Get()->GetResumeSeconds();
	ASSERT_LE(seconds, 5u);
	std::this_thread::sleep_for(std::chrono::seconds(seconds + 2));

	int returned = 0;
	ibProtocolNode tokenParams;
	tokenParams.SetValue(ibProtocol::ibLogin::Token, token);
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
	const wxString client = login.result.GetString(ibProtocol::ibLogin::Client);
	ASSERT_TRUE(SessionRowExists(client));

	int other = 0;
	ibProtocolNode bad;
	bad.SetValue(ibProtocol::ibLogin::Token, wxString(wxT("not-a-token")));
	const Rpc malformed = g_base->Call(Request(31, "login", bad), &other);
	EXPECT_FALSE(malformed.ok);
	EXPECT_EQ(malformed.code, 401);

	ibProtocolNode unknown;
	unknown.SetValue(ibProtocol::ibLogin::Token, wxString(wxT("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef")));
	const Rpc refused = g_base->Call(Request(32, "login", unknown), &other);
	EXPECT_FALSE(refused.ok);
	EXPECT_EQ(refused.code, 401);
	EXPECT_TRUE(SessionRowExists(client));

	ibProtocolNode frameParams;
	frameParams.SetValue(ibProtocol::ibLogin::Client, client);
	const Rpc still = g_base->Call(Request(33, "frame", frameParams), &connection);
	EXPECT_TRUE(still.ok) << still.message;
}

TEST_F(SessionFailover, LiveSessionIsNotTakenByToken)
{
	int connection = 0;
	const Rpc login = g_base->Call(Request(40, "login", PasswordLogin()), &connection);
	ASSERT_TRUE(login.ok) << login.message;
	const wxString client = login.result.GetString(ibProtocol::ibLogin::Client);
	const wxString token = login.result.GetString(ibProtocol::ibLogin::Token);
	ASSERT_FALSE(token.IsEmpty());

	int intruder = 0;
	ibProtocolNode tokenParams;
	tokenParams.SetValue(ibProtocol::ibLogin::Token, token);
	const Rpc stolen = g_base->Call(Request(41, "login", tokenParams), &intruder);
	EXPECT_FALSE(stolen.ok);
	EXPECT_EQ(stolen.code, 401);

	ibProtocolNode frameParams;
	frameParams.SetValue(ibProtocol::ibLogin::Client, client);
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
	params.SetValue(ibProtocol::ibLogin::Mode, static_cast<int>(ibProtocol::ibLogin::ibMode::Designer));
	const Rpc login = g_base->Call(Request(55, "login", params), &connection);
	ASSERT_TRUE(login.ok) << login.message;
	EXPECT_TRUE(login.result.GetString(ibProtocol::ibLogin::Token).IsEmpty());
	EXPECT_FALSE(ListsResume(login.result));
	const wxString client = login.result.GetString(ibProtocol::ibLogin::Client);
	g_base->host->Disconnect(&connection);
	EXPECT_FALSE(SessionRowExists(client));
}

TEST_F(SessionFailover, NoTokenWithoutASocket)
{
	const Rpc login = g_base->Call(Request(50, "login", PasswordLogin()), nullptr);
	ASSERT_TRUE(login.ok) << login.message;
	EXPECT_TRUE(login.result.GetString(ibProtocol::ibLogin::Token).IsEmpty());
	EXPECT_FALSE(ListsResume(login.result));
	EXPECT_TRUE(login.result.Has(ibProtocol::ibFrame::Frame));
}

TEST_F(SessionFailover, RepeatedIdWithoutOptInRunsAgain)
{
	int connection = 0;
	const Rpc login = g_base->Call(Request(60, "login", PasswordLogin(false)), &connection);
	ASSERT_TRUE(login.ok) << login.message;
	EXPECT_FALSE(login.result.GetString(ibProtocol::ibLogin::Token).IsEmpty());
	const wxString client = login.result.GetString(ibProtocol::ibLogin::Client);
	const long long frame = login.result.GetInt(ibProtocol::ibFrame::Frame);

	ibProtocolNode params;
	params.SetValue(ibProtocol::ibLogin::Client, client).SetValue(ibProtocol::ibLogin::Since, frame);
	const Rpc first = g_base->Call(Request(61, "frame", params), &connection);
	ASSERT_TRUE(first.ok) << first.message;
	const Rpc again = g_base->Call(Request(61, "frame", params), &connection);
	ASSERT_TRUE(again.ok) << again.message;
	EXPECT_EQ(again.result.GetInt(ibProtocol::ibFrame::Frame), first.result.GetInt(ibProtocol::ibFrame::Frame) + 1);
}

TEST_F(SessionFailover, OlderIdAndADifferentCallAreRefused)
{
	int connection = 0;
	const Rpc login = g_base->Call(Request(70, "login", PasswordLogin()), &connection);
	ASSERT_TRUE(login.ok) << login.message;
	const wxString client = login.result.GetString(ibProtocol::ibLogin::Client);
	const long long frame = login.result.GetInt(ibProtocol::ibFrame::Frame);

	ibProtocolNode params;
	params.SetValue(ibProtocol::ibLogin::Client, client).SetValue(ibProtocol::ibLogin::Since, frame);
	const Rpc first = g_base->Call(Request(71, "frame", params), &connection);
	ASSERT_TRUE(first.ok) << first.message;
	const long long drawn = first.result.GetInt(ibProtocol::ibFrame::Frame);

	ibProtocolNode next = params;
	const Rpc second = g_base->Call(Request(72, "frame", next), &connection);
	ASSERT_TRUE(second.ok) << second.message;

	const Rpc replay = g_base->Call(Request(71, "frame", params), &connection);
	EXPECT_FALSE(replay.ok);
	EXPECT_EQ(replay.code, 400);

	ibProtocolNode other = params;
	other.SetValue(ibProtocol::ibLogin::Since, frame + 50);
	const Rpc mismatch = g_base->Call(Request(72, "frame", other), &connection);
	EXPECT_FALSE(mismatch.ok);
	EXPECT_EQ(mismatch.code, 400);

	const Rpc onward = g_base->Call(Request(73, "frame", params), &connection);
	ASSERT_TRUE(onward.ok) << onward.message;
	EXPECT_EQ(onward.result.GetInt(ibProtocol::ibFrame::Frame), drawn + 2);
}

TEST_F(SessionFailover, DuplicateIdRunsOnce)
{
	int connection = 0;
	const Rpc login = g_base->Call(Request(80, "login", PasswordLogin()), &connection);
	ASSERT_TRUE(login.ok) << login.message;
	const wxString client = login.result.GetString(ibProtocol::ibLogin::Client);
	const long long frame = login.result.GetInt(ibProtocol::ibFrame::Frame);

	ibProtocolNode params;
	params.SetValue(ibProtocol::ibLogin::Client, client).SetValue(ibProtocol::ibLogin::Since, frame);
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
	EXPECT_EQ(a.result.GetInt(ibProtocol::ibFrame::Frame), frame + 1);

	const Rpc third = g_base->Call(Request(81, "frame", params), &connection);
	ASSERT_TRUE(third.ok) << third.message;
	EXPECT_EQ(ReadRpc(g_base->host->Call(text, &connection, wxT("test"))).result.GetInt(ibProtocol::ibFrame::Frame), frame + 1);

	const Rpc next = g_base->Call(Request(82, "frame", params), &connection);
	ASSERT_TRUE(next.ok) << next.message;
	EXPECT_EQ(next.result.GetInt(ibProtocol::ibFrame::Frame), frame + 2);
}

TEST_F(SessionFailover, AnotherConnectionDoesNotReadTheCachedAnswer)
{
	int connection = 0;
	const Rpc login = g_base->Call(Request(90, "login", PasswordLogin()), &connection);
	ASSERT_TRUE(login.ok) << login.message;
	const wxString client = login.result.GetString(ibProtocol::ibLogin::Client);

	ibProtocolNode params;
	params.SetValue(ibProtocol::ibLogin::Client, client);
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
	EXPECT_EQ(cached.result.GetInt(ibProtocol::ibFrame::Frame), first.result.GetInt(ibProtocol::ibFrame::Frame));
}

TEST_F(SessionFailover, StartModuleThatAsksDoesNotHangTheLogin)
{
	// The configuration is up as the base opens, before anybody logs in.
	ibMetaDataConfigurationBase* const meta = ibApplicationInstance::GetActiveMetaData(g_base->app);
	ASSERT_NE(meta, nullptr);
	ibValueMetaObjectConfiguration* const common = meta->GetCommonMetaObject();
	ASSERT_NE(common, nullptr);
	ibValueMetaObjectModule* const module = const_cast<ibValueMetaObjectModule*>(common->GetObjectModule());
	ASSERT_NE(module, nullptr);
	// The cache key is the configuration digest, not the module text. A login that already compiled
	// the empty module would serve that bytecode again, and onStart would never ask. Drop the row
	// whenever the text changes, the same way an apply retires it.
	struct Restore {
		ibValueMetaObjectModule* module;
		wxString                 text;
		~Restore()
		{
			module->SetModuleText(text);
			ibByteCodeCache::Invalidate(module->GetGuid().GetGuid());
		}
	} restore{ module, module->GetModuleText() };
	const bool words = common->GetCompileSyntax() == ibProgramSyntax::syntax_ves;
	module->SetModuleText(words
		? wxT("Procedure onStart()\n")
		  wxT("\tQuestion(\"stay\", QuestionMode.Ok);\n")
		  wxT("EndProcedure\n")
		: wxT("Procedure onStart() {\n")
		  wxT("\tQuestion(\"stay\", QuestionMode.Ok);\n")
		  wxT("}\n"));
	ibByteCodeCache::Invalidate(module->GetGuid().GetGuid());

	int connection = 0;
	auto pending = std::async(std::launch::async, [&]() {
		return g_base->Call(Request(120, "login", PasswordLogin()), &connection);
	});
	ASSERT_EQ(pending.wait_for(std::chrono::seconds(10)), std::future_status::ready)
		<< "login hung behind a start module that asks";
	const Rpc login = pending.get();
	ASSERT_TRUE(login.ok) << login.message;
	EXPECT_FALSE(login.result.GetString(ibProtocol::ibLogin::Token).IsEmpty());
	const ibProtocolNode request = login.result.FindChild("Request");
	ASSERT_TRUE(request.IsNode()) << "the start module's question was not on the login answer: "
		<< login.result.Write().ToUtf8();
	EXPECT_EQ(request.GetString("Text"), wxT("stay"));

	const wxString client = login.result.GetString(ibProtocol::ibLogin::Client);
	ibProtocolNode response;
	response.SetValue("Button", static_cast<long long>(wxOK));
	ibProtocolNode answer;
	answer.SetValue(ibProtocol::ibLogin::Client, client);
	answer.SetValue("Id", request.GetString("Id"));
	answer.SetValue("Response", response);
	const Rpc responded = g_base->Call(Request(121, "respond", answer), &connection);
	EXPECT_TRUE(responded.ok) << responded.message;
}

TEST_F(SessionFailover, DetachedSessionSaysUntilWhen)
{
	int watcher = 0;
	const Rpc admin = g_base->Call(Request(130, "login", PasswordLogin(false)), &watcher);
	ASSERT_TRUE(admin.ok) << admin.message;
	const wxString adminId = admin.result.GetString(ibProtocol::ibLogin::Client);

	int connection = 0;
	const Rpc login = g_base->Call(Request(131, "login", PasswordLogin()), &connection);
	ASSERT_TRUE(login.ok) << login.message;
	const wxString client = login.result.GetString(ibProtocol::ibLogin::Client);
	g_base->host->Disconnect(&connection);

	ibProtocolNode params;
	params.SetValue(ibProtocol::ibLogin::Client, adminId);
	params.SetValue("Schema", static_cast<int>(ibProtocol::ibSchema::ibKind::ActiveUser));

	bool found = false;
	wxString last;
	for (int attempt = 0; attempt < 15 && !found; ++attempt) {
		std::this_thread::sleep_for(std::chrono::milliseconds(200));
		const wxString text = g_base->host->Call(Request(140 + attempt, "schema", params), &watcher, wxT("test"));
		last = text;
		const Rpc list = ReadRpc(text);
		if (!list.ok)
			continue;
		for (const ibProtocolNode& row : list.result.FindChild("Sessions").Children()) {
			if (row.GetString("Session") == client
				&& row.GetString("Activity").Find(wxT("detached until")) != wxNOT_FOUND)
				found = true;
		}
	}
	EXPECT_TRUE(found) << last;
}

namespace {

// A connection that fails one exchange, then accepts the login and the resent call.
class ScriptedConnection : public ibProtocolConnection {
public:
	std::vector<ibString> sent;
	std::vector<bool>     fail;
	std::vector<ibString> answers;
	int                   reconnects = 0;

	bool Exchange(const ibString& request, ibString& answer, ibProtocol::ibRefusal& refusal, wxString& error) override
	{
		sent.push_back(request);
		if (fail.empty()) {
			refusal = ibProtocol::ibRefusal::Failed;
			error = wxT("no script");
			return false;
		}
		const bool drop = fail.front();
		fail.erase(fail.begin());
		if (drop) {
			refusal = ibProtocol::ibRefusal::NoSession;
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
	void Listen(std::function<void(const ibString&)>) override {}
};

ibString Answer(long long id, const ibProtocolNode& result)
{
	ibProtocolNode root;
	root.SetValue(ibProtocol::ibRpc::Version, "2.0")
		.SetValue(ibProtocol::ibRpc::Id, id)
		.SetValue(ibProtocol::ibRpc::Result, result);
	return root.Write();
}

long long IdOf(const ibString& text)
{
	ibProtocolNode node;
	EXPECT_TRUE(ibProtocolNode::Read(text, node));
	return node.GetInt(ibProtocol::ibRpc::Id);
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
			resumed.SetValue(ibProtocol::ibLogin::Client, wxString(wxT("client-1")))
				.SetValue(ibProtocol::ibLogin::Protocol, 2);
			return resumed;
		}()),
		Answer(2, [&]() {
			ibProtocolNode frame;
			frame.SetValue(ibProtocol::ibFrame::Frame, 2).SetValue(ibProtocol::ibFrame::Title, wxString(wxT("Home")));
			return frame;
		}()),
		Answer(4, [&]() {
			ibProtocolNode frame;
			frame.SetValue(ibProtocol::ibFrame::Frame, 3).SetValue(ibProtocol::ibFrame::Title, wxString(wxT("Home")));
			return frame;
		}()),
	};

	ibCommunicator communicator(std::move(script));
	ibProtocolNode result;
	ibProtocol::ibRefusal refusal = ibProtocol::ibRefusal::None;
	wxString error;

	ASSERT_TRUE(communicator.Login(wxT("user"), wxT("secret"), ibProtocol::ibLogin::ibMode::Runtime, result, refusal, error)) << error;

	ibProtocolNode params;
	ASSERT_TRUE(communicator.Call(ibProtocol::ibMethod::Frame, params, result, refusal, error)) << error;
	EXPECT_EQ(raw->reconnects, 1);
	ASSERT_GE(raw->sent.size(), 4u);
	// 0 password login, 1 the frame that dropped, 2 login {Token}, 3 the same frame again.
	EXPECT_EQ(IdOf(raw->sent[1]), 2);
	EXPECT_EQ(IdOf(raw->sent[2]), 3);
	EXPECT_EQ(IdOf(raw->sent[3]), 2);
	EXPECT_TRUE(raw->sent[2].Contains(wxT("\"Token\"")));

	ASSERT_TRUE(communicator.Call(ibProtocol::ibMethod::Frame, params, result, refusal, error)) << error;
	EXPECT_EQ(IdOf(raw->sent.back()), 4);
}

namespace {

// A state event as the client's host makes one — {Control, Event, Args}.
ibProtocolNode StateEvent(int control, ibProtocol::ibEvent::ibKind event, const char* name = nullptr, int value = 0, int column = 0)
{
	ibProtocolNode args;
	if (name != nullptr)
		args.SetValue(name, value);
	if (column != 0)
		args.SetValue(ibProtocol::ibEvent::Column, column);
	ibProtocolNode node;
	node.SetValue(ibProtocol::ibEvent::Control, control)
		.SetValue(ibProtocol::ibEvent::Event, static_cast<int>(event))
		.SetValue(ibProtocol::ibEvent::Args, args);
	return node;
}

std::vector<ibProtocolNode> PendingOf(const ibString& text)
{
	ibProtocolNode node;
	EXPECT_TRUE(ibProtocolNode::Read(text, node));
	return node.FindChild(ibProtocol::ibRpc::Params).GetList(ibProtocol::ibLogin::Pending);
}

ibString LoginAnswer(int protocol)
{
	return ibString::Format(
		wxT("{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"Client\":\"client-1\",\"Protocol\":%d,\"Frame\":1,\"Title\":\"Home\"}}"),
		protocol);
}

ibString FrameAnswer(long long id, int number)
{
	ibProtocolNode frame;
	frame.SetValue(ibProtocol::ibFrame::Frame, number).SetValue(ibProtocol::ibFrame::Title, wxString(wxT("Home")));
	return Answer(id, frame);
}

} // namespace

// A walk down a list is one record: the latest of each state event per control, in the order they were last done, goes
// with the next call — and only with it.
TEST(SubscribedEventsClient, HeldEventsGoWithTheNextCall)
{
	auto script = std::make_unique<ScriptedConnection>();
	ScriptedConnection* const raw = script.get();
	raw->fail = { false, false, false };
	raw->answers = { LoginAnswer(3), FrameAnswer(2, 2), FrameAnswer(3, 3) };

	ibCommunicator communicator(std::move(script));
	ibProtocolNode result;
	ibProtocol::ibRefusal refusal = ibProtocol::ibRefusal::None;
	wxString error;
	ASSERT_TRUE(communicator.Login(wxT("user"), wxT("secret"), ibProtocol::ibLogin::ibMode::Runtime, result, refusal, error)) << error;

	for (int row = 1; row <= 100; ++row)
		EXPECT_TRUE(communicator.Hold(StateEvent(7, ibProtocol::ibEvent::ibKind::Row, ibProtocol::ibEvent::Row, row)));
	for (int focus = 0; focus < 5; ++focus)
		EXPECT_TRUE(communicator.Hold(StateEvent(3, ibProtocol::ibEvent::ibKind::Focus)));
	EXPECT_TRUE(communicator.Hold(StateEvent(7, ibProtocol::ibEvent::ibKind::Resize, "Width", 80, 21)));
	EXPECT_TRUE(communicator.Hold(StateEvent(7, ibProtocol::ibEvent::ibKind::Resize, "Width", 90, 22)));
	EXPECT_TRUE(communicator.Hold(StateEvent(7, ibProtocol::ibEvent::ibKind::Resize, "Width", 120, 21)));
	EXPECT_TRUE(communicator.Hold(StateEvent(7, ibProtocol::ibEvent::ibKind::Row, ibProtocol::ibEvent::Row, 500)));

	ibProtocolNode params;
	ASSERT_TRUE(communicator.Call(ibProtocol::ibMethod::Frame, params, result, refusal, error)) << error;
	ASSERT_EQ(raw->sent.size(), 2u);
	const std::vector<ibProtocolNode> pending = PendingOf(raw->sent[1]);
	ASSERT_EQ(pending.size(), 4u);
	EXPECT_EQ(pending[0].GetInt(ibProtocol::ibEvent::Event), static_cast<int>(ibProtocol::ibEvent::ibKind::Focus));
	EXPECT_EQ(pending[0].GetInt(ibProtocol::ibEvent::Control), 3);
	// Each column's width is its own: 22's stays, 21's is the later one.
	EXPECT_EQ(pending[1].FindChild(ibProtocol::ibEvent::Args).GetInt(ibProtocol::ibEvent::Column), 22);
	EXPECT_EQ(pending[2].FindChild(ibProtocol::ibEvent::Args).GetInt(ibProtocol::ibEvent::Column), 21);
	EXPECT_EQ(pending[2].FindChild(ibProtocol::ibEvent::Args).GetInt("Width"), 120);
	EXPECT_EQ(pending[3].GetInt(ibProtocol::ibEvent::Event), static_cast<int>(ibProtocol::ibEvent::ibKind::Row));
	EXPECT_EQ(pending[3].FindChild(ibProtocol::ibEvent::Args).GetInt(ibProtocol::ibEvent::Row), 500);

	// …and the server reads it as the client wrote it: a list of events, each with its Args.
	ibRpcRequest request;
	wxString parseError;
	ASSERT_TRUE(ibRpcParseRequest(raw->sent[1], request, parseError)) << parseError;
	const ibDataValue* const read = request.m_params.FindField(ibProtocol::ibLogin::Pending);
	ASSERT_NE(read, nullptr);
	ASSERT_EQ(read->Kind(), ibDataKind::Array);
	ASSERT_EQ(read->AsArray().size(), 4u);
	const ibDataValue& last = read->AsArray()[3];
	ASSERT_EQ(last.Kind(), ibDataKind::Child);
	EXPECT_EQ(last.AsChild()->GetValue<std::int32_t>(ibProtocol::ibEvent::Event),
		static_cast<std::int32_t>(ibProtocol::ibEvent::ibKind::Row));
	EXPECT_EQ(last.AsChild()->GetValue<std::int32_t>(ibProtocol::ibEvent::Control), 7);
	const ibDataNode* const args = last.AsChild()->FindChild(ibProtocol::ibEvent::Args);
	ASSERT_NE(args, nullptr);
	EXPECT_EQ(args->GetValue<std::int32_t>(ibProtocol::ibEvent::Row), 500);

	// Taken once.
	ASSERT_TRUE(communicator.Call(ibProtocol::ibMethod::Frame, params, result, refusal, error)) << error;
	EXPECT_TRUE(PendingOf(raw->sent.back()).empty());
}

// A server of protocol 2 would never hear a held event: the client sends it at once.
TEST(SubscribedEventsClient, NothingHeldBelowProtocol3)
{
	auto script = std::make_unique<ScriptedConnection>();
	ScriptedConnection* const raw = script.get();
	raw->fail = { false, false };
	raw->answers = { LoginAnswer(2), FrameAnswer(2, 2) };

	ibCommunicator communicator(std::move(script));
	ibProtocolNode result;
	ibProtocol::ibRefusal refusal = ibProtocol::ibRefusal::None;
	wxString error;
	EXPECT_FALSE(communicator.Hold(StateEvent(7, ibProtocol::ibEvent::ibKind::Row, ibProtocol::ibEvent::Row, 1)));
	ASSERT_TRUE(communicator.Login(wxT("user"), wxT("secret"), ibProtocol::ibLogin::ibMode::Runtime, result, refusal, error)) << error;
	EXPECT_FALSE(communicator.Hold(StateEvent(7, ibProtocol::ibEvent::ibKind::Row, ibProtocol::ibEvent::Row, 1)));

	ibProtocolNode params;
	ASSERT_TRUE(communicator.Call(ibProtocol::ibMethod::Frame, params, result, refusal, error)) << error;
	EXPECT_TRUE(PendingOf(raw->sent.back()).empty());
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
