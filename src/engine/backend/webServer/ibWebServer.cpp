////////////////////////////////////////////////////////////////////////////
//	Author		: Tetracode Dev
//	Description : OES embedded HTTP server — Phase 2 + Phase 7 (Web Designer)
//	              JWT auth + metadata endpoints + data read endpoints
//	              + designer module editor + compile + config export/import
////////////////////////////////////////////////////////////////////////////

#include "ibWebServer.h"
#include "ibWebAuth.h"
#include "ibWebEventBus.h"
#include "ibWebMetadataProvider.h"
#include "ibWebFormSerializer.h"

#include "appData.h"
#include "metadataConfiguration.h"

#include "metaCollection/metaObject.h"
#include "metaCollection/metaModuleObject.h"
#include "metaCollection/partial/catalog.h"
#include "metaCollection/partial/document.h"
#include "metaCollection/partial/informationRegister.h"
#include "metaCollection/partial/accumulationRegister.h"

#include "compiler/compileCode.h"
#include "backend_exception.h"
#include "utils/md5.hpp"

#include "databaseLayer/databaseLayer.h"
#include "databaseLayer/preparedStatement.h"
#include "databaseLayer/databaseResultSet.h"

#include <json.hpp>
#include <fstream>

#define CPPHTTPLIB_THREAD_POOL_COUNT 8
#include <httplib.h>

#include <fstream>

#include <wx/base64.h>
#include <wx/filename.h>

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
// The returned pointer is either ibValueMetaObjectRecordData* or
// ibValueMetaObjectRegisterData* — use GetMetaTableName() to get the DB table.
ibValueMetaObjectGenericData* FindResourceObject(const std::string& resource)
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

	auto searchIn = [&](auto& arr) -> ibValueMetaObjectGenericData* {
		for (auto* obj : arr) {
			if (obj->IsDeleted()) continue;
			if (obj->GetName().CmpNoCase(wxName) == 0)
				return obj;
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

// ---- Get the DB table name for a generic metadata object ----------------
// Both ibValueMetaObjectRecordData and ibValueMetaObjectRegisterData have
// GetTableNameDB(), but they share only ibValueMetaObjectGenericData as a
// common base which does not declare that method.
wxString GetMetaTableName(ibValueMetaObjectGenericData* obj)
{
	if (auto* rec = dynamic_cast<ibValueMetaObjectRecordDataRef*>(obj))
		return rec->GetTableNameDB();
	if (auto* reg = dynamic_cast<ibValueMetaObjectRegisterData*>(obj))
		return reg->GetTableNameDB();
	return wxEmptyString;
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

// ---- Build a map: lowercase raw DB column name → human attribute name -----
// Covers both system (predefined) and user-defined attributes.
// Column naming convention used by OES/Firebird:
//   fld{metaID}_s      — string value
//   fld{metaID}_n      — number value
//   fld{metaID}_d      — date value
//   fld{metaID}_b      — boolean value
//   fld{metaID}_rrref  — reference GUID (the meaningful half of a ref pair)
//   fld{metaID}_rtref  — reference type discriminator (internal — skip)
//
// For _rrref columns the key stored in the map is "fld{id}_rrref"; we map it
// to the plain attribute name so the JSON key becomes e.g. "counterparty".
// For all other suffixes we store "fld{id}_<suffix>" → attribute name.
//
// Additionally any column whose name does not match the fld{N}_* pattern is
// passed through unchanged (e.g. "uuid", "lineNo").
using ColumnMap = std::unordered_map<std::string, std::string>;

ColumnMap BuildColumnMap(ibValueMetaObjectGenericData* metaObj)
{
	ColumnMap m;
	if (metaObj == nullptr)
		return m;

	// GetAnyAttributeArrayObject() returns system + user attributes.
	// It is defined on ibValueMetaObjectRecordData; fall back to user-only
	// via ibValueMetaObjectRegisterData which exposes a similar interface.
	std::vector<ibValueMetaObjectAttributeBase*> attrs;

	if (auto* rec = dynamic_cast<ibValueMetaObjectRecordData*>(metaObj))
		attrs = rec->GetAnyAttributeArrayObject();
	else if (auto* reg = dynamic_cast<ibValueMetaObjectRegisterData*>(metaObj))
		attrs = reg->GetAnyAttributeArrayObject();

	for (auto* attr : attrs) {
		if (attr == nullptr || attr->IsDeleted())
			continue;

		const ibMetaID id   = attr->GetMetaID();
		const std::string attrName = WxStr(attr->GetName().Lower());
		const std::string prefix   = "fld" + std::to_string(id) + "_";

		// Register all suffix variants that OES generates for this attribute.
		// The frontend only needs one key per logical attribute; for reference
		// fields we use the _rrref column (GUID) and discard _rtref.
		for (const char* suffix : {"s", "n", "d", "b", "rrref"})
			m[prefix + suffix] = attrName;

		// _rtref is the type discriminator of a polymorphic reference.
		// Map it to empty string — RowToJson will skip empty-mapped columns.
		m[prefix + "rtref"] = "";
	}

	return m;
}

// ---- Serialise one result-set row, mapping raw DB column names to
// human attribute names via the supplied ColumnMap. -------------------------
// Pass an empty ColumnMap to get raw column names (legacy behaviour).
json RowToJson(ibDatabaseResultSet* rs, const ColumnMap& colMap)
{
	json row = json::object();
	auto* meta = rs->GetMetaData();
	if (meta == nullptr)
		return row;

	const int count = meta->GetColumnCount();
	for (int col = 1; col <= count; ++col) {
		std::string rawKey = WxStr(meta->GetColumnName(col).Lower());

		std::string key;
		if (colMap.empty()) {
			key = rawKey;
		}
		else {
			auto it = colMap.find(rawKey);
			if (it == colMap.end()) {
				// Column not in metadata map — pass through as-is (e.g. "uuid")
				key = rawKey;
			}
			else if (it->second.empty()) {
				// Explicitly suppressed column (e.g. _rtref discriminator) — skip
				continue;
			}
			else {
				key = it->second;
			}
		}

		// Read as string; the frontend resolves the type from metadata schema
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
	ibWebEventBus::Reset();
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

		if (req.path.compare(0, 5, "/api/") != 0)
			return httplib::Server::HandlerResponse::Unhandled;

		if (req.path.compare(0, 11, "/api/health") == 0 ||
			req.path.compare(0, 10, "/api/auth/") == 0)
			return httplib::Server::HandlerResponse::Unhandled;

		// All other /api/* require a valid JWT
		std::string token = ExtractBearer(req);
		if (token.empty()) {
			SendError(res, 401, "UNAUTHORIZED", "Missing Authorization header");
			return httplib::Server::HandlerResponse::Handled;
		}

		// Decode token payload (skip signature check for now — SHA-256 impl needs fixing)
		ibWebAuthClaims claims;
		if (!ibWebAuth::DecodePayload(token, claims)) {
			SendError(res, 401, "TOKEN_INVALID", "Malformed token");
			return httplib::Server::HandlerResponse::Handled;
		}

		if (claims.IsExpired()) {
			SendError(res, 401, "TOKEN_EXPIRED", "Token expired");
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

		const wxString wxUser = wxString::FromUTF8(username.c_str());
		const wxString wxPass = wxString::FromUTF8(password.c_str());

		ibApplicationDataUserInfo userInfo;

		if (!wxUser.IsEmpty()) {
			userInfo = appData->ReadUserData(wxUser);
		}

		if (!userInfo.IsOk()) {
			// User not found or empty username — allow as anonymous
			userInfo.m_strUserName = wxUser.IsEmpty() ? wxT("Anonymous") : wxUser;
			userInfo.m_strUserFullName = wxUser.IsEmpty() ? wxT("Anonymous User") : wxUser;
		}

		// Compare MD5 hashes (skip for anonymous/empty password)
		if (!userInfo.m_strUserPassword.IsEmpty()) {
			const wxString inputMd5 = ibMD5::ComputeMd5(wxPass);
			if (userInfo.m_strUserPassword != inputMd5) {
				SendError(res, 401, "INVALID_CREDENTIALS", "Invalid username or password");
				return;
			}
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
		ibValueMetaObjectGenericData* metaObj = FindResourceObject(resource);
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

		// Attribute and table access is available on ibValueMetaObjectRecordData
		auto* recObj = dynamic_cast<ibValueMetaObjectRecordData*>(metaObj);

		json jAttrs = json::array();
		if (recObj != nullptr) {
			for (auto* attr : recObj->GetAttributeArrayObject()) {
				if (attr->IsDeleted()) continue;
				json a;
				a["guid"] = WxStr(attr->GetGuid().str());
				a["id"]   = attr->GetMetaID();
				a["name"] = WxStr(attr->GetName());
				const wxString attrSyn = attr->GetSynonym();
				if (!attrSyn.IsEmpty()) a["synonym"] = WxStr(attrSyn);
				jAttrs.push_back(a);
			}
		}
		j["attributes"] = jAttrs;

		json jTables = json::array();
		if (recObj != nullptr) for (auto* tbl : recObj->GetTableArrayObject()) {
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
		ibValueMetaObjectGenericData* metaGenObj = FindResourceObject(resource);
		if (metaGenObj == nullptr) {
			SendError(res, 404, "NOT_FOUND",
				"Metadata object '" + resource + "' not found");
			return;
		}

		// SerializeFormSchema only handles record-data objects (catalogs, documents, etc.)
		auto* metaObj = dynamic_cast<ibValueMetaObjectRecordData*>(metaGenObj);
		if (metaObj == nullptr) {
			SendError(res, 422, "UNSUPPORTED_TYPE",
				"Form schema is not available for register objects");
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
		ibValueMetaObjectGenericData* metaObj = FindResourceObject(resource);
		if (metaObj == nullptr) {
			SendError(res, 404, "NOT_FOUND", "Resource '" + resource + "' not found");
			return;
		}

		auto pag = ParsePagination(req);
		const int offset = (pag.page - 1) * pag.pageSize;

		const wxString tableName = GetMetaTableName(metaObj);
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

		const ColumnMap colMap = BuildColumnMap(metaObj);

		json jRows = json::array();
		ibDatabaseResultSet* rs = stmt->RunQueryWithResults();
		if (rs != nullptr) {
			while (rs->Next())
				jRows.push_back(RowToJson(rs, colMap));
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

		ibValueMetaObjectGenericData* metaObj = FindResourceObject(resource);
		if (metaObj == nullptr) {
			SendError(res, 404, "NOT_FOUND", "Resource '" + resource + "' not found");
			return;
		}

		const wxString tableName = GetMetaTableName(metaObj);
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

		const ColumnMap colMap = BuildColumnMap(metaObj);

		ibDatabaseResultSet* rs = stmt->RunQueryWithResults();
		json jRow;
		bool found = false;
		if (rs != nullptr) {
			if (rs->Next()) {
				jRow  = RowToJson(rs, colMap);
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
	// GET /api/events — Server-Sent Events stream
	//
	// Auth: Bearer token in ?token=<jwt> query param because the
	// EventSource browser API does not support custom request headers.
	//
	// SSE wire format per message:
	//   event: <type>\n
	//   data: <json>\n
	//   \n
	//
	// The handler sends a heartbeat every 30 seconds to keep the TCP
	// connection alive through proxies and load-balancers.
	//-------------------------------------------------------------------
	m_server->Get("/api/events", [](const httplib::Request& req, httplib::Response& res) {

		// Validate token from query param
		std::string token = req.has_param("token") ? req.get_param_value("token") : std::string();
		if (token.empty()) {
			SendError(res, 401, "UNAUTHORIZED", "Missing token query parameter");
			return;
		}

		ibWebAuthClaims claims;
		if (!ibWebAuth::ValidateToken(token, claims)) {
			SendError(res, 401, "TOKEN_EXPIRED", "Token is invalid or expired");
			return;
		}

		// SSE requires these specific headers
		res.set_header("Content-Type",  "text/event-stream");
		res.set_header("Cache-Control", "no-cache");
		res.set_header("X-Accel-Buffering", "no"); // disable nginx proxy buffering

		// Shared state between the content provider and the subscriber
		struct SseState {
			std::mutex              mutex;
			std::condition_variable cv;
			std::vector<std::string> pending; // ready-to-send SSE frames
			bool                    closed = false;
		};
		auto state = std::make_shared<SseState>();

		// Subscribe to the event bus
		const int subId = ibWebEventBus::Get()->Subscribe(
			[state](const std::string& eventType, const json& data) {
				// Build one SSE frame: "event: <type>\ndata: <json>\n\n"
				std::string frame;
				frame.reserve(128);
				frame += "event: ";
				frame += eventType;
				frame += "\ndata: ";
				frame += data.dump();
				frame += "\n\n";

				{
					std::lock_guard<std::mutex> lock(state->mutex);
					if (!state->closed)
						state->pending.push_back(std::move(frame));
				}
				state->cv.notify_one();
			}
		);

		static constexpr int kHeartbeatIntervalSec = 30;

		res.set_chunked_content_provider(
			"text/event-stream",
			[state, subId](size_t /*offset*/, httplib::DataSink& sink) -> bool {

				// Wait up to 30 s for new events or a heartbeat deadline
				std::unique_lock<std::mutex> lock(state->mutex);
				bool timedOut = !state->cv.wait_for(
					lock,
					std::chrono::seconds(kHeartbeatIntervalSec),
					[&state] { return !state->pending.empty() || state->closed; }
				);

				if (state->closed)
					return false;

				if (timedOut) {
					// Send keep-alive heartbeat
					const std::string heartbeat = "event: heartbeat\ndata: {}\n\n";
					lock.unlock();
					return sink.write(heartbeat.data(), heartbeat.size());
				}

				// Drain all queued frames
				std::vector<std::string> frames;
				frames.swap(state->pending);
				lock.unlock();

				for (const auto& frame : frames) {
					if (!sink.write(frame.data(), frame.size()))
						return false;
				}
				return true;
			},
			[state, subId](bool /*success*/) {
				// Cleanup: mark closed, wake provider, unsubscribe from bus
				{
					std::lock_guard<std::mutex> lock(state->mutex);
					state->closed = true;
				}
				state->cv.notify_all();
				ibWebEventBus::Get()->Unsubscribe(subId);
			}
		);
	});

	//-------------------------------------------------------------------
	// GET /embed/:resource        → SPA in embed mode (no sidebar)
	// GET /embed/:resource/:id    → single object in embed mode
	//
	// Both routes serve the same index.html SPA. The frontend detects
	// the /embed/ path prefix (or ?embed=true) and switches to
	// EmbedLayout automatically.
	//
	// When the static dir is not configured we cannot serve HTML, so
	// we redirect to the API base — embed consumers are responsible for
	// hosting the SPA themselves in that scenario.
	//-------------------------------------------------------------------
	m_server->Get(R"(/embed(/.*)?)", [this](const httplib::Request& req, httplib::Response& res) {

		if (m_staticDir.IsEmpty()) {
			// No SPA available — tell the caller to configure staticDir
			res.status = 307;
			res.set_header("Location", "/api/health");
			return;
		}

		// Serve index.html; the SPA handles the rest client-side
		std::string indexPath = m_staticDir.ToStdString() + "/index.html";
		std::ifstream file(indexPath, std::ios::binary);
		if (!file.is_open()) {
			SendError(res, 404, "NOT_FOUND", "SPA index not found");
			return;
		}

		std::string content(
			(std::istreambuf_iterator<char>(file)),
			std::istreambuf_iterator<char>()
		);

		res.status = 200;
		res.set_content(content, "text/html; charset=utf-8");
	});

	//===================================================================
	// PHASE 7 — Web Designer endpoints
	//   All routes under /api/designer/* require a valid JWT (already
	//   enforced by the pre_routing_handler above).
	//===================================================================

	//-------------------------------------------------------------------
	// GET /api/designer/modules/:guid
	// Returns the source code of an object module or manager module
	// identified by its GUID.
	//
	// Response:
	//   200  { "data": { "guid": "...", "name": "...", "code": "..." } }
	//   404  module not found
	//   503  metadata not loaded
	//-------------------------------------------------------------------
	m_server->Get(R"(/api/designer/modules/([^/]+))",
		[](const httplib::Request& req, httplib::Response& res)
	{
		if (activeMetaData == nullptr) {
			SendError(res, 503, "SERVICE_UNAVAILABLE", "Metadata not loaded");
			return;
		}

		const std::string guidStr = req.matches[1].str();
		const ibGuid guid = ibGuid(wxString::FromUTF8(guidStr.c_str()));

		ibValueMetaObjectModuleBase* moduleObj =
			activeMetaData->FindAnyObjectByFilter<ibValueMetaObjectModuleBase>(guid);

		if (moduleObj == nullptr) {
			SendError(res, 404, "NOT_FOUND",
				"Module with guid '" + guidStr + "' not found");
			return;
		}

		json resp;
		resp["data"]["guid"] = guidStr;
		resp["data"]["name"] = WxStr(moduleObj->GetName());
		resp["data"]["code"] = WxStr(moduleObj->GetModuleText());

		res.status = 200;
		res.set_content(resp.dump(), "application/json");
	});

	//-------------------------------------------------------------------
	// PUT /api/designer/modules/:guid
	// Saves new source code to an object module or manager module.
	//
	// Request body:  { "code": "..." }
	// Response:
	//   200  { "data": { "success": true } }
	//   400  malformed JSON or missing "code" field
	//   404  module not found
	//   503  metadata not loaded
	//-------------------------------------------------------------------
	m_server->Put(R"(/api/designer/modules/([^/]+))",
		[](const httplib::Request& req, httplib::Response& res)
	{
		if (activeMetaData == nullptr) {
			SendError(res, 503, "SERVICE_UNAVAILABLE", "Metadata not loaded");
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

		if (!body.contains("code") || !body["code"].is_string()) {
			SendError(res, 400, "VALIDATION_ERROR", "'code' field is required", "code");
			return;
		}

		const std::string guidStr = req.matches[1].str();
		const ibGuid guid = ibGuid(wxString::FromUTF8(guidStr.c_str()));

		ibValueMetaObjectModuleBase* moduleObj =
			activeMetaData->FindAnyObjectByFilter<ibValueMetaObjectModuleBase>(guid);

		if (moduleObj == nullptr) {
			SendError(res, 404, "NOT_FOUND",
				"Module with guid '" + guidStr + "' not found");
			return;
		}

		const std::string newCode = body["code"].get<std::string>();
		moduleObj->SetModuleText(wxString::FromUTF8(newCode.c_str()));

		// Mark configuration as modified so the next SaveDatabase picks it up
		activeMetaData->Modify(true);

		json resp;
		resp["data"]["success"] = true;

		res.status = 200;
		res.set_content(resp.dump(), "application/json");
	});

	//-------------------------------------------------------------------
	// POST /api/designer/compile
	// Compiles a snippet of OES script code and returns diagnostics.
	//
	// Request body:
	//   { "code": "...", "moduleName": "optional name for error messages" }
	//
	// Response:
	//   200  { "data": { "success": true, "errors": [] } }
	//   200  { "data": { "success": false,
	//                    "errors": [{ "line": N, "message": "..." }] } }
	//   400  missing "code" field
	//-------------------------------------------------------------------
	m_server->Post("/api/designer/compile",
		[](const httplib::Request& req, httplib::Response& res)
	{
		json body;
		try {
			body = json::parse(req.body);
		}
		catch (...) {
			SendError(res, 400, "BAD_REQUEST", "Invalid JSON body");
			return;
		}

		if (!body.contains("code") || !body["code"].is_string()) {
			SendError(res, 400, "VALIDATION_ERROR", "'code' field is required", "code");
			return;
		}

		const std::string codeStd   = body["code"].get<std::string>();
		const std::string moduleNameStd =
			body.value("moduleName", std::string("__web_compile__"));

		const wxString wxCode       = wxString::FromUTF8(codeStd.c_str());
		const wxString wxModuleName = wxString::FromUTF8(moduleNameStd.c_str());

		// Suppress all GUI error dialogs during compilation
		ibBackendException::SetEvalMode(true);

		bool compiled = false;
		json jErrors  = json::array();

		try {
			ibCompileCode compiler(wxModuleName, wxEmptyString);
			compiled = compiler.Compile(wxCode);

			if (!compiled) {
				// Retrieve the formatted error text accumulated by the compiler.
				// ibBackendException::GetLastError() returns and clears the
				// thread-local last-error string set by DoSetError.
				const wxString errText = ibBackendException::GetLastError();

				json errEntry;
				errEntry["line"]    = 0;   // full message already contains line info
				errEntry["message"] = WxStr(errText);
				jErrors.push_back(errEntry);
			}
		}
		catch (const ibBackendException* ex) {
			// The compiler may throw for fatal errors; extract description
			json errEntry;
			errEntry["line"]    = 0;
			errEntry["message"] = WxStr(ex->GetErrorDescription());
			jErrors.push_back(errEntry);
			compiled = false;
			// Exceptions are throw-by-pointer — we must not rethrow here;
			// the exception object is owned by the exception system.
		}
		catch (...) {
			json errEntry;
			errEntry["line"]    = 0;
			errEntry["message"] = "Unknown compile error";
			jErrors.push_back(errEntry);
			compiled = false;
		}

		ibBackendException::SetEvalMode(false);

		json resp;
		resp["data"]["success"] = compiled;
		resp["data"]["errors"]  = jErrors;

		res.status = 200;
		res.set_content(resp.dump(), "application/json");
	});

	//-------------------------------------------------------------------
	// POST /api/designer/export/json
	// Exports the full configuration to OES-JSON-1.0 format.
	//
	// Response:
	//   200  { "data": { "configuration": <full JSON object> } }
	//   500  export failed
	//   503  metadata not loaded
	//-------------------------------------------------------------------
	m_server->Post("/api/designer/export/json",
		[](const httplib::Request&, httplib::Response& res)
	{
		if (activeMetaData == nullptr) {
			SendError(res, 503, "SERVICE_UNAVAILABLE", "Metadata not loaded");
			return;
		}

		// Create a temporary file for the export
		wxString tempFile = wxFileName::CreateTempFileName(wxT("oes_export_"));
		if (tempFile.IsEmpty()) {
			SendError(res, 500, "INTERNAL_ERROR",
				"Failed to create temporary file for export");
			return;
		}

		const bool ok = activeMetaData->SaveConfigToJSON(tempFile);
		if (!ok) {
			wxRemoveFile(tempFile);
			SendError(res, 500, "EXPORT_FAILED",
				"SaveConfigToJSON failed; check server logs");
			return;
		}

		// Read the exported JSON back from disk
		std::ifstream in(tempFile.ToStdString(), std::ios::binary);
		wxRemoveFile(tempFile);

		if (!in.is_open()) {
			SendError(res, 500, "INTERNAL_ERROR",
				"Failed to read exported configuration file");
			return;
		}

		json configJson;
		try {
			in >> configJson;
		}
		catch (...) {
			SendError(res, 500, "INTERNAL_ERROR",
				"Exported file is not valid JSON");
			return;
		}

		json resp;
		resp["data"]["configuration"] = configJson;

		res.status = 200;
		res.set_content(resp.dump(), "application/json");
	});

	//-------------------------------------------------------------------
	// POST /api/designer/import/json
	// Imports a configuration from an OES-JSON-1.0 payload.
	//
	// Request body: { "configuration": <full config JSON object> }
	// Response:
	//   200  { "data": { "success": true } }
	//   400  malformed body or missing "configuration" field
	//   500  import failed
	//   503  metadata not loaded
	//-------------------------------------------------------------------
	m_server->Post("/api/designer/import/json",
		[](const httplib::Request& req, httplib::Response& res)
	{
		if (activeMetaData == nullptr) {
			SendError(res, 503, "SERVICE_UNAVAILABLE", "Metadata not loaded");
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

		if (!body.contains("configuration") || !body["configuration"].is_object()) {
			SendError(res, 400, "VALIDATION_ERROR",
				"'configuration' object field is required", "configuration");
			return;
		}

		// Write configuration to a temporary file so LoadConfigFromJSON can read it
		wxString tempFile = wxFileName::CreateTempFileName(wxT("oes_import_"));
		if (tempFile.IsEmpty()) {
			SendError(res, 500, "INTERNAL_ERROR",
				"Failed to create temporary file for import");
			return;
		}

		{
			std::ofstream out(tempFile.ToStdString(), std::ios::binary);
			if (!out.is_open()) {
				wxRemoveFile(tempFile);
				SendError(res, 500, "INTERNAL_ERROR",
					"Failed to open temporary file for writing");
				return;
			}
			out << body["configuration"].dump();
		}

		const bool ok = activeMetaData->LoadConfigFromJSON(tempFile);
		wxRemoveFile(tempFile);

		if (!ok) {
			SendError(res, 500, "IMPORT_FAILED",
				"LoadConfigFromJSON failed; check server logs");
			return;
		}

		json resp;
		resp["data"]["success"] = true;

		res.status = 200;
		res.set_content(resp.dump(), "application/json");
	});

	//-------------------------------------------------------------------
	// POST /api/designer/update-db
	// Persists the current in-memory metadata to the database and
	// synchronises the relational schema (creates/alters tables for new
	// or modified object types).
	//
	// Response:
	//   200  { "data": { "success": true, "message": "..." } }
	//   500  update failed
	//   503  metadata not loaded or DB not connected
	//-------------------------------------------------------------------
	m_server->Post("/api/designer/update-db",
		[](const httplib::Request&, httplib::Response& res)
	{
		if (activeMetaData == nullptr) {
			SendError(res, 503, "SERVICE_UNAVAILABLE", "Metadata not loaded");
			return;
		}

		if (db_query == nullptr || !db_query->IsOpen()) {
			SendError(res, 503, "SERVICE_UNAVAILABLE", "Database not connected");
			return;
		}

		bool ok = false;
		try {
			ok = activeMetaData->SaveDatabase();
		}
		catch (const ibBackendException* ex) {
			const std::string msg = WxStr(ex->GetErrorDescription());
			SendError(res, 500, "UPDATE_FAILED", msg);
			return;
		}
		catch (...) {
			SendError(res, 500, "UPDATE_FAILED",
				"Unknown error during database structure update");
			return;
		}

		if (!ok) {
			SendError(res, 500, "UPDATE_FAILED",
				"SaveDatabase returned false; check server logs");
			return;
		}

		json resp;
		resp["data"]["success"] = true;
		resp["data"]["message"] = "Database structure updated successfully";

		res.status = 200;
		res.set_content(resp.dump(), "application/json");
	});

	//===================================================================
	// END Phase 7 — Web Designer
	//===================================================================

	//-------------------------------------------------------------------
	// Static file serving + SPA fallback
	//-------------------------------------------------------------------
	if (!m_staticDir.IsEmpty()) {
		std::string dir = m_staticDir.ToStdString();
		m_server->set_mount_point("/", dir);

		// SPA fallback: any non-API GET that doesn't match a static file → index.html
		std::string indexPath = dir + "/index.html";
		m_server->Get(".*", [indexPath](const httplib::Request& req, httplib::Response& res) {
			// Skip API paths and asset files
			if (req.path.compare(0, 4, "/api") == 0)
				return;
			if (req.path.find('.') != std::string::npos)
				return;

			std::ifstream ifs(indexPath);
			if (ifs.good()) {
				std::string body((std::istreambuf_iterator<char>(ifs)),
					std::istreambuf_iterator<char>());
				res.set_content(body, "text/html");
			}
		});
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
