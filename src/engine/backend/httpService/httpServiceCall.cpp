////////////////////////////////////////////////////////////////////////////
//	Description : a request to an HTTP service of the configuration, answered
////////////////////////////////////////////////////////////////////////////

#include "httpServiceCall.h"

#include "backend/appData.h"
#include "backend/backend_exception.h"
#include "backend/logger/logger.h"                       // what went wrong in a handler, where an administrator reads it
#include "backend/metaCollection/metaHttpServiceObject.h"
#include "backend/metadataConfiguration.h"
#include "backend/moduleManager/moduleManager.h"         // ibValueModuleManagerRuntimeConfiguration::FindCommonModule
#include "backend/session/session.h"
#include "backend/session/sessionRegistry.h"             // CreateSession<T> — its body
#include "backend/system/value/valueHttp.h"              // HTTPServiceRequest, HTTPServiceResponse

#include "core/utils/base64.h"                           // the user and the password of Basic

namespace {

ibHttpResponse Status(int status)
{
	ibHttpResponse response;
	response.m_status = status;
	return response;
}

// WHO IS ASKING — `Authorization: Basic` (RFC 7617: base64 of the UTF-8 of "user:password"); nothing, or another
// scheme, is nobody.
void ReadCredentials(const ibHttpRequest& request, ibString& user, ibString& password)
{
	const std::string authorization = request.GetHeader("authorization");
	if (authorization.compare(0, 6, "Basic ") != 0)
		return;
	ibMemoryBuffer decoded;
	if (!ibBase64::Read(ibString::FromUTF8(authorization.substr(6)), decoded))
		return;
	const ibString pair = ibString::FromUTF8(static_cast<const char*>(decoded.GetData()), decoded.GetDataLen());
	const int colon = pair.Find(ibT(':'));
	if (colon == wxNOT_FOUND)
		return;
	user = pair.Left(colon);
	password = pair.Mid(colon + 1);
}

// The service by its root, the template by the rest of the path, the method by the request's, the handler called —
// on the session's worker, in the session's configuration.
ibHttpResponse Run(ibSession* session, const ibHttpRequest& request, const ibString& prefix)
{
	// /desk/Transfer/12 — the root, and the rest of the path after it.
	const ibString path = ibString::FromUTF8(request.m_path);
	const size_t from = path.length() > 0 && path[0] == ibT('/') ? 1 : 0;
	size_t slash = path.find(ibT('/'), from);
	if (slash == ibString::npos)
		slash = path.length();
	const ibString root = path.Mid(from, slash - from);
	const ibString rest = path.Mid(slash);

	const ibMetaDataConfigurationBase* const metaData = session->GetMetaData();
	if (metaData == nullptr || root.IsEmpty())
		return Status(404);

	const ibValueMetaObjectHttpService* service = nullptr;
	for (const ibValueMetaObject* candidate : metaData->GetAnyArrayObject(g_metaHttpServiceCLSID)) {
		if (!candidate->IsDeleted() && static_cast<const ibValueMetaObjectHttpService*>(candidate)->GetRootUrl() == root) {
			service = static_cast<const ibValueMetaObjectHttpService*>(candidate);
			break;
		}
	}
	if (service == nullptr)
		return Status(404);

	std::vector<std::pair<ibString, ibString>> parameters;
	const ibValueMetaObjectUrlTemplate* const urlTemplate = service->FindTemplate(rest, parameters);
	if (urlTemplate == nullptr)
		return Status(404);

	const ibValueMetaObjectHttpServiceMethod* const method = urlTemplate->FindHttpMethod(ibString::FromUTF8(request.m_method));
	if (method == nullptr) {
		ibHttpResponse refused = Status(405);
		refused.m_headers.emplace_back("Allow", std::string(urlTemplate->GetAllowed().ToUtf8()));
		return refused;
	}

	// EVERY REFUSAL BELOW THROWS: the caller is told 500 and the journal why.
	ibValueModuleManagerRuntimeConfiguration* const manager = session->GetManagerModule();
	if (manager == nullptr)
		ibBackendCoreException::Error(_("the session has no runtime"));
	ibValueModuleManager::ibValueModuleUnit* const unit = manager->FindCommonModule(service->GetServiceModule());
	if (unit == nullptr)
		ibBackendCoreException::Error(_("the module of the HTTP service '%s' is not registered in this session"), service->GetName());

	const ibValuePtr<ibValueHttpServiceRequest> asked(new ibValueHttpServiceRequest(request, prefix + ibT("/") + root, rest, parameters));
	ibValue given = asked, answer;
	unit->ExecAsFunc(method->GetHandler(), answer, given);

	// No answer — a handler that returned none, and as well one the module has no function for: the call finds no
	// function and says nothing, so the sentence names both.
	ibValueHttpServiceResponse* response = nullptr;
	if (!answer.ConvertToValue(response) || response == nullptr)
		ibBackendCoreException::Error(_("the handler '%s' of the HTTP service '%s' gave no HTTPServiceResponse - a function of that name in the service module returns one"),
			method->GetHandler(), service->GetName());
	return response->ToResponse();
}

} // namespace

ibHttpResponse ibHttpServiceCall::Answer(ibApplicationInstance* applicationInstance, const ibHttpRequest& request,
	const ibString& prefix)
{
	if (applicationInstance == nullptr)
		return Status(503);

	ibString user, password;
	ReadCredentials(request, user, password);

	// A SESSION OF ITS OWN, for this request alone — its row in Active Users while it works, and gone with the holder.
	ibSessionHolder holder = applicationInstance->CreateSession<ibSession>(ibSessionKind::HttpService,
		ibGuid(ibGuid::newGuid()).str(), request.m_address);
	if (!holder)
		return Status(503);
	ibSession* const session = holder.Get();

	ibHttpResponse response;
	try {
		// ⭐ ON THE SESSION'S OWN WORKER — the login runs the configuration's session module, and the handler is the
		// configuration's code: everything a session does runs on its worker.
		ibSession::OpenResult opened = ibSession::OpenResult::Failed;
		session->Submit([&]() { opened = holder->Open(user, password); }).get();
		if (opened == ibSession::OpenResult::Timeout)
			return Status(503);
		if (opened != ibSession::OpenResult::Authenticated) {
			ibHttpResponse refused = Status(401);
			refused.m_headers.emplace_back("WWW-Authenticate", "Basic realm=\"" +
				std::string(applicationInstance->GetInstanceName().ToUtf8()) + "\", charset=\"UTF-8\"");
			return refused;
		}
		session->Submit([&]() { response = Run(session, request, prefix); }).get();
	}
	catch (const ibCoreException& err) {
		// The configuration's code talking: said where an administrator reads it, and the caller told no more than 500.
		if (ibLogger* const log = ibApplicationInstance::GetLogger(applicationInstance))
			log->Error(ibT("http"), ibT("failed"), ibString::Format(_("HTTP service %s %s: %s"),
				ibString::FromUTF8(request.m_method), ibString::FromUTF8(request.m_path), err.GetErrorDescription()));
		return Status(500);
	}
	return response;
}
