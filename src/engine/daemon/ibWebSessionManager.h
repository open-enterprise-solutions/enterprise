////////////////////////////////////////////////////////////////////////////
//	Author		: Tetracode Dev
//	Description : Web session manager — each user session holds forms
//	              in memory on the server. Forms are rendered as JSON,
//	              events are processed via ibProcUnit.
////////////////////////////////////////////////////////////////////////////

#ifndef _IB_WEB_SESSION_MANAGER_H__
#define _IB_WEB_SESSION_MANAGER_H__

#include "backend/backend.h"

#include <json.hpp>

#include <string>
#include <mutex>
#include <unordered_map>
#include <memory>
#include <chrono>

using json = nlohmann::json;

// Forward declarations
class ibValueForm;
class ibWebVisualHost;
class ibDatabaseLayer;
class ibValueMetaObjectFormBase;
class ibSourceDataObject;

///////////////////////////////////////////////////////////////////////////

struct ibWebSession {
	std::string id;
	std::string userGuid;
	std::shared_ptr<ibDatabaseLayer> db;      // Cloned DB connection
	std::chrono::steady_clock::time_point lastActive;

	// Currently open form (one per session for now)
	ibValueForm* form = nullptr;
	ibWebVisualHost* host = nullptr;
};

///////////////////////////////////////////////////////////////////////////

class BACKEND_API ibWebSessionManager {
public:

	static ibWebSessionManager* Get();
	static void Initialize();
	static void Destroy();

	// Session lifecycle
	std::string CreateSession(const std::string& userGuid);
	void DestroySession(const std::string& sessionId);
	ibWebSession* GetSession(const std::string& sessionId);
	void TouchSession(const std::string& sessionId);

	// Form operations
	json OpenForm(const std::string& sessionId,
		const std::string& metaType, const std::string& metaName,
		const std::string& objectId = "");

	json GetFormLayout(const std::string& sessionId);

	json HandleEvent(const std::string& sessionId,
		int controlId, const std::string& eventName,
		const json& eventData);

	json CloseForm(const std::string& sessionId);

private:

	ibWebSessionManager();
	~ibWebSessionManager();

	std::mutex m_mutex;
	std::unordered_map<std::string, ibWebSession> m_sessions;

	static ibWebSessionManager* ms_instance;
};

#define webSessionManager (ibWebSessionManager::Get())

#endif // _IB_WEB_SESSION_MANAGER_H__
