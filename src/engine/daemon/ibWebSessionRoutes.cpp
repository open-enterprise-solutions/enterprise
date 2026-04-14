////////////////////////////////////////////////////////////////////////////
//	Author		: Tetracode Dev
//	Description : Session API routes — registered by daemon after
//	              ibWebServer::Initialize(). These need frontend.dll
//	              (ibValueForm, ibWebVisualHost) which only daemon links.
////////////////////////////////////////////////////////////////////////////

#include "ibWebSessionManager.h"
#include "backend/webServer/ibWebAuth.h"

#include <httplib.h>
#include <json.hpp>

using json = nlohmann::json;

namespace {

std::string ExtractBearer(const httplib::Request& req)
{
	std::string auth = req.get_header_value("Authorization");
	if (auth.size() > 7 && auth.compare(0, 7, "Bearer ") == 0)
		return auth.substr(7);
	return {};
}

void SendError(httplib::Response& res, int status,
	const std::string& code, const std::string& message)
{
	json body;
	body["error"]["code"] = code;
	body["error"]["message"] = message;
	res.status = status;
	res.set_content(body.dump(), "application/json");
}

} // anonymous namespace

void RegisterSessionRoutes(httplib::Server* svr)
{
	if (svr == nullptr)
		return;

	// POST /api/session/open-form
	svr->Post("/api/session/open-form", [](const httplib::Request& req, httplib::Response& res) {
		if (webSessionManager == nullptr) {
			SendError(res, 503, "SERVICE_UNAVAILABLE", "Session manager not initialized");
			return;
		}

		json body;
		try { body = json::parse(req.body); }
		catch (...) { SendError(res, 400, "BAD_REQUEST", "Invalid JSON"); return; }

		std::string metaType = body.value("metaType", std::string());
		std::string metaName = body.value("metaName", std::string());
		std::string objectId = body.value("objectId", std::string());

		if (metaName.empty()) {
			SendError(res, 422, "VALIDATION_ERROR", "metaName is required");
			return;
		}

		std::string token = ExtractBearer(req);
		ibWebAuthClaims claims;
		ibWebAuth::DecodePayload(token, claims);

		std::string sessionId = webSessionManager->CreateSession(claims.sub);
		json result = webSessionManager->OpenForm(sessionId, metaType, metaName, objectId);
		result["sessionId"] = sessionId;

		res.set_content(result.dump(), "application/json");
	});

	// POST /api/session/event
	svr->Post("/api/session/event", [](const httplib::Request& req, httplib::Response& res) {
		if (webSessionManager == nullptr) {
			SendError(res, 503, "SERVICE_UNAVAILABLE", "Session manager not initialized");
			return;
		}

		json body;
		try { body = json::parse(req.body); }
		catch (...) { SendError(res, 400, "BAD_REQUEST", "Invalid JSON"); return; }

		std::string sessionId = body.value("sessionId", std::string());
		int controlId = body.value("controlId", 0);
		std::string eventName = body.value("event", std::string());
		json eventData = body.value("data", json::object());

		json result = webSessionManager->HandleEvent(sessionId, controlId, eventName, eventData);
		res.set_content(result.dump(), "application/json");
	});

	// GET /api/session/layout
	svr->Get("/api/session/layout", [](const httplib::Request& req, httplib::Response& res) {
		if (webSessionManager == nullptr) {
			SendError(res, 503, "SERVICE_UNAVAILABLE", "Session manager not initialized");
			return;
		}

		std::string sessionId = req.get_param_value("sessionId");
		json result = webSessionManager->GetFormLayout(sessionId);
		res.set_content(result.dump(), "application/json");
	});

	// POST /api/session/close
	svr->Post("/api/session/close", [](const httplib::Request& req, httplib::Response& res) {
		if (webSessionManager == nullptr) {
			SendError(res, 503, "SERVICE_UNAVAILABLE", "Session manager not initialized");
			return;
		}

		json body;
		try { body = json::parse(req.body); }
		catch (...) { SendError(res, 400, "BAD_REQUEST", "Invalid JSON"); return; }

		std::string sessionId = body.value("sessionId", std::string());
		json result = webSessionManager->CloseForm(sessionId);
		res.set_content(result.dump(), "application/json");
	});
}
