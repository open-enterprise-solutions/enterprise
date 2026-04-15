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

	// Load form layout from blob if available
	if (formMeta != nullptr) {
		wxMemoryBuffer formData = formMeta->GetFormData();
		if (formData.GetDataLen() > 0) {
			session->form->LoadForm(formData);
		}
	}

	// Build form (creates control tree from metadata if no blob)
	if (session->form->GetChildCount() == 0) {
		session->form->BuildForm(defaultFormType);
	}

	// Initialize form module (compile script, execute)
	session->form->InitializeFormModule();

	// Create web visual host — builds JSON proxy tree
	session->host = new ibWebVisualHost(session->form);
	session->host->CreateWebHost();

	// Return layout
	json result;
	result["sessionId"] = sessionId;
	result["layout"] = session->host->GetFormLayout();
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
