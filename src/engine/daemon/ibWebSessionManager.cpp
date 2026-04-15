////////////////////////////////////////////////////////////////////////////
//	Author		: Tetracode Dev
//	Description : Web session manager implementation
////////////////////////////////////////////////////////////////////////////

#include "ibWebSessionManager.h"

#include "appData.h"
#include "metadataConfiguration.h"
#include "metaCollection/metaObject.h"
#include "metaCollection/partial/commonObject.h"
#include "backend_form.h"
#include "databaseLayer/databaseLayer.h"

#include "frontend/visualView/webHost/ibWebVisualHost.h"
#include "frontend/visualView/ctrl/form.h"

#include <random>
#include <sstream>
#include <iomanip>

ibWebSessionManager* ibWebSessionManager::ms_instance = nullptr;

//***********************************************************************
//*  Build a JSON form layout directly from metadata (no wxWidgets)    *
//***********************************************************************

namespace {

json BuildLayoutFromMetadata(ibValueMetaObject* metaObj, const std::string& /*metaType*/)
{
	json root;
	try {

	root["id"] = 1;
	root["type"] = "form";
	root["name"] = metaObj->GetName().ToStdString();

	json props;
	wxString synonym = metaObj->GetSynonym();
	props["caption"] = synonym.IsEmpty()
		? metaObj->GetName().ToStdString()
		: synonym.ToStdString();
	root["props"] = props;

	json children = json::array();
	int nextId = 100;

	auto* recordObj = dynamic_cast<ibValueMetaObjectRecordData*>(metaObj);
	if (recordObj == nullptr) {
		root["children"] = children;
		return root;
	}

	// Attributes (system + user)
	auto attrs = recordObj->GetAttributeArrayObject();
	for (auto* attr : attrs) {
		if (attr == nullptr || attr->IsDeleted()) continue;
		json field;
		field["id"] = nextId++;
		field["type"] = "textbox";
		field["name"] = attr->GetName().ToStdString();
		json fprops;
		wxString attrSyn = attr->GetSynonym();
		fprops["label"] = attrSyn.IsEmpty()
			? attr->GetName().ToStdString()
			: attrSyn.ToStdString();
		field["props"] = fprops;
		children.push_back(field);
	}

	// Tabular sections
	auto tables = recordObj->GetTableArrayObject();
	for (auto* table : tables) {
		if (table == nullptr || table->IsDeleted()) continue;
		json tableNode;
		tableNode["id"] = nextId++;
		tableNode["type"] = "tablebox";
		tableNode["name"] = table->GetName().ToStdString();
		json tprops;
		wxString tSyn = table->GetSynonym();
		tprops["caption"] = tSyn.IsEmpty()
			? table->GetName().ToStdString()
			: tSyn.ToStdString();
		tableNode["props"] = tprops;

		json cols = json::array();
		auto tableAttrs = table->GetAttributeArrayObject();
		for (auto* tattr : tableAttrs) {
			if (tattr == nullptr || tattr->IsDeleted()) continue;
			json col;
			col["id"] = nextId++;
			col["type"] = "statictext";
			col["name"] = tattr->GetName().ToStdString();
			json cprops;
			wxString cSyn = tattr->GetSynonym();
			cprops["caption"] = cSyn.IsEmpty()
				? tattr->GetName().ToStdString()
				: cSyn.ToStdString();
			col["props"] = cprops;
			cols.push_back(col);
		}
		tableNode["children"] = cols;
		children.push_back(tableNode);
	}

	root["children"] = children;

	} catch (...) {
		root["children"] = json::array();
	}

	return root;
}

} // anonymous namespace

//***********************************************************************
//*                      Helper: generate session ID                   *
//***********************************************************************

namespace {

std::string GenerateSessionId()
{
	std::random_device rd;
	std::mt19937_64 gen(rd());
	std::uniform_int_distribution<uint64_t> dist;

	std::ostringstream oss;
	oss << std::hex << std::setfill('0')
		<< std::setw(16) << dist(gen)
		<< std::setw(16) << dist(gen);
	return oss.str();
}

} // anonymous namespace

//***********************************************************************
//*                      Singleton                                     *
//***********************************************************************

ibWebSessionManager::ibWebSessionManager() {}

ibWebSessionManager::~ibWebSessionManager()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	for (auto& [id, session] : m_sessions) {
		delete session.host;
		// form is ref-counted, don't delete directly
	}
	m_sessions.clear();
}

ibWebSessionManager* ibWebSessionManager::Get() { return ms_instance; }

void ibWebSessionManager::Initialize()
{
	if (ms_instance == nullptr)
		ms_instance = new ibWebSessionManager();
}

void ibWebSessionManager::Destroy()
{
	delete ms_instance;
	ms_instance = nullptr;
}

//***********************************************************************
//*                      Session lifecycle                             *
//***********************************************************************

std::string ibWebSessionManager::CreateSession(const std::string& userGuid)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	ibWebSession session;
	session.id = GenerateSessionId();
	session.userGuid = userGuid;
	session.lastActive = std::chrono::steady_clock::now();

	// Clone DB connection for this session's thread
	if (db_query != nullptr)
		session.db = std::shared_ptr<ibDatabaseLayer>(db_query->Clone());

	std::string id = session.id;
	m_sessions[id] = std::move(session);
	return id;
}

void ibWebSessionManager::DestroySession(const std::string& sessionId)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	auto it = m_sessions.find(sessionId);
	if (it == m_sessions.end())
		return;

	delete it->second.host;
	it->second.host = nullptr;
	it->second.form = nullptr;

	m_sessions.erase(it);
}

ibWebSession* ibWebSessionManager::GetSession(const std::string& sessionId)
{
	auto it = m_sessions.find(sessionId);
	return (it != m_sessions.end()) ? &it->second : nullptr;
}

void ibWebSessionManager::TouchSession(const std::string& sessionId)
{
	auto it = m_sessions.find(sessionId);
	if (it != m_sessions.end())
		it->second.lastActive = std::chrono::steady_clock::now();
}

//***********************************************************************
//*                      Form operations                               *
//***********************************************************************

json ibWebSessionManager::OpenForm(const std::string& sessionId,
	const std::string& metaType, const std::string& metaName,
	const std::string& objectId)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	auto* session = GetSession(sessionId);
	if (session == nullptr)
		return json{{"error", "Session not found"}};

	TouchSession(sessionId);

	// Close existing form if open
	if (session->host != nullptr) {
		delete session->host;
		session->host = nullptr;
		session->form = nullptr;
	}

	// Find metadata object
	if (activeMetaData == nullptr)
		return json{{"error", "Metadata not loaded"}};

	auto* root = activeMetaData->GetCommonMetaObject();
	if (root == nullptr)
		return json{{"error", "No configuration"}};

	// Find the metadata form for the requested object
	// For now: find the object's default form
	const wxString wxName = wxString::FromUTF8(metaName.c_str());

	// Search through metadata to find the object
	auto allObjects = root->GetAnyArrayObject<ibValueMetaObject>();
	ibValueMetaObject* metaObj = nullptr;
	for (auto* obj : allObjects) {
		if (obj->IsDeleted()) continue;
		if (obj->GetName().CmpNoCase(wxName) == 0) {
			metaObj = obj;
			break;
		}
	}

	if (metaObj == nullptr)
		return json{{"error", "Object '" + metaName + "' not found"}};

	// Get default form from the metadata object
	const ibValueMetaObjectFormBase* formMeta = nullptr;
	auto* genericObj = dynamic_cast<ibValueMetaObjectGenericData*>(metaObj);
	if (genericObj != nullptr) {
		auto forms = genericObj->GetFormArrayObject();
		if (!forms.empty())
			formMeta = forms[0];
	}

	// Create form via backend_mainFrame
	ibBackendValueForm* backendForm = nullptr;

	if (formMeta != nullptr) {
		backendForm = ibBackendValueForm::CreateNewForm(formMeta, nullptr, nullptr);
	}
	else {
		// No form defined — create auto-generated form
		backendForm = ibBackendValueForm::CreateNewForm(nullptr, nullptr, nullptr);
	}

	if (backendForm == nullptr)
		return json{{"error", "Failed to create form"}};

	// Cast to ibValueForm (frontend class)
	session->form = dynamic_cast<ibValueForm*>(backendForm);
	if (session->form == nullptr)
		return json{{"error", "Form cast failed"}};

	// In service mode, building the full control tree via BuildForm/LoadForm
	// crashes because control factories create wxWidgets objects.
	// Instead, build a JSON layout directly from metadata attributes.
	json layout = BuildLayoutFromMetadata(metaObj, metaType);

	// Return layout
	json result;
	result["sessionId"] = sessionId;
	result["layout"] = layout;
	return result;
}

json ibWebSessionManager::GetFormLayout(const std::string& sessionId)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	auto* session = GetSession(sessionId);
	if (session == nullptr || session->host == nullptr)
		return json{{"error", "No form open"}};

	TouchSession(sessionId);
	return session->host->GetFormLayout();
}

json ibWebSessionManager::HandleEvent(const std::string& sessionId,
	int controlId, const std::string& eventName, const json& eventData)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	auto* session = GetSession(sessionId);
	if (session == nullptr || session->host == nullptr)
		return json{{"error", "No form open"}};

	TouchSession(sessionId);
	return session->host->HandleEvent(controlId, eventName, eventData);
}

json ibWebSessionManager::CloseForm(const std::string& sessionId)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	auto* session = GetSession(sessionId);
	if (session == nullptr)
		return json{{"error", "Session not found"}};

	delete session->host;
	session->host = nullptr;
	session->form = nullptr;

	return json{{"ok", true}};
}
