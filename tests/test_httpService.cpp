// =============================================================================
// HTTP services — a configuration answering a request (metaHttpServiceObject.h, httpServiceCall.h).
//
//   1. a template's match, on its own: a literal, a {parameter}, the rest (*), the root;
//   2. what is a template at all, and what is refused;
//   3. among the templates a path matches, the most particular answers — a literal before a parameter, a parameter
//      before the rest — and between equals the one declared first;
//   4. a method by the request's, else Any; the template's methods as Allow lists them;
//   5. the three kinds go through a save and a load whole;
//   6. a request answered end to end, on SQLite, in a session of its own: the handler given the request, its
//      response sent — and 401, 404, 405 and 500 where they are due, 401 before anything is looked up;
//   7. the kind: what the configuration offers among the web addresses, an object it does not have refused, and a
//      registered service answered through the protocol's `serve` — and nothing once it is taken off;
//   8. the save: a method whose Handler the module has no function for is refused, by its Service.Template.Method.
// =============================================================================

#include <gtest/gtest.h>

#include <wx/filename.h>
#include <wx/init.h>
#include <wx/stdpaths.h>

#include "backend/appData.h"
#include "backend/compiler/compileCode.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/databaseQueryBuilder.h"
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#include "backend/httpService/httpServiceCall.h"
#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/metaHttpServiceObject.h"
#include "backend/propertyManager/propertyObject.h"   // ibPropertyGate — a property set as a person sets it
#include "backend/restructureInfo.h"                   // the save's ledger — what a refused save says
#include "backend/served/servedAddressRegistry.h"      // the base's web addresses — where a service is registered
#include "backend/session/sessionRegistry.h"           // Start — a base put together by hand starts its registry
#include "backend/userInfo.h"
#include "backend/utils/passwordHash.hpp"
#include "core/exception.h"
#include "core/guid.h"
#include "core/utils/base64.h"
#include "core/net/httpServer.h"
#include "core/serialize/dataBuilder.h"
#include "frmserver/client/clientHost.h"
#include "protocol/protocol.h"

#include "activeMetaDataAccess.h"

namespace {

using ibParameters = std::vector<std::pair<ibString, ibString>>;

bool Matches(const ibString& urlTemplate, const ibString& path, ibParameters* parameters = nullptr)
{
	std::vector<int> rank;
	ibParameters given;
	const bool matched = ibValueMetaObjectUrlTemplate::Match(urlTemplate, path, rank, given);
	if (parameters != nullptr)
		*parameters = given;
	return matched;
}

// Through the door a person's edit goes through — the inspector's, the MCP server's: the owner asked first.
bool Set(ibValueMetaObject* object, const ibString& name, const ibVariant& value)
{
	ibProperty* const property = object->GetProperty(name);
	return property != nullptr && ibPropertyGate::SetValue(object, property, value);
}

// A service in a configuration in memory: its templates, each with its methods — `GET:Handler`, `Any:Handler`.
struct ibServiceShape {
	ibString                                          m_template;
	std::vector<std::pair<ibHttpMethod, ibString>>    m_methods;
};

ibValueMetaObjectHttpService* MakeService(ibMetaDataConfigurationFile& cfg, const ibString& name,
	const std::vector<ibServiceShape>& shape)
{
	ibValueMetaObjectConfiguration* const root = cfg.GetCommonMetaObject();
	auto* service = dynamic_cast<ibValueMetaObjectHttpService*>(cfg.CreateMetaObject(g_metaHttpServiceCLSID, root, false));
	if (service == nullptr)
		return nullptr;
	cfg.RenameMetaObject(service, name);
	if (!Set(service, ibT("RootURL"), ibVariant(name.Lower())))
		return nullptr;
	int at = 0;
	for (const ibServiceShape& one : shape) {
		ibValueMetaObject* const urlTemplate = cfg.CreateMetaObject(g_metaUrlTemplateCLSID, service, false);
		if (urlTemplate == nullptr)
			return nullptr;
		cfg.RenameMetaObject(urlTemplate, ibString::Format(ibT("Template%d"), ++at));
		if (!Set(urlTemplate, ibT("Template"), ibVariant(one.m_template)))
			return nullptr;
		for (const auto& method : one.m_methods) {
			ibValueMetaObject* const made = cfg.CreateMetaObject(g_metaHttpServiceMethodCLSID, urlTemplate, false);
			if (made == nullptr)
				return nullptr;
			if (!Set(made, ibT("HTTPMethod"), ibVariant(static_cast<long>(method.first)))
				|| !Set(made, ibT("Handler"), ibVariant(method.second)))
				return nullptr;
		}
	}
	return service;
}

ibString Handler(const ibValueMetaObjectHttpService* service, const ibString& path, const char* method)
{
	ibParameters parameters;
	const ibValueMetaObjectUrlTemplate* const found = service->FindTemplate(path, parameters);
	if (found == nullptr)
		return ibT("<404>");
	const ibValueMetaObjectHttpServiceMethod* const answering = found->FindHttpMethod(method);
	return answering != nullptr ? answering->GetHandler() : ibT("<405>");
}

} // namespace

// 1 - the match on its own
TEST(HttpService, ATemplateMatchesItsOwnPaths)
{
	EXPECT_TRUE(Matches(ibT("/Transfers"), ibT("/Transfers")));
	EXPECT_TRUE(Matches(ibT("/Transfers"), ibT("/Transfers/"))) << "a slash at the end says nothing";
	EXPECT_FALSE(Matches(ibT("/Transfers"), ibT("/transfers"))) << "an address is read as written";
	EXPECT_FALSE(Matches(ibT("/Transfers"), ibT("/Transfers/7")));
	EXPECT_FALSE(Matches(ibT("/Transfer/{id}"), ibT("/Transfer"))) << "a parameter is one segment, not none";

	ibParameters parameters;
	ASSERT_TRUE(Matches(ibT("/Transfer/{id}/reset"), ibT("/Transfer/12/reset"), &parameters));
	ASSERT_EQ(parameters.size(), 1u);
	EXPECT_EQ(parameters[0].first, ibString(ibT("id")));
	EXPECT_EQ(parameters[0].second, ibString(ibT("12")));

	EXPECT_TRUE(Matches(ibT("/*"), ibT("/"))) << "the rest may be nothing";
	EXPECT_TRUE(Matches(ibT("/*"), ibT("/a/b/c")));
	EXPECT_TRUE(Matches(ibT("/files/*"), ibT("/files")));
	EXPECT_FALSE(Matches(ibT("/files/*"), ibT("/other/a")));

	EXPECT_TRUE(Matches(ibT("/"), ibT(""))) << "the root is the service's own address";
	EXPECT_TRUE(Matches(ibT("/"), ibT("/")));
	EXPECT_FALSE(Matches(ibT("/"), ibT("/a")));
}

// 2 - a template, or not
TEST(HttpService, ATemplateIsLiteralsParametersAndALastRest)
{
	EXPECT_TRUE(ibValueMetaObjectUrlTemplate::IsTemplate(ibT("/")));
	EXPECT_TRUE(ibValueMetaObjectUrlTemplate::IsTemplate(ibT("/Transfer/{id}/reset")));
	EXPECT_TRUE(ibValueMetaObjectUrlTemplate::IsTemplate(ibT("/*")));
	EXPECT_TRUE(ibValueMetaObjectUrlTemplate::IsTemplate(ibT("/a/{from}/{to}")));

	EXPECT_FALSE(ibValueMetaObjectUrlTemplate::IsTemplate(ibT(""))) << "from the root";
	EXPECT_FALSE(ibValueMetaObjectUrlTemplate::IsTemplate(ibT("Transfer")));
	EXPECT_FALSE(ibValueMetaObjectUrlTemplate::IsTemplate(ibT("/a//b"))) << "no empty segment";
	EXPECT_FALSE(ibValueMetaObjectUrlTemplate::IsTemplate(ibT("/*/a"))) << "the rest is the last segment";
	EXPECT_FALSE(ibValueMetaObjectUrlTemplate::IsTemplate(ibT("/{}"))) << "a parameter has a name";
	EXPECT_FALSE(ibValueMetaObjectUrlTemplate::IsTemplate(ibT("/{a b}")));
	EXPECT_FALSE(ibValueMetaObjectUrlTemplate::IsTemplate(ibT("/{id}/{id}"))) << "no name twice";
	EXPECT_FALSE(ibValueMetaObjectUrlTemplate::IsTemplate(ibT("/a?b")));
	EXPECT_FALSE(ibValueMetaObjectUrlTemplate::IsTemplate(ibT("/file.{ext}"))) << "a parameter is a whole segment";
}

// 3 and 4 - which template, and which method
TEST(HttpService, TheMostParticularTemplateAnswersAndTheMethodByItsName)
{
	ibMetaDataConfigurationFile cfg;
	const ibValueMetaObjectHttpService* const service = MakeService(cfg, ibT("Desk"), {
		{ ibT("/*"),               { { ibHttpMethod_Any, ibT("Rest") } } },
		{ ibT("/Transfer/{id}"),   { { ibHttpMethod_Get, ibT("OneTransfer") }, { ibHttpMethod_Delete, ibT("DropTransfer") } } },
		{ ibT("/Transfer/list"),   { { ibHttpMethod_Get, ibT("TransferList") } } },
		{ ibT("/Transfer/{code}"), { { ibHttpMethod_Get, ibT("Shadowed") } } },
	});
	ASSERT_NE(service, nullptr);

	EXPECT_EQ(Handler(service, ibT("/Transfer/list"), ibHttp::Get), ibString(ibT("TransferList"))) << "a literal before a parameter";
	EXPECT_EQ(Handler(service, ibT("/Transfer/7"), ibHttp::Get), ibString(ibT("OneTransfer"))) << "a parameter before the rest, the first of equals";
	EXPECT_EQ(Handler(service, ibT("/Transfer/7"), ibHttp::Delete), ibString(ibT("DropTransfer")));
	EXPECT_EQ(Handler(service, ibT("/Transfer/7"), ibHttp::Post), ibString(ibT("<405>"))) << "the template has no POST, and Any is another template's";
	EXPECT_EQ(Handler(service, ibT("/anything/else"), ibHttp::Patch), ibString(ibT("Rest"))) << "Any answers every method";

	ibParameters parameters;
	const ibValueMetaObjectUrlTemplate* const found = service->FindTemplate(ibT("/Transfer/7"), parameters);
	ASSERT_NE(found, nullptr);
	EXPECT_EQ(found->GetAllowed(), std::string("GET, DELETE"));
	ASSERT_EQ(parameters.size(), 1u);
	EXPECT_EQ(parameters[0].first, ibString(ibT("id")));
}

// 5 - a save and a load
TEST(HttpService, TheKindsGoThroughASaveAndALoad)
{
	const ibString file = wxFileName(wxStandardPaths::Get().GetTempDir(), wxT("oes-http-service.cfg")).GetFullPath();
	{
		ibMetaDataConfigurationFile cfg;
		ASSERT_NE(MakeService(cfg, ibT("Desk"), { { ibT("/Transfer/{id}"), { { ibHttpMethod_Post, ibT("Reset") } } } }), nullptr);
		ASSERT_TRUE(cfg.SaveConfigToFile(file));
	}

	ibMetaDataConfigurationFile cfg;
	ASSERT_TRUE(cfg.LoadConfigFromFile(file));
	wxRemoveFile(file);

	const std::vector<ibValueMetaObject*> services = cfg.GetAnyArrayObject(g_metaHttpServiceCLSID);
	ASSERT_EQ(services.size(), 1u);
	const auto* service = static_cast<const ibValueMetaObjectHttpService*>(services[0]);
	EXPECT_EQ(service->GetRootUrl(), ibString(ibT("desk")));
	ASSERT_NE(service->GetServiceModule(), nullptr) << "the module goes with its service";
	EXPECT_EQ(Handler(service, ibT("/Transfer/12"), ibHttp::Post), ibString(ibT("Reset")));
	EXPECT_EQ(Handler(service, ibT("/Transfer/12"), ibHttp::Get), ibString(ibT("<405>")));
}

namespace {

// THE BASE: SQLite in a file of its own — the session registry keeps a connection of its own, so the pool is wider
// than one and every connection is the same database — and a configuration made here as its active one, what a
// request's session comes in with (ibSession::GetMetaData).
struct HttpServiceFix : ::testing::Test {
	wxInitializer                                m_wxInit;
	// RunDatabase sets this thread's code style to the configuration's; TearDown puts it back.
	const short                                  m_codeStyle = ibCompileCode::GetCodeStyle();
	ibString                                     m_file;
	std::shared_ptr<ibMetaDataConfigurationFile> cfg;
	ibGuid                                       m_service;   // Desk's object — what its web address names

	void SetUp() override
	{
		if (!m_wxInit.IsOk())
			GTEST_SKIP() << "wxBase init failed (no wxApp host)";
		if (!ibApplicationInstance::CreateAppDataEnv(ibRunMode::eFILE_MODE))
			GTEST_SKIP() << "appData env unavailable headless";
		ibConnectionPool* pool = ibApplicationInstance::GetConnectionPool();
		if (pool == nullptr)
			GTEST_SKIP() << "no connection pool after CreateAppDataEnv";
		m_file = wxFileName::CreateTempFileName(wxT("oes-http-service"));
		auto db = std::make_shared<ibDatabaseLayerSQLite>();
		if (m_file.IsEmpty() || !db->Open(m_file))
			GTEST_SKIP() << "SQLite open failed";
		pool->Init(db, /*poolSize=*/4, /*minIdle=*/0);

		// The users — none, until a test makes one — and the sessions' rows, which the registry writes as it lets
		// a session in; then the registry, started the way a base's Open starts it.
		ibDatabaseQueryBuilder q;
		q.Execute(ibCreateTable(user_table, {
			{ wxT("guid"),       ibTypeString(36),  /*notNull*/false, /*pk*/true },
			{ wxT("name"),       ibTypeString(64),  /*notNull*/true,  /*pk*/false },
			{ wxT("fullName"),   ibTypeString(128), /*notNull*/true,  /*pk*/false },
			{ wxT("changed"),    ibTypeDate(),      /*notNull*/true,  /*pk*/false },
			{ wxT("dataSize"),   ibTypeInteger(),   /*notNull*/true,  /*pk*/false },
			{ wxT("binaryData"), ibTypeBlob(),      /*notNull*/true,  /*pk*/false },
		}));
		ASSERT_NE(db_query->RunQuery(wxT(
			"CREATE TABLE sys_session ("
			"session TEXT PRIMARY KEY, userName TEXT, application INTEGER, "
			"started TEXT, lastActive TEXT, computer TEXT)")), -1);
		ibApplicationInstance::GetSessionRegistry()->Start();

		cfg = ibMetaData::MakeShared<ibMetaDataConfigurationFile>();
		ActiveMetaDataAccess::Replace(cfg);

		ibValueMetaObjectHttpService* const service = MakeService(*cfg, ibT("Desk"), {
			{ ibT("/Transfer/{id}"), { { ibHttpMethod_Post, ibT("TransferPost") } } },
			{ ibT("/broken"),        { { ibHttpMethod_Get, ibT("Broken") }, { ibHttpMethod_Put, ibT("NoResponse") } } },
		});
		ASSERT_NE(service, nullptr);
		m_service = service->GetGuid().GetGuid();
		const_cast<ibValueMetaObjectManagerModule*>(service->GetServiceModule())->SetModuleText(
			wxT("Function TransferPost(Request) {\n")
			wxT("\tvar response = New HTTPServiceResponse(201);\n")
			wxT("\tresponse.Headers.Insert(\"X-Id\", Request.URLParameters.id);\n")
			wxT("\tresponse.SetBodyFromString(Request.HTTPMethod + \"|\" + Request.BaseURL + \"|\" + Request.RelativeURL\n")
			wxT("\t\t+ \"|\" + Request.QueryOptions.note + \"|\" + Request.Headers[\"Content-Type\"] + \"|\" + Request.GetBodyAsString());\n")
			wxT("\treturn response;\n")
			wxT("}\n")
			wxT("Function Broken(Request) {\n")
			wxT("\tRaise(\"the handler broke\");\n")
			wxT("}\n")
			wxT("Function NoResponse(Request) {\n")
			wxT("\treturn \"a string\";\n")
			wxT("}\n"));
		ASSERT_TRUE(cfg->RunDatabase()) << "the configuration in memory did not run";
	}

	// The base's one user, keeper, with the password given.
	void Keep(const ibString& password)
	{
		ibUserInfo keeper;
		keeper.m_strUserGuid = ibNewUniqueGuid;
		keeper.m_strUserName = wxT("keeper");
		keeper.m_strUserFullName = wxT("Keeper");
		keeper.m_strUserPassword = ibPasswordHash::Hash(password);
		ASSERT_TRUE(ibUserInfo::Save(keeper));
	}

	void TearDown() override
	{
		if (cfg)
			cfg->CloseDatabase();
		ibCompileCode::SetCodeStyle(m_codeStyle);
		if (ibApplicationInstance::Get(/*required*/ false) != nullptr)
			ibApplicationInstance::DestroyAppDataEnv();
		if (!m_file.IsEmpty())
			wxRemoveFile(m_file);
	}

	static ibHttpRequest Request(const std::string& method, const std::string& path, const std::string& query = std::string(),
		const std::string& body = std::string())
	{
		ibHttpRequest request;
		request.m_method = method;
		request.m_path = path;
		request.m_query = query;
		request.m_body = body;
		request.m_headers.emplace_back("content-type", "text/plain");
		request.m_address = wxT("127.0.0.1");
		return request;
	}

	static std::string Header(const ibHttpResponse& response, const std::string& name)
	{
		for (const auto& header : response.m_headers) {
			if (header.first == name)
				return header.second;
		}
		return std::string();
	}
};

} // namespace

// 6 - end to end
TEST_F(HttpServiceFix, TheHandlerIsGivenTheRequestAndItsResponseIsSent)
{
	const ibHttpResponse response = ibHttpServiceCall::Answer(ibApplicationInstance::Get(), m_service,
		Request(ibHttp::Post,"/trade/es/desk/Transfer/12", "note=a%20b", "hello"), wxT("/trade/es/desk"));
	EXPECT_EQ(response.m_status, 201) << response.m_body;
	EXPECT_EQ(Header(response, "X-Id"), "12");
	EXPECT_EQ(response.m_body, "POST|/trade/es/desk|/Transfer/12|a b|text/plain|hello");
}

TEST_F(HttpServiceFix, WhatNobodyAnswersIsSaidByItsStatus)
{
	ibApplicationInstance* const base = ibApplicationInstance::Get();
	EXPECT_EQ(ibHttpServiceCall::Answer(base, ibGuid::newGuid(), Request(ibHttp::Get,"/trade/es/desk/x"), wxT("/trade/es/desk")).m_status, 404)
		<< "no such service";
	EXPECT_EQ(ibHttpServiceCall::Answer(base, m_service, Request(ibHttp::Get,"/trade/es/desk/nothing"), wxT("/trade/es/desk")).m_status, 404)
		<< "no such template";

	const ibHttpResponse refused = ibHttpServiceCall::Answer(base, m_service, Request(ibHttp::Get,"/trade/es/desk/Transfer/12"), wxT("/trade/es/desk"));
	EXPECT_EQ(refused.m_status, 405);
	EXPECT_EQ(Header(refused, "Allow"), "POST");

	const ibHttpResponse broken = ibHttpServiceCall::Answer(base, m_service, Request(ibHttp::Get,"/trade/es/desk/broken"), wxT("/trade/es/desk"));
	EXPECT_EQ(broken.m_status, 500);
	EXPECT_EQ(broken.m_body.find("broke"), std::string::npos) << "the handler's words stay in the base's journal";
	EXPECT_EQ(ibHttpServiceCall::Answer(base, m_service, Request(ibHttp::Put,"/trade/es/desk/broken"), wxT("/trade/es/desk")).m_status, 500)
		<< "a handler that returns no HTTPServiceResponse";
}

TEST_F(HttpServiceFix, WhoIsNotLetInIsAnsweredWhoAreYou)
{
	Keep(wxT("right-secret"));
	ibApplicationInstance* const base = ibApplicationInstance::Get();
	const auto basic = [](const std::string& pair) {
		std::string header = "Basic ";
		ibBase64::Append(header, pair.data(), pair.size());
		return header;
	};

	// The right password first: a refused login holds the next one back a while, as a person's does.
	ibHttpRequest right = Request(ibHttp::Post,"/trade/es/desk/Transfer/12", "note=keeper");
	right.m_headers.emplace_back("authorization", basic("keeper:right-secret"));
	EXPECT_EQ(ibHttpServiceCall::Answer(base, m_service, right, wxT("/trade/es/desk")).m_status, 201);

	const ibHttpResponse refused = ibHttpServiceCall::Answer(base, m_service, Request(ibHttp::Post,"/trade/es/desk/Transfer/12"), wxT("/trade/es/desk"));
	EXPECT_EQ(refused.m_status, 401) << "a base with users lets nobody in unnamed";
	EXPECT_EQ(Header(refused, "WWW-Authenticate"), "Basic realm=\"OES\", charset=\"UTF-8\"") << "the platform's realm, not the base's name";
	EXPECT_EQ(ibHttpServiceCall::Answer(base, ibGuid::newGuid(), Request(ibHttp::Get,"/trade/es/nobody/x"), wxT("/trade/es/nobody")).m_status, 401)
		<< "what the base has is told only to whoever is let in";

	ibHttpRequest wrong = Request(ibHttp::Post,"/trade/es/desk/Transfer/12");
	wrong.m_headers.emplace_back("authorization", basic("keeper:wrong-secret"));
	EXPECT_EQ(ibHttpServiceCall::Answer(base, m_service, wrong, wxT("/trade/es/desk")).m_status, 401);
}

namespace {

// The protocol's `serve`, as the web server writes it for a request to /trade/es/desk/… (webserver/webServer.cpp).
ibDataNode ServeCall(const ibGuid& entry, const std::string& method, const std::string& path, const std::string& query,
	const std::string& body)
{
	ibDataNode params;
	params.SetValue(ibProtocol::ibServed::Address, entry.str());
	params.SetValue(ibProtocol::ibServed::Prefix, ibString(wxT("/trade/es/desk")));
	params.SetValue(ibProtocol::ibServed::Method, ibMemoryBuffer(method.data(), method.size()));
	params.SetValue(ibProtocol::ibServed::Path, ibMemoryBuffer(path.data(), path.size()));
	params.SetValue(ibProtocol::ibServed::Query, ibMemoryBuffer(query.data(), query.size()));
	const std::string name = "content-type", value = "text/plain";
	auto type = std::make_shared<ibDataNode>();
	type->SetValue(ibProtocol::ibServed::Name, ibMemoryBuffer(name.data(), name.size()));
	type->SetValue(ibProtocol::ibServed::Value, ibMemoryBuffer(value.data(), value.size()));
	std::vector<ibDataValue> headers{ ibDataValue::Child(type) };
	params.AddField(ibProtocol::ibServed::Headers, ibDataValue::Array(std::move(headers)));
	params.SetValue(ibProtocol::ibServed::Body, ibMemoryBuffer(body.data(), body.size()));
	return params;
}

// The answer's words, as the bytes they are written in.
std::string BytesOf(const ibDataNode& node, const char* field)
{
	const ibMemoryBuffer bytes = node.GetValue<ibMemoryBuffer>(field);
	return std::string(static_cast<const char*>(bytes.GetData()), bytes.GetDataLen());
}

std::string HeaderOf(const ibDataNode& result, const std::string& name)
{
	if (const auto list = result.FindField(ibProtocol::ibServed::Headers)) {
		for (const ibDataValue& item : list->AsArray()) {
			const std::optional<ibDataNode> header = item.AsChild();
			if (header && BytesOf(*header, ibProtocol::ibServed::Name) == name)
				return BytesOf(*header, ibProtocol::ibServed::Value);
		}
	}
	return std::string();
}

std::string BodyOf(const ibDataNode& result)
{
	return BytesOf(result, ibProtocol::ibServed::Body);
}

} // namespace

// 7 - the kind, through the base's web addresses
TEST_F(HttpServiceFix, ARegisteredServiceIsAnsweredThroughServe)
{
	ibServedAddressRegistry* const registry = ibApplicationInstance::GetServedAddressRegistry();
	ASSERT_NE(registry, nullptr);
	const ibServedKind* const kind = ibServedKind::Find(ibT("HTTPService"));
	ASSERT_NE(kind, nullptr) << "the kind registers itself with the engine";

	// What the configuration offers: the service at es/<RootURL>, by its object.
	const std::vector<ibServedAddress> offers = kind->Offer();
	ASSERT_EQ(offers.size(), 1u);
	EXPECT_EQ(offers[0].m_path, ibString(ibT("es/desk")));
	EXPECT_EQ(offers[0].m_object, m_service);

	// An object the configuration does not have is refused by the kind, before anything is listed.
	ibServedAddress stranger = offers[0];
	stranger.m_object = ibGuid::newGuid();
	stranger.m_owner = ibGuid::newGuid();
	EXPECT_THROW(registry->Register(stranger), ibCoreException);
	EXPECT_TRUE(registry->GetServedAddressArray().empty());

	// Registered — a session's entry here: the kept ones go to the base under its lock manager — and asked through the
	// protocol's `serve`, as the web server asks it.
	ibServedAddress entry = offers[0];
	entry.m_owner = ibGuid::newGuid();
	const ibGuid id = registry->Register(entry);

	ibClientHost host(ibApplicationInstance::Get());
	ibDataNode result;
	ibProtocol::ibRefusal refusal = ibProtocol::ibRefusal::None;
	ibString error;
	ASSERT_TRUE(host.Call(ibProtocol::ibMethod::Serve, ServeCall(id, ibHttp::Post,"/trade/es/desk/Transfer/7", "note=n", "body"),
		result, refusal, error)) << error.ToStdString();
	EXPECT_EQ(result.GetValue<std::int32_t>(ibProtocol::ibServed::Status), 201);
	EXPECT_EQ(HeaderOf(result, "X-Id"), "7");
	EXPECT_EQ(BodyOf(result), "POST|/trade/es/desk|/Transfer/7|n|text/plain|body");

	// Taken off, it answers nothing — what is published is a choice.
	registry->Unregister(id);
	ibDataNode gone;
	ASSERT_TRUE(host.Call(ibProtocol::ibMethod::Serve, ServeCall(id, ibHttp::Post,"/trade/es/desk/Transfer/7", "", ""),
		gone, refusal, error)) << error.ToStdString();
	EXPECT_EQ(gone.GetValue<std::int32_t>(ibProtocol::ibServed::Status), 404);
}

// 8 - the save
TEST(HttpService, AMethodNobodyAnswersIsNotSaved)
{
	// A SAVE WRITES ITS REFUSALS INTO THE ACTIVE CONFIGURATION'S LEDGER (ibMetaDataConfigurationBase::GetRestructureInfo),
	// so the configuration saved here is made the active one, and put back after — test_predefinedCells' steps. With
	// none active the ledger was read through a null: an access violation on its first Clear, on every platform.
	struct ActiveConfiguration {
		std::shared_ptr<ibMetaDataConfigurationFile> m_cfg;
		const short m_codeStyle = ibCompileCode::GetCodeStyle();
		bool m_ok = false;
		ActiveConfiguration()
		{
			if (!ibApplicationInstance::CreateAppDataEnv(ibRunMode::eFILE_MODE))
				return;
			m_cfg = ibMetaData::MakeShared<ibMetaDataConfigurationFile>();
			ActiveMetaDataAccess::Replace(m_cfg);
			m_ok = true;
		}
		~ActiveConfiguration()
		{
			if (!m_ok)
				return;
			ibCompileCode::SetCodeStyle(m_codeStyle);
			ibApplicationInstance::DestroyAppDataEnv();
		}
	} active;
	if (!active.m_ok)
		GTEST_SKIP() << "appData env unavailable headless";
	ibMetaDataConfigurationFile& cfg = *active.m_cfg;

	ibValueMetaObjectHttpService* const service = MakeService(cfg, ibT("Desk"), {
		{ ibT("/Transfer/{id}"), { { ibHttpMethod_Post, ibT("TransferPost") }, { ibHttpMethod_Get, ibT("Missing") } } },
	});
	ASSERT_NE(service, nullptr);
	const_cast<ibValueMetaObjectManagerModule*>(service->GetServiceModule())->SetModuleText(
		wxT("Function TransferPost(Request) {\n")
		wxT("\treturn New HTTPServiceResponse(200);\n")
		wxT("}\n"));

	ibRestructureInfo& ledger = ibMetaDataConfigurationBase::GetRestructureInfo();
	ledger.Clear();
	EXPECT_FALSE(service->OnSaveMetaObject(0)) << "Missing has no function in the module";
	ibString said;
	for (const ibRestructureInfo::Entry& line : ledger)
		said += line.descr;
	ledger.Clear();
	EXPECT_NE(said.Find(ibT("Desk.")), wxNOT_FOUND) << said.ToStdString();
	EXPECT_NE(said.Find(ibT("'Missing'")), wxNOT_FOUND) << said.ToStdString();
	EXPECT_EQ(said.Find(ibT("'TransferPost'")), wxNOT_FOUND) << "the method whose function is there is not named";
}
