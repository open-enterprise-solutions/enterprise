#ifndef __HTTP_SERVICE_CALL_H__
#define __HTTP_SERVICE_CALL_H__

// A REQUEST TO AN HTTP SERVICE OF THE CONFIGURATION, ANSWERED — the base's half of it. The web server is the
// transport: it carries the request to the base the way it carries a client's call (ibClientHost, protocol method
// `http`), and this is what the base does with it.
//
//   1. a session of its own (ibSessionKind::HttpService), under the identity the request came with:
//      `Authorization: Basic` read as a login is — a base without users lets it in as it lets in anybody;
//   2. in that session's configuration, the service by its root, the template by the path, the method by the
//      request's (metaCollection/metaHttpServiceObject.h);
//   3. the handler, a function of the service module, given an HTTPServiceRequest and returning an
//      HTTPServiceResponse (system/value/valueHttp.h);
//   4. the session ended.
//
// ⭐ IT ALWAYS ANSWERS — what goes wrong is a status, as a web server says it: 401 (who is asking is not known),
// 404 (no service or template there), 405 (the template has no such method, Allow saying which it has), 500 (the
// handler raised, or returned no HTTPServiceResponse), 503 (no session could be made). A 500's reason goes to the
// technology journal, never to the caller: it is the configuration's code talking, and whoever called is somebody
// else's system.
//
// ⭐ THE SESSION BEFORE THE SERVICE. Which services and templates a configuration has is read in the session's
// own configuration (a session keeps the one it came in with), and it is told only to whoever is let in: a
// caller that is not is answered 401 whatever address it asked for.

#include "backend/backend.h"
#include "core/net/httpServer.h"   // ibHttpRequest, ibHttpResponse — what came, and what goes back

class ibApplicationInstance;

class BACKEND_API ibHttpServiceCall {
public:

	// `request.m_path` — the path after /hs: the service's root and the rest (/desk/Transfer/12/reset);
	// `prefix` — the address up to there (/trade/hs), what the handler's BaseURL is made of.
	static ibHttpResponse Answer(ibApplicationInstance* applicationInstance, const ibHttpRequest& request,
		const ibString& prefix);
};

#endif // !__HTTP_SERVICE_CALL_H__
