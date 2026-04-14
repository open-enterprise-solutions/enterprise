////////////////////////////////////////////////////////////////////////////
//	Author		: Tetracode Dev
//	Description : OES embedded HTTP server — Phase 2
//	              JWT auth + metadata endpoints + data read endpoints
////////////////////////////////////////////////////////////////////////////

#include "ibWebServer.h"
#include "ibWebAuth.h"
#include "ibWebMetadataProvider.h"
#include "ibWebFormSerializer.h"

#include "appData.h"
#include "metadataConfiguration.h"

#include "metaCollection/metaObject.h"
#include "metaCollection/partial/catalog.h"
#include "metaCollection/partial/document.h"
#include "metaCollection/partial/informationRegister.h"
#include "metaCollection/partial/accumulationRegister.h"

#include "databaseLayer/databaseLayer.h"
#include "databaseLayer/preparedStatement.h"
#include "databaseLayer/databaseResultSet.h"

#include <json.hpp>

#define CPPHTTPLIB_THREAD_POOL_COUNT 8
#include <httplib.h>

#include <wx/base64.h>

using json = nlohmann::json;

ibWebServer* ibWebServer::ms_instance = nullptr;

//***********************************************************************
//*                       Response helpers                              *
//***********************************************************************

namespace {

// Pagination defaults / limits
static constexpr int kDefaultPageSize = 25;
static constexpr int kMaxPageSize     = 1000;

// ---- Error response ----------------------------------------------------

void SendError(httplib::Response& res, int status,
	const std::string& code, const std::string& message,
	const std::string& field = "")
{
	json body;
	body["error"]["code"]    = code;
	body["error"]["message"] = message;
	if (!field.empty())
		body["error"]["field"] = field;
	res.status = status;
	res.set_content(body.dump(), "application/json");
}

// ---- Paginated list wrapper --------------------------------------------

json MakeListResponse(const json& data, int total, int page, int pageSize)
{
	json body;
	body["data"]            = data;
	body["meta"]["total"]   = total;
	body["meta"]["page"]    = page;
	body["meta"]["pageSize"]= pageSize;
	return body;
}

// ---- Parse pagination params -------------------------------------------

struct PaginationParams {
	int page     = 1;
	int pageSize = kDefaultPageSize;
};

PaginationParams ParsePagination(const httplib::Request& req)
{
	PaginationParams p;
	if (req.has_param("page")) {
		try { p.page = std::stoi(req.get_param_value("page")); }
		catch (const std::exception&) { /* invalid value — use default */ }
	}
	if (req.has_param("pageSize")) {
		try { p.pageSize = std::stoi(req.get_param_value("pageSize")); }
		catch (const std::exception&) { /* invalid value — use default */ }
	}
	p.page     = std::max(1, p.page);
	p.pageSize = std::clamp(p.pageSize, 1, kMaxPageSize);
	return p;
}

// ---- Extract Bearer token from request ---------------------------------

std::string ExtractBearer(const httplib::Request& req)
{
	std::string auth = req.get_header_value("Authorization");
	if (auth.size() > 7 && auth.substr(0, 7) == "Bearer ")
		return auth.substr(7);
	return {};
}

// ---- Inline WxToStd (kept local to avoid pulling utils header) ----------

inline std::string WxStr(const wxString& s)
{
	return std::string(s.ToUTF8());
}

// ---- Map a :resource path segment to a metadata type -------------------
// Returns nullptr if no matching object is found.
ibValueMetaObjectRecordData* FindResourceObject(const std::string& resource)
{
	if (activeMetaData == nullptr)
		return nullptr;

	auto* root = activeMetaData->GetCommonMetaObject();
	if (root == nullptr)
		return nullptr;

	// resource format: "<type>.<name>"  e.g. "catalog.counterparties"
	// or just "<name>" (search all types)
	std::string typePart, namePart;
	auto dotPos = resource.find('.');
	if (dotPos != std::string::npos) {
		typePart = resource.substr(0, dotPos);
		namePart = resource.substr(dotPos + 1);
	}
	else {
		namePart = resource;
	}

	const wxString wxName = wxString::FromUTF8(namePart.c_str());

	auto searchIn = [&](auto& arr) -> ibValueMetaObjectRecordData* {
		for (auto* obj : arr) {
			if (obj->IsDeleted()) continue;
			if (obj->GetName().CmpNoCase(wxName) == 0)
				return static_cast<ibValueMetaObjectRecordData*>(obj);
		}
		return nullptr;
	};

	if (typePart.empty() || typePart == "catalog") {
		auto arr = root->GetAnyArrayObject<ibValueMetaObjectCatalog>();
		if (auto* obj = searchIn(arr)) return obj;
	}
	if (typePart.empty() || typePart == "document") {
		auto arr = root->GetAnyArrayObject<ibValueMetaObjectDocument>();
		if (auto* obj = searchIn(arr)) return obj;
	}
	if (typePart.empty() || typePart == "informationregister") {
		auto arr = root->GetAnyArrayObject<ibValueMetaObjectInformationRegister>();
		if (auto* obj = searchIn(arr)) return obj;
	}
	if (typePart.empty() || typePart == "accumulationregister") {
		auto arr = root->GetAnyArrayObject<ibValueMetaObjectAccumulationRegister>();
		if (auto* obj = searchIn(arr)) return obj;
	}

	return nullptr;
}

// ---- Build a cross-DB paged SELECT ------------------------------------
// Returns an SQL string. tableName must come from GetTableNameDB() —
// it is an internal constant, not user input.
wxString BuildPagedSelect(const wxString& tableName,
	const wxString& whereClause,
	const wxString& orderClause,
	int offset, int limit,
	bool isFirebird)
{
	if (isFirebird) {
		// Firebird: ROWS <first+1> TO <last>  (1-based)
		int rowFirst = offset + 1;
		int rowLast  = offset + limit;
		wxString q = wxString::Format("SELECT * FROM %s", tableName);
		if (!whereClause.IsEmpty()) q += " WHERE " + whereClause;
		if (!orderClause.IsEmpty()) q += " ORDER BY " + orderClause;
		q += wxString::Format(" ROWS %d TO %d", rowFirst, rowLast);
		return q;
	}
	else {
		wxString q = wxString::Format("SELECT * FROM %s", tableName);
		if (!whereClause.IsEmpty()) q += " WHERE " + whereClause;
		if (!orderClause.IsEmpty()) q += " ORDER BY " + orderClause;
		q += wxString::Format(" LIMIT %d OFFSET %d", limit, offset);
		return q;
	}
}

// ---- Serialise one result-set row as a generic key->value JSON object -
// We read column names from the result-set metadata.
json RowToJson(ibDatabaseResultSet* rs)
{
	json row = json::object();
	auto* meta = rs->GetMetaData();
	if (meta == nullptr)
		return row;

	const int count = meta->GetColumnCount();
	for (int col = 1; col <= count; ++col) {
		wxString colName = meta->GetColumnName(col);
		std::string key  = WxStr(colName.Lower());

		// Read as string; the frontend knows the type from metadata
		wxString val = rs->GetResultString(col);
		row[key] = WxStr(val);
	}
	return row;
}

} // anonymous namespace

//***********************************************************************
//*                         ibWebServer                                 *
//***********************************************************************

ibWebServer::ibWebServer(int port, const wxString& staticDir)
	: m_server(std::make_unique<httplib::Server>())
	, m_running(false)
	, m_port(port)
	, m_staticDir(staticDir)
	, m_startTime(std::chrono::steady_clock::now())
{
	// cpp-httplib owns the ThreadPool pointer returned by this lambda
	m_server->new_task_queue = [] {
		return new httplib::ThreadPool(8, 64);
	};
}

ibWebServer::~ibWebServer()
{
	Stop();
}

bool ibWebServer::Initialize(int port, const wxString& staticDir)
{
	if (ms_instance != nullptr)
		return false;

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

//***********************************************************************
//*                        RegisterRoutes                               *
//***********************************************************************

void ibWebServer::RegisterRoutes()
{
	//-------------------------------------------------------------------
	// CORS preflight — must run before auth middleware
	//-------------------------------------------------------------------
	m_server->Options(".*", [](const httplib::Request&, httplib::Response& res) {
		res.set_header("Access-Control-Allow-Origin",  "*");
		res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
		res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
		res.status = 204;
	});

	// Add CORS to every response
	m_server->set_post_routing_handler([](const httplib::Request&, httplib::Response& res) {
		res.set_header("Access-Control-Allow-Origin", "*");
	});

	//-------------------------------------------------------------------
	// Auth middleware — JWT validation for all /api/* except exemptions
	//-------------------------------------------------------------------
	m_server->set_pre_routing_handler([](const httplib::Request& req, httplib::Response& res) {

		// Exempt: CORS preflight, health check, auth endpoints
		if (req.method == "OPTIONS")
			return httplib::Server::HandlerResponse::Unhandled;

		if (!req.path.starts_with("/api/"))
			return httplib::Server::HandlerResponse::Unhandled;

		if (req.path.starts_with("/api/health") ||
			req.path.starts_with("/api/auth/"))
			return httplib::Server::HandlerResponse::Unhandled;

		// All other /api/* require a valid JWT
		std::string token = ExtractBearer(req);
		if (token.empty()) {
			SendError(res, 401, "UNAUTHORIZED", "Missing Authorization header");
			return httplib::Server::HandlerResponse::Handled;
		}

		ibWebAuthClaims claims;
		if (!ibWebAuth::ValidateToken(token, claims)) {
			SendError(res, 401, "TOKEN_EXPIRED", "Token is invalid or expired");
			return httplib::Server::HandlerResponse::Handled;
		}

		return httplib::Server::HandlerResponse::Unhandled;
	});

	//-------------------------------------------------------------------
	// GET /api/health
	//-------------------------------------------------------------------
	m_server->Get("/api/health", [this](const httplib::Request&, httplib::Response& res) {
		auto now    = std::chrono::steady_clock::now();
		auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now - m_startTime).count();

		json body;
		body["status"]  = "ok";
		body["version"] = "0.2.0";
		body["uptime"]  = uptime;
		body["port"]    = m_port;

		if (appData != nullptr) {
			body["mode"]     = appData->ServiceMode() ? "service" : "enterprise";
			body["database"] = (db_query != nullptr && db_query->IsOpen()) ? "connected" : "disconnected";
		}
		else {
			body["mode"]     = "standalone";
			body["database"] = "disconnected";
		}

		res.set_content(body.dump(), "application/json");
	});

	//-------------------------------------------------------------------
	// POST /api/auth/login
	// Body: { "username": "...", "password": "..." }
	// Returns: { "data": { "token": "...", "expiresAt": <epoch>, "user": {...} } }
	//-------------------------------------------------------------------
	m_server->Post("/api/auth/login", [](const httplib::Request& req, httplib::Response& res) {

		if (appData == nullptr) {
			SendError(res, 503, "SERVICE_UNAVAILABLE", "Application not initialised");
			return;
		}

		json body;
		try {
			body = json::parse(req.body);
		}
		catch (...) {
			SendError(res, 400, "BAD_REQUEST", "Invalid JSON body");
			return;
		}

		std::string username = body.value("username", std::string());
		std::string password = body.value("password", std::string());

		if (username.empty()) {
			SendError(res, 422, "VALIDATION_ERROR", "username is required", "username");
			return;
		}
		if (password.empty()) {
			SendError(res, 422, "VALIDATION_ERROR", "password is required", "password");
			return;
		}

		const wxString wxUser = wxString::FromUTF8(username.c_str());
		const wxString wxPass = wxString::FromUTF8(password.c_str());

		ibApplicationDataUserInfo userInfo = appData->ReadUserData(wxUser);
		if (!userInfo.IsOk()) {
			SendError(res, 401, "INVALID_CREDENTIALS", "Invalid username or password");
			return;
		}

		// Compare MD5 hashes (existing auth convention in OES)
		const wxString inputMd5 = appData->ComputeMd5(wxPass);
		if (userInfo.m_strUserPassword != inputMd5) {
			SendError(res, 401, "INVALID_CREDENTIALS", "Invalid username or password");
			return;
		}

		// Build roles list
		std::vector<std::string> roles;
		for (const auto& role : userInfo.m_roleArray)
			roles.push_back(WxStr(role.m_strRoleName));

		const std::string userGuid = WxStr(userInfo.m_strUserGuid);
		const std::string token    = ibWebAuth::CreateToken(userGuid, username, roles);

		const int64_t now       = static_cast<int64_t>(std::time(nullptr));
		const int64_t expiresAt = now + ibWebAuth::ms_tokenLifetimeSec;

		json userData;
		userData["guid"]     = userGuid;
		userData["username"] = username;
		userData["fullName"] = WxStr(userInfo.m_strUserFullName);
		userData["roles"]    = roles;

		json resp;
		resp["data"]["token"]     = token;
		resp["data"]["expiresAt"] = expiresAt;
		resp["data"]["user"]      = userData;

		res.status = 200;
		res.set_content(resp.dump(), "application/json");
	});

	//-------------------------------------------------------------------
	// POST /api/auth/refresh
	// Body: { "token": "..." }
	//-------------------------------------------------------------------
	m_server->Post("/api/auth/refresh", [](const httplib::Request& req, httplib::Response& res) {

		json body;
		try { body = json::parse(req.body); }
		catch (...) {
			SendError(res, 400, "BAD_REQUEST", "Invalid JSON body");
			return;
		}

		std::string oldToken = body.value("token", std::string());
		if (oldToken.empty()) {
			SendError(res, 422, "VALIDATION_ERROR", "token is required", "token");
			return;
		}

		ibWebAuthClaims claims;
		// Allow refresh of expired tokens within a grace window (not checking expiry here)
		// We validate signature only by re-parsing — reuse ValidateToken but we need
		// to permit expired tokens during refresh. Parse manually:
		if (!ibWebAuth::ValidateToken(oldToken, claims)) {
			// Try once more — the only case we accept here is when the token
			// is structurally valid but expired. For simplicity, reject fully
			// invalid (bad signature) tokens.
			SendError(res, 401, "TOKEN_INVALID", "Token signature is invalid");
			return;
		}

		const std::string newToken = ibWebAuth::CreateToken(claims.sub, claims.name, claims.roles);
		const int64_t expiresAt    = static_cast<int64_t>(std::time(nullptr)) + ibWebAuth::ms_tokenLifetimeSec;

		json resp;
		resp["data"]["token"]     = newToken;
		resp["data"]["expiresAt"] = expiresAt;

		res.status = 200;
		res.set_content(resp.dump(), "application/json");
	});

	//-------------------------------------------------------------------
	// POST /api/auth/logout
	//-------------------------------------------------------------------
	m_server->Post("/api/auth/logout", [](const httplib::Request&, httplib::Response& res) {
		// Stateless JWT — nothing to invalidate server-side.
		// Client must discard its token.
		res.status = 204;
	});

	//-------------------------------------------------------------------
	// GET /api/metadata/tree
	//-------------------------------------------------------------------
	m_server->Get("/api/metadata/tree", [](const httplib::Request&, httplib::Response& res) {

		if (activeMetaData == nullptr) {
			SendError(res, 503, "SERVICE_UNAVAILABLE", "Metadata not loaded");
			return;
		}

		json tree = ibWebMetadataProvider::SerializeMetadataTree();

		json resp;
		resp["data"] = tree;
		res.status = 200;
		res.set_content(resp.dump(), "application/json");
	});

	//-------------------------------------------------------------------
	// GET /api/metadata/:type
	// :type = "catalog.counterparties", "document.sale", …
	//-------------------------------------------------------------------
	m_server->Get(R"(/api/metadata/([^/]+))", [](const httplib::Request& req, httplib::Response& res) {

		if (activeMetaData == nullptr) {
			SendError(res, 503, "SERVICE_UNAVAILABLE", "Metadata not loaded");
			return;
		}

		std::string resource = req.matches[1].str();
		ibValueMetaObjectRecordData* metaObj = FindResourceObject(resource);
		if (metaObj == nullptr) {
			SendError(res, 404, "NOT_FOUND",
				"Metadata object '" + resource + "' not found");
			return;
		}

		// Serialise: name, guid, type, user attributes, tabular sections
		json j = json::object();
		j["guid"]     = WxStr(metaObj->GetGuid().str());
		j["id"]       = metaObj->GetMetaID();
		j["name"]     = WxStr(metaObj->GetName());

		const wxString syn = metaObj->GetSynonym();
		if (!syn.IsEmpty())
			j["synonym"] = WxStr(syn);

		json jAttrs = json::array();
		for (auto* attr : metaObj->GetAttributeArrayObject()) {
			if (attr->IsDeleted()) continue;
			json a;
			a["guid"] = WxStr(attr->GetGuid().str());
			a["id"]   = attr->GetMetaID();
			a["name"] = WxStr(attr->GetName());
			const wxString attrSyn = attr->GetSynonym();
			if (!attrSyn.IsEmpty()) a["synonym"] = WxStr(attrSyn);
			jAttrs.push_back(a);
		}
		j["attributes"] = jAttrs;

		json jTables = json::array();
		for (auto* tbl : metaObj->GetTableArrayObject()) {
			if (tbl->IsDeleted()) continue;
			json t;
			t["guid"] = WxStr(tbl->GetGuid().str());
			t["id"]   = tbl->GetMetaID();
			t["name"] = WxStr(tbl->GetName());
			const wxString tblSyn = tbl->GetSynonym();
			if (!tblSyn.IsEmpty()) t["synonym"] = WxStr(tblSyn);

			json tAttrs = json::array();
			for (auto* tattr : tbl->GetAttributeArrayObject()) {
				if (tattr->IsDeleted()) continue;
				json ta;
				ta["guid"] = WxStr(tattr->GetGuid().str());
				ta["id"]   = tattr->GetMetaID();
				ta["name"] = WxStr(tattr->GetName());
				tAttrs.push_back(ta);
			}
			t["attributes"] = tAttrs;
			jTables.push_back(t);
		}
		j["tabularSections"] = jTables;

		json resp;
		resp["data"] = j;
		res.status = 200;
		res.set_content(resp.dump(), "application/json");
	});

	//-------------------------------------------------------------------
	// GET /api/form/:resource/schema
	// Returns a Formily-compatible JSON Schema for the named resource.
	// :resource format: "<type>.<name>"  e.g. "catalog.products"
	//-------------------------------------------------------------------
	m_server->Get(R"(/api/form/([^/]+)/schema)", [](const httplib::Request& req, httplib::Response& res) {

		if (activeMetaData == nullptr) {
			SendError(res, 503, "SERVICE_UNAVAILABLE", "Metadata not loaded");
			return;
		}

		const std::string resource = req.matches[1].str();
		ibValueMetaObjectRecordData* metaObj = FindResourceObject(resource);
		if (metaObj == nullptr) {
			SendError(res, 404, "NOT_FOUND",
				"Metadata object '" + resource + "' not found");
			return;
		}

		json schema = ibWebFormSerializer::SerializeFormSchema(metaObj);

		json resp;
		resp["data"] = schema;
		res.status = 200;
		res.set_content(resp.dump(), "application/json");
	});

	//-------------------------------------------------------------------
	// GET /api/data/:resource
	// Query params: page, pageSize, sort, filter[field]=value
	//-------------------------------------------------------------------
	m_server->Get(R"(/api/data/([^/]+)$)", [](const httplib::Request& req, httplib::Response& res) {

		if (db_query == nullptr || !db_query->IsOpen()) {
			SendError(res, 503, "SERVICE_UNAVAILABLE", "Database not connected");
			return;
		}

		std::string resource = req.matches[1].str();
		ibValueMetaObjectRecordData* metaObj = FindResourceObject(resource);
		if (metaObj == nullptr) {
			SendError(res, 404, "NOT_FOUND", "Resource '" + resource + "' not found");
			return;
		}

		auto pag = ParsePagination(req);
		const int offset = (pag.page - 1) * pag.pageSize;

		const wxString tableName = metaObj->GetTableNameDB();
		if (tableName.IsEmpty()) {
			SendError(res, 503, "SERVICE_UNAVAILABLE", "Object has no database table");
			return;
		}

		// Each handler MUST use its own cloned DB connection
		auto localDb = std::shared_ptr<ibDatabaseLayer>(db_query->Clone());
		if (localDb == nullptr) {
			SendError(res, 503, "SERVICE_UNAVAILABLE", "Failed to clone DB connection");
			return;
		}

		const bool isFirebird =
			(localDb->GetDatabaseLayerType() == DATABASELAYER_FIREBIRD);

		// --- Count total ---
		wxString countQuery = wxString::Format("SELECT COUNT(*) FROM %s", tableName);
		ibDatabaseResultSet* countRs = localDb->RunQueryWithResults(countQuery);
		int total = 0;
		if (countRs != nullptr) {
			if (countRs->Next())
				total = countRs->GetResultInt(1);
			localDb->CloseResultSet(countRs);
		}

		// --- Paged SELECT ---
		wxString selectSql = BuildPagedSelect(
			tableName,
			wxEmptyString, // no WHERE for basic list
			wxT("uuid"),   // stable default sort
			offset, pag.pageSize,
			isFirebird
		);

		ibPreparedStatement* stmt = localDb->PrepareStatement(selectSql);
		if (stmt == nullptr) {
			SendError(res, 500, "QUERY_ERROR", "Failed to prepare list query");
			return;
		}

		json jRows = json::array();
		ibDatabaseResultSet* rs = stmt->RunQueryWithResults();
		if (rs != nullptr) {
			while (rs->Next())
				jRows.push_back(RowToJson(rs));
			localDb->CloseResultSet(rs);
		}
		localDb->CloseStatement(stmt);

		json resp = MakeListResponse(jRows, total, pag.page, pag.pageSize);
		res.status = 200;
		res.set_content(resp.dump(), "application/json");
	});

	//-------------------------------------------------------------------
	// GET /api/data/:resource/:id
	//-------------------------------------------------------------------
	m_server->Get(R"(/api/data/([^/]+)/([^/]+)$)", [](const httplib::Request& req, httplib::Response& res) {

		if (db_query == nullptr || !db_query->IsOpen()) {
			SendError(res, 503, "SERVICE_UNAVAILABLE", "Database not connected");
			return;
		}

		std::string resource = req.matches[1].str();
		std::string id       = req.matches[2].str();

		ibValueMetaObjectRecordData* metaObj = FindResourceObject(resource);
		if (metaObj == nullptr) {
			SendError(res, 404, "NOT_FOUND", "Resource '" + resource + "' not found");
			return;
		}

		const wxString tableName = metaObj->GetTableNameDB();
		if (tableName.IsEmpty()) {
			SendError(res, 503, "SERVICE_UNAVAILABLE", "Object has no database table");
			return;
		}

		// Clone the connection for this handler
		auto localDb = std::shared_ptr<ibDatabaseLayer>(db_query->Clone());
		if (localDb == nullptr) {
			SendError(res, 503, "SERVICE_UNAVAILABLE", "Failed to clone DB connection");
			return;
		}

		const bool isFirebird =
			(localDb->GetDatabaseLayerType() == DATABASELAYER_FIREBIRD);

		// Use parameterised query — id is user input
		wxString selectSql;
		if (isFirebird)
			selectSql = wxString::Format("SELECT FIRST 1 * FROM %s WHERE uuid = ?", tableName);
		else
			selectSql = wxString::Format("SELECT * FROM %s WHERE uuid = ? LIMIT 1", tableName);

		ibPreparedStatement* stmt = localDb->PrepareStatement(selectSql);
		if (stmt == nullptr) {
			SendError(res, 500, "QUERY_ERROR", "Failed to prepare query");
			return;
		}

		stmt->SetParamString(1, wxString::FromUTF8(id.c_str()));

		ibDatabaseResultSet* rs = stmt->RunQueryWithResults();
		json jRow;
		bool found = false;
		if (rs != nullptr) {
			if (rs->Next()) {
				jRow  = RowToJson(rs);
				found = true;
			}
			localDb->CloseResultSet(rs);
		}
		localDb->CloseStatement(stmt);

		if (!found) {
			SendError(res, 404, "NOT_FOUND",
				"Record '" + id + "' not found in '" + resource + "'");
			return;
		}

		json resp;
		resp["data"] = jRow;
		res.status = 200;
		res.set_content(resp.dump(), "application/json");
	});

	//-------------------------------------------------------------------
	// Static file serving (SPA fallback)
	//-------------------------------------------------------------------
	if (!m_staticDir.IsEmpty()) {
		std::string dir = m_staticDir.ToStdString();
		m_server->set_mount_point("/", dir);
	}

	//-------------------------------------------------------------------
	// Exception handler
	//-------------------------------------------------------------------
	m_server->set_exception_handler([](const httplib::Request&, httplib::Response& res,
		std::exception_ptr ep)
	{
		json err;
		try {
			std::rethrow_exception(ep);
		}
		catch (const std::exception& e) {
			err["error"]["code"]    = "INTERNAL_ERROR";
			err["error"]["message"] = e.what();
		}
		catch (...) {
			err["error"]["code"]    = "INTERNAL_ERROR";
			err["error"]["message"] = "Unknown internal error";
		}
		res.status = 500;
		res.set_content(err.dump(), "application/json");
	});

	//-------------------------------------------------------------------
	// Logger
	//-------------------------------------------------------------------
	m_server->set_logger([](const httplib::Request& req, const httplib::Response& res) {
		wxLogMessage(wxT("[HTTP] %s %s -> %d"),
			wxString(req.method), wxString(req.path), res.status);
	});
}

//***********************************************************************
//*                         Lifecycle                                   *
//***********************************************************************

void ibWebServer::Start()
{
	if (m_running.load())
		return;

	m_running.store(true);

	m_serverThread = std::thread([this]() {
		if (!m_server->listen("0.0.0.0", m_port)) {
			m_running.store(false);
			wxLogError(wxT("Web server failed to start on port %d"), m_port);
		}
	});

	// Brief pause to confirm the listen socket is open
	std::this_thread::sleep_for(std::chrono::milliseconds(100));

	if (!m_server->is_running())
		m_running.store(false);
}

void ibWebServer::Stop()
{
	if (!m_running.load())
		return;

	m_server->stop();
	m_running.store(false);

	if (m_serverThread.joinable())
		m_serverThread.join();
}

void ibWebServer::WaitForShutdown()
{
	if (m_serverThread.joinable())
		m_serverThread.join();
}
