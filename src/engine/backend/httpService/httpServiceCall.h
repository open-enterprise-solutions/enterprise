#ifndef __HTTP_SERVICE_CALL_H__
#define __HTTP_SERVICE_CALL_H__

// A REQUEST TO AN HTTP SERVICE OF THE APPLICATION, ANSWERED — the base's half of it. The web server is the
// transport: a service answers at a web address of the base (core/net/servedAddress.h) of the kind HTTPService, which
// this file registers (SERVED_KIND_REGISTER) — the entry names the service by its object, and its path is where it is
// asked (es/<RootURL>, unless the administrator gave another). What is published is a choice: a declared service
// answers nothing until it is registered there.
//
//   1. who is asking: `Authorization: Basic`, checked as a login checks it — before any session is made, so a
//      caller not let in costs none; a base without users lets it in as it lets in anybody;
//   2. a session of its own (ibSessionKind::HttpService), that user installed in it;
//   3. in that session's application, the service by its object, the template by the path, the method by the
//      request's (metaCollection/metaHttpServiceObject.h);
//   4. the handler, a function of the service module, given an HTTPServiceRequest and returning an
//      HTTPServiceResponse (system/value/valueHttp.h);
//   5. the session ended.
//
// ⭐ IT ALWAYS ANSWERS — what goes wrong is a status, as a web server says it: 401 (who is asking is not known),
// 404 (no such service in the session's application, or no template there), 405 (the template has no such method,
// Allow saying which it has), 500 (the handler raised, or returned no HTTPServiceResponse), 503 (no session could be
// made). A 500's reason goes to the technology journal, never to the caller: it is the application's code talking,
// and whoever called is somebody else's system.

#include "backend/backend.h"
#include "core/guid.h"
#include "core/net/httpServer.h"   // ibHttpRequest, ibHttpResponse — what came, and what goes back

class ibApplicationInstance;

class BACKEND_API ibHttpServiceCall {
public:

	// `object` — the service's guid, its entry's object; `request.m_path` — the whole path, decoded
	// (/trade/es/desk/Transfer/12/reset); `prefix` — the address up to and with the entry's path (/trade/es/desk): the
	// handler's BaseURL, and what the path continues.
	static ibHttpResponse Answer(ibApplicationInstance* applicationInstance, const ibGuid& object,
		const ibHttpRequest& request, const ibString& prefix);
};

#endif // !__HTTP_SERVICE_CALL_H__
