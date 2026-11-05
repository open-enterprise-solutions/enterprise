// A thin client's session outlives a dropped connection (docs/public/client-protocol.md).
//
// Cases against a file base, through the door a POST uses (ibClientHost::Call of the JSON-RPC text — a socket's packet
// goes to the same call), and the held events of a call as the server reads them. The communicator's own side — its ids
// across a dropped socket, its channels, what it holds for the next call — is tests/test_communicator.cpp.

#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <thread>

#include "backend/compiler/cache/byteCodeCache.h"
#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/metaObjectMetadata.h"

#include <wx/filename.h>
#include <wx/init.h>
#include <wx/log.h>      // wxLogStderr — a warning must not become a modal box and hang a headless run

#include "backend/appData.h"
#include "backend/appHost.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "core/settings/settingsShelf.h"      // ibSettingsCategory - the style's row, refused by the base
#include "backend/rpc/rpcMessage.h"     // ibRpcParseRequest — a call as the server's port reads it
#include "backend/utils/passwordHash.hpp"
#ifdef OES_USE_FIREBIRD
#include "backend/databaseLayer/firebird/firebirdDatabaseLayer.h"
#endif
#include "core/crypto/sessionToken.h"
#include "core/exception.h"

#include "frmserver/client/clientHost.h"

#include "protocol/protocolNode.h"
#include "protocol/protocolDesigner.h"   // ibProtocol::ibSchema::ibTypes

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
	ibString       message;
};

Rpc ReadRpc(const ibString& text)
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

ibString Request(long long id, const char* method, const ibProtocolNode& params)
{
	ibProtocolNode request;
	request.SetValue(ibProtocol::ibRpc::Version, "2.0")
		.SetValue(ibProtocol::ibRpc::Id, id)
		.SetValue(ibProtocol::ibRpc::Method, method)
		.SetValue(ibProtocol::ibRpc::Params, params);
	return request.Write();
}

ibString TokenHashOf(const ibString& token)
{
	unsigned char raw[ibSessionToken::kBytes];
	if (!ibSessionToken::ParseHex(token, raw))
		return ibString();
	return ibSessionToken::HashHex(raw, ibSessionToken::kBytes);
}

ibString StoredHash(const ibString& sessionId)
{
	ibConnectionScope scope = ibConnectionPool::GetFreeConnection();
	if (!scope)
		return ibString();
	ibStatementGuard statement(scope.shared(), scope->PrepareStatement(wxT("SELECT tokenHash FROM sys_session WHERE session = ?")));
	if (!statement)
		return ibString();
	statement->SetParamString(1, sessionId);
	ibResultSetGuard rows(scope.shared(), statement->RunQueryWithResults());
	if (!rows.get() || !rows->Next())
		return ibString();
	return rows->GetResultString(1);
}

bool SessionRowExists(const ibString& sessionId)
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
	ibString                               dir;
	ibString                               reason;
	bool                                   failed = false;   // the client is here and the base still did not open
	ibApplicationInstance*                 app = nullptr;
	std::unique_ptr<ibClientHost>          host;

	void Open()
	{
		if (!wx.IsOk()) {
			reason = wxT("wxBase init failed");
			return;
		}
		// As a headless runtime test: no modal log.
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

	Rpc Call(const ibString& text, const void* connection)
	{
		return ReadRpc(host->Call(text, connection, wxT("test")));
	}
};

Base* g_base = nullptr;

ibProtocolNode PasswordLogin(bool resume = true)
{
	ibProtocolNode params;
	params.SetValue(ibProtocol::ibLogin::User, ibString())
		.SetValue(ibProtocol::ibLogin::Password, ibString())
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
	const ibString client = login.result.GetString(ibProtocol::ibLogin::Client);
	const ibString token = login.result.GetString(ibProtocol::ibLogin::Token);
	const long long frame = login.result.GetInt(ibProtocol::ibFrame::Frame);
	ASSERT_FALSE(client.IsEmpty());
	ASSERT_FALSE(token.IsEmpty());
	ASSERT_NE(frame, 0);
	ASSERT_TRUE(login.result.Has(ibProtocol::ibFrame::Frame));

	const ibString hash = TokenHashOf(token);
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

TEST_F(SessionFailover, ABaseWithNoUsersGivesTheAssistantItsChannel)
{
	int connection = 0;
	const Rpc login = g_base->Call(Request(1, "login", PasswordLogin()), &connection);
	ASSERT_TRUE(login.ok) << login.message;

	// Open access: nobody to keep out, so the assistant's right, closed by default, is open here.
	ibProtocolNode params;
	params.SetValue(ibProtocol::ibLogin::Client, login.result.GetString(ibProtocol::ibLogin::Client))
		.SetValue(ibProtocol::ibMcp::Command, static_cast<int>(ibProtocol::ibMcp::ibCommand::Channel));
	const Rpc channel = g_base->Call(Request(2, "mcp", params), &connection);
	EXPECT_TRUE(channel.ok) << channel.message;

	g_base->host->Disconnect(&connection);
}

TEST_F(SessionFailover, APersonWhoseRolesDoNotGiveTheAssistantHasNoChannel)
{
	const ibString password = wxT("right-secret");

	// A user with no role, gone again with the test: the other cases log into a base with no users.
	struct OneUser {
		ibGuid guid = wxNewUniqueGuid;
		~OneUser()
		{
			ibApplicationInstanceScope working(g_base->app);
			ibUserInfo::Delete(guid);
		}
	} user;
	{
		ibApplicationInstanceScope working(g_base->app);
		ibUserInfo info;
		info.m_strUserGuid = user.guid;
		info.m_strUserName = wxT("clerk");
		info.m_strUserFullName = wxT("Clerk");
		info.m_strUserPassword = ibPasswordHash::Hash(password);
		ASSERT_TRUE(ibUserInfo::Save(info));
	}

	int connection = 0;
	ibProtocolNode credentials = PasswordLogin();
	credentials.SetValue(ibProtocol::ibLogin::User, ibString(wxT("clerk")))
		.SetValue(ibProtocol::ibLogin::Password, password);
	const Rpc login = g_base->Call(Request(1, "login", credentials), &connection);
	ASSERT_TRUE(login.ok) << login.message;

	ibProtocolNode params;
	params.SetValue(ibProtocol::ibLogin::Client, login.result.GetString(ibProtocol::ibLogin::Client))
		.SetValue(ibProtocol::ibMcp::Command, static_cast<int>(ibProtocol::ibMcp::ibCommand::Channel));
	const Rpc refused = g_base->Call(Request(2, "mcp", params), &connection);
	EXPECT_FALSE(refused.ok);
	EXPECT_EQ(refused.code, static_cast<long long>(ibProtocol::ibRefusal::NoRight));

	g_base->host->Disconnect(&connection);
}

TEST_F(SessionFailover, LostCallIsNotExecutedTwice)
{
	int connection = 0;
	const Rpc login = g_base->Call(Request(10, "login", PasswordLogin()), &connection);
	ASSERT_TRUE(login.ok) << login.message;
	const ibString client = login.result.GetString(ibProtocol::ibLogin::Client);
	const long long frame = login.result.GetInt(ibProtocol::ibFrame::Frame);

	ibProtocolNode params;
	params.SetValue(ibProtocol::ibLogin::Client, client).SetValue(ibProtocol::ibLogin::Since, frame);
	const ibString firstText = g_base->host->Call(Request(11, "frame", params), &connection, wxT("test"));
	const Rpc first = ReadRpc(firstText);
	ASSERT_TRUE(first.ok) << first.message;
	const long long drawn = first.result.GetInt(ibProtocol::ibFrame::Frame);

	const ibString againText = g_base->host->Call(Request(11, "frame", params), &connection, wxT("test"));
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
	const ibString client = login.result.GetString(ibProtocol::ibLogin::Client);
	const ibString token = login.result.GetString(ibProtocol::ibLogin::Token);
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
	const ibString client = login.result.GetString(ibProtocol::ibLogin::Client);
	ASSERT_TRUE(SessionRowExists(client));

	int other = 0;
	ibProtocolNode bad;
	bad.SetValue(ibProtocol::ibLogin::Token, ibString(wxT("not-a-token")));
	const Rpc malformed = g_base->Call(Request(31, "login", bad), &other);
	EXPECT_FALSE(malformed.ok);
	EXPECT_EQ(malformed.code, 401);

	ibProtocolNode unknown;
	unknown.SetValue(ibProtocol::ibLogin::Token, ibString(wxT("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef")));
	const Rpc refused = g_base->Call(Request(32, "login", unknown), &other);
	EXPECT_FALSE(refused.ok);
	EXPECT_EQ(refused.code, 401);
	EXPECT_TRUE(SessionRowExists(client));

	ibProtocolNode frameParams;
	frameParams.SetValue(ibProtocol::ibLogin::Client, client);
	const Rpc still = g_base->Call(Request(33, "frame", frameParams), &connection);
	EXPECT_TRUE(still.ok) << still.message;
}

// The door of every client: a login that comes again too soon is refused as a login is, which asks the person again,
// and says how long to wait, not that the password is wrong.
TEST_F(SessionFailover, ALoginThatComesAgainTooSoonIsToldToWait)
{
	ibProtocolNode params;
	params.SetValue(ibProtocol::ibLogin::User, ibString(wxT("nobody")))
		.SetValue(ibProtocol::ibLogin::Password, ibString(wxT("wrong")))
		.SetValue(ibProtocol::ibLogin::Protocol, ibProtocol::Version);

	const Rpc wrong = g_base->Call(Request(40, "login", params), nullptr);
	EXPECT_FALSE(wrong.ok);
	EXPECT_EQ(wrong.code, 401);

	const Rpc again = g_base->Call(Request(41, "login", params), nullptr);
	EXPECT_FALSE(again.ok);
	EXPECT_EQ(again.code, 401);
	EXPECT_TRUE(again.message.Contains(wxT("try again"))) << again.message;
	EXPECT_NE(again.message, wrong.message);
}

TEST_F(SessionFailover, LiveSessionIsNotTakenByToken)
{
	int connection = 0;
	const Rpc login = g_base->Call(Request(40, "login", PasswordLogin()), &connection);
	ASSERT_TRUE(login.ok) << login.message;
	const ibString client = login.result.GetString(ibProtocol::ibLogin::Client);
	const ibString token = login.result.GetString(ibProtocol::ibLogin::Token);
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
	const ibString client = login.result.GetString(ibProtocol::ibLogin::Client);
	g_base->host->Disconnect(&connection);
	EXPECT_FALSE(SessionRowExists(client));
}

// The designer's syntax helper asks the server for the names its articles do not write up, and what one offers.
TEST_F(SessionFailover, TheSyntaxHelperAsksForATypeAndWhatItOffers)
{
	int connection = 0;
	ibProtocolNode login = PasswordLogin();
	login.SetValue(ibProtocol::ibLogin::Mode, static_cast<int>(ibProtocol::ibLogin::ibMode::Designer));
	const Rpc opened = g_base->Call(Request(60, "login", login), &connection);
	ASSERT_TRUE(opened.ok) << opened.message;
	const ibString client = opened.result.GetString(ibProtocol::ibLogin::Client);

	const auto ask = [&](long long id, const char* field) {
		ibProtocolNode args;
		args.SetValue(field, ibString(wxT("Array")));
		ibProtocolNode params;
		params.SetValue(ibProtocol::ibLogin::Client, client);
		params.SetValue(ibProtocol::ibSchema::Schema, static_cast<int>(ibProtocol::ibSchema::ibKind::Types));
		params.SetValue(ibProtocol::ibSchema::Args, args);
		return g_base->Call(Request(id, "schema", params), &connection);
	};

	const Rpc listed = ask(61, ibProtocol::ibSchema::ibTypes::Query);
	ASSERT_TRUE(listed.ok) << listed.message;
	bool found = false;
	for (const ibProtocolNode& row : listed.result.FindChild(ibProtocol::ibSchema::ibTypes::Types).Children())
		found = found || row.GetString(ibProtocol::ibSchema::ibTypes::Name) == ibString(wxT("Array"));
	EXPECT_TRUE(found);

	const Rpc members = ask(62, ibProtocol::ibSchema::ibTypes::Name);
	ASSERT_TRUE(members.ok) << members.message;
	EXPECT_FALSE(members.result.FindChild(ibProtocol::ibSchema::ibTypes::Methods).Children().empty());

	g_base->host->Disconnect(&connection);
}

// THE PERSON'S THEME — kept in the base as theirs and read back: the system's until they choose; a value that is no
// theme is refused, nothing kept. (This base has no users: its one row is everybody's.)
TEST_F(SessionFailover, ThePersonsThemeIsKeptAndReadBack)
{
	int connection = 0;
	const Rpc opened = g_base->Call(Request(130, "login", PasswordLogin()), &connection);
	ASSERT_TRUE(opened.ok) << opened.message;
	const ibString client = opened.result.GetString(ibProtocol::ibLogin::Client);

	// A call of the schema; with a command, Args.Theme as the given setter writes it.
	const auto call = [&](long long id, int command, const std::function<void(ibProtocolNode&)>& theme, int& connectionOf) {
		ibProtocolNode params;
		params.SetValue(ibProtocol::ibLogin::Client, client);
		params.SetValue(ibProtocol::ibSchema::Schema, static_cast<int>(ibProtocol::ibSchema::ibKind::Style));
		if (command != 0) {
			ibProtocolNode args;
			theme(args);
			params.SetValue(ibProtocol::ibSchema::Command, command);
			params.SetValue(ibProtocol::ibSchema::Args, args);
		}
		return g_base->Call(Request(id, "schema", params), &connectionOf);
	};
	const auto style = [&](long long id, int command, int theme, int& connectionOf) {
		return call(id, command, [theme](ibProtocolNode& args) { args.SetValue(ibProtocol::ibSchema::ibStyle::Theme, theme); },
			connectionOf);
	};
	const int save = static_cast<int>(ibProtocol::ibSchema::ibCommand::Save);
	const int dark = static_cast<int>(ibProtocol::ibSchema::ibStyle::ibTheme::Dark);
	const int system = static_cast<int>(ibProtocol::ibSchema::ibStyle::ibTheme::System);

	// The frame offers it.
	bool offered = false;
	const ibDataNode* const frame = opened.result.GetBody();
	ASSERT_NE(frame, nullptr);
	if (const auto schemas = frame->FindField(ibProtocol::ibFrame::Schemas))
		for (const ibDataValue& kind : schemas->AsArray())
			offered = offered || kind.AsInt() == static_cast<std::int64_t>(ibProtocol::ibSchema::ibKind::Style);
	EXPECT_TRUE(offered);

	const Rpc before = style(131, 0, 0, connection);
	ASSERT_TRUE(before.ok) << before.message;
	EXPECT_EQ(before.result.GetInt(ibProtocol::ibSchema::ibStyle::Theme, -1), system);

	// Back to the system's however the case ends: the base's no-user row is everybody's, and the other cases log into it.
	struct Reset {
		std::function<void()> m_reset;
		~Reset() { m_reset(); }
	} reset{ [&]() { EXPECT_TRUE(style(141, save, system, connection).ok); g_base->host->Disconnect(&connection); } };

	const Rpc kept = style(132, save, dark, connection);
	ASSERT_TRUE(kept.ok) << kept.message;
	const Rpc after = style(133, 0, 0, connection);
	ASSERT_TRUE(after.ok) << after.message;
	EXPECT_EQ(after.result.GetInt(ibProtocol::ibSchema::ibStyle::Theme, -1), dark);

	// No theme: a number out of the three, one wider than an int whose low 32 bits are 2 (Dark), a number given as text.
	const Rpc refused = style(134, save, 7, connection);
	EXPECT_FALSE(refused.ok);
	EXPECT_EQ(refused.code, static_cast<long long>(ibProtocol::ibRefusal::BadParameter));
	const Rpc wide = call(135, save, [](ibProtocolNode& args) {
		args.SetValue(ibProtocol::ibSchema::ibStyle::Theme, 4294967298LL); }, connection);
	EXPECT_FALSE(wide.ok);
	EXPECT_EQ(wide.code, static_cast<long long>(ibProtocol::ibRefusal::BadParameter));
	const Rpc text = call(136, save, [](ibProtocolNode& args) {
		args.SetValue(ibProtocol::ibSchema::ibStyle::Theme, ibString(ibT("2"))); }, connection);
	EXPECT_FALSE(text.ok);
	EXPECT_EQ(text.code, static_cast<long long>(ibProtocol::ibRefusal::BadParameter));
	const Rpc still = style(137, 0, 0, connection);
	ASSERT_TRUE(still.ok) << still.message;
	EXPECT_EQ(still.result.GetInt(ibProtocol::ibSchema::ibStyle::Theme, -1), dark) << "a refused theme keeps nothing";

	// A storage that would not keep it: the row of the style refused by the base itself (a check on its category, so
	// no other row is touched). The person is told - a refusal, not a frame answered as if it were kept.
	const auto runSql = [](const ibString& sql) {
		ibConnectionScope scope = ibConnectionPool::GetFreeConnection();
		return scope ? scope->RunStatement(sql) : -1;
	};
	const ibString noStyle = ibString::Format(wxT("ALTER TABLE sys_settings ADD CONSTRAINT oes_test_no_style CHECK (category <> %d)"),
		static_cast<int>(ibSettingsCategory::Style));
	ASSERT_NE(runSql(noStyle), -1);
	{
		struct Allow {
			std::function<int(const ibString&)> m_run;
			~Allow() { EXPECT_NE(m_run(wxT("ALTER TABLE sys_settings DROP CONSTRAINT oes_test_no_style")), -1); }
		} allow{ runSql };
		const Rpc unkept = style(138, save, static_cast<int>(ibProtocol::ibSchema::ibStyle::ibTheme::Light), connection);
		EXPECT_FALSE(unkept.ok);
		EXPECT_EQ(unkept.code, static_cast<long long>(ibProtocol::ibRefusal::Exception)) << unkept.message;
	}
	const Rpc stillDark = style(140, 0, 0, connection);
	ASSERT_TRUE(stillDark.ok) << stillDark.message;
	EXPECT_EQ(stillDark.result.GetInt(ibProtocol::ibSchema::ibStyle::Theme, -1), dark) << "an unkept theme is not the one in force";
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
	const ibString client = login.result.GetString(ibProtocol::ibLogin::Client);
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
	const ibString client = login.result.GetString(ibProtocol::ibLogin::Client);
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
	const ibString client = login.result.GetString(ibProtocol::ibLogin::Client);
	const long long frame = login.result.GetInt(ibProtocol::ibFrame::Frame);

	ibProtocolNode params;
	params.SetValue(ibProtocol::ibLogin::Client, client).SetValue(ibProtocol::ibLogin::Since, frame);
	const ibString text = Request(81, "frame", params);
	ibString first;
	ibString second;
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
	const ibString client = login.result.GetString(ibProtocol::ibLogin::Client);

	ibProtocolNode params;
	params.SetValue(ibProtocol::ibLogin::Client, client);
	const ibString text = Request(91, "frame", params);
	const Rpc first = g_base->Call(text, &connection);
	ASSERT_TRUE(first.ok) << first.message;

	int other = 0;
	const ibString stolenText = g_base->host->Call(text, &other, wxT("test"));
	const Rpc stolen = ReadRpc(stolenText);
	EXPECT_FALSE(stolen.ok);
	EXPECT_EQ(stolen.code, 401);

	const ibString cachedText = g_base->host->Call(text, &connection, wxT("test"));
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
		ibString                 text;
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

	const ibString client = login.result.GetString(ibProtocol::ibLogin::Client);
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
	const ibString adminId = admin.result.GetString(ibProtocol::ibLogin::Client);

	int connection = 0;
	const Rpc login = g_base->Call(Request(131, "login", PasswordLogin()), &connection);
	ASSERT_TRUE(login.ok) << login.message;
	const ibString client = login.result.GetString(ibProtocol::ibLogin::Client);
	g_base->host->Disconnect(&connection);

	ibProtocolNode params;
	params.SetValue(ibProtocol::ibLogin::Client, adminId);
	params.SetValue("Schema", static_cast<int>(ibProtocol::ibSchema::ibKind::ActiveUser));

	bool found = false;
	ibString last;
	for (int attempt = 0; attempt < 15 && !found; ++attempt) {
		std::this_thread::sleep_for(std::chrono::milliseconds(200));
		const ibString text = g_base->host->Call(Request(140 + attempt, "schema", params), &watcher, wxT("test"));
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

// What the client holds for the next call goes in its Pending as the communicator puts it there (ibProtocolNode::AddItem
// of each event), and the server reads it so: a list of events, each with its Args.
TEST(SubscribedEvents, TheServerReadsHeldEventsAsTheClientWritesThem)
{
	const auto event = [](int control, const char* name, int value) {
		ibProtocolNode args;
		args.SetValue(name, value);
		ibProtocolNode node;
		node.SetValue(ibProtocol::ibEvent::Control, control)
			.SetValue(ibProtocol::ibEvent::Event, static_cast<int>(ibProtocol::ibEvent::ibKind::Row))
			.SetValue(ibProtocol::ibEvent::Args, args);
		return node;
	};
	ibProtocolNode params;
	params.SetValue(ibProtocol::ibLogin::Client, ibString(wxT("client-1")));
	params.AddItem(ibProtocol::ibLogin::Pending, event(3, ibProtocol::ibEvent::Row, 20));
	params.AddItem(ibProtocol::ibLogin::Pending, event(7, ibProtocol::ibEvent::Row, 500));
	ibProtocolNode request;
	request.SetValue(ibProtocol::ibRpc::Id, 2)
		.SetValue(ibProtocol::ibRpc::Method, ibProtocol::MethodName(ibProtocol::ibMethod::Frame))
		.SetValue(ibProtocol::ibRpc::Params, params);
	ibMemoryBuffer packet;
	request.Write(packet);

	ibRpcRequest read;
	ibString parseError;
	ASSERT_TRUE(ibRpcParseRequest(packet, read, parseError)) << parseError;
	const auto pending = read.m_params.FindField(ibProtocol::ibLogin::Pending);
	ASSERT_TRUE(pending);
	ASSERT_EQ(pending->Kind(), ibDataKind::Array);
	ASSERT_EQ(pending->AsArray().size(), 2u);
	const ibDataValue& last = pending->AsArray()[1];
	ASSERT_EQ(last.Kind(), ibDataKind::Child);
	EXPECT_EQ(last.AsChild()->GetValue<std::int32_t>(ibProtocol::ibEvent::Event),
		static_cast<std::int32_t>(ibProtocol::ibEvent::ibKind::Row));
	EXPECT_EQ(last.AsChild()->GetValue<std::int32_t>(ibProtocol::ibEvent::Control), 7);
	const auto args = last.AsChild()->FindChild(ibProtocol::ibEvent::Args);
	ASSERT_TRUE(args);
	EXPECT_EQ(args->GetValue<std::int32_t>(ibProtocol::ibEvent::Row), 500);
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
