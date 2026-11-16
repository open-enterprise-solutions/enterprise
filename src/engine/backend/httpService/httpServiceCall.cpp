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
#include "backend/served/servedKind.h"                   // the kind this file registers
#include "backend/session/session.h"
#include "backend/session/sessionRegistry.h"             // CreateSession<T> — its body; InstallUser, NotifyAuthenticated
#include "backend/system/value/valueHttp.h"              // HTTPServiceRequest, HTTPServiceResponse

#include "core/utils/base64.h"                           // the user and the password of Basic

namespace {

ibHttpResponse Status(int status)
{
	ibHttpResponse response;
	response.m_status = status;
	return response;
}

// NOBODY LET IN — and the scheme to try, under the platform's own realm: what the base is called is not a stranger's
// to read.
ibHttpResponse Unauthorized()
{
	ibHttpResponse refused = Status(401);
	refused.m_headers.emplace_back("WWW-Authenticate", "Basic realm=\"OES\", charset=\"UTF-8\"");
	return refused;
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
	if (colon == ibNOT_FOUND)
		return;
	user = pair.Left(colon);
	password = pair.Mid(colon + 1);
}

// THE SERVICE AN ENTRY NAMES, in that configuration — null when it is not there: deleted, or not an HTTP service.
const ibValueMetaObjectHttpService* FindService(const ibMetaDataConfigurationBase* metaData, const ibGuid& service)
{
	return metaData != nullptr ? metaData->FindAnyObjectByFilter<ibValueMetaObjectHttpService>(service, g_metaHttpServiceCLSID) : nullptr;
}

// The handler called — on the session's worker, in the session's configuration: the one the service and its method
// were found in (Answer); `rest` and `parameters` what the template made of the path.
ibHttpResponse Run(ibSession* session, const ibValueMetaObjectHttpService* service, const ibValueMetaObjectHttpServiceMethod* method,
	const ibHttpRequest& request, const ibString& prefix, const ibString& rest, const std::vector<std::pair<ibString, ibString>>& parameters)
{
	// EVERY REFUSAL BELOW THROWS: the caller is told 500 and the journal why.
	ibValueModuleManagerRuntimeConfiguration* const manager = session->GetManagerModule();
	if (manager == nullptr)
		ibBackendCoreException::Error(_("the session has no runtime"));
	ibValueModuleManager::ibValueModuleUnit* const unit = manager->FindCommonModule(service->GetServiceModule());
	if (unit == nullptr)
		ibBackendCoreException::Error(_("the module of the HTTP service '%s' is not registered in this session"), service->GetName());

	const ibValuePtr<ibValueHttpServiceRequest> asked(new ibValueHttpServiceRequest(request, prefix, rest, parameters));
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

// THE KIND — an entry names its service by its object; the configuration offers each service it declares at
// es/<RootURL>.
class ibServedHttpService : public ibServedKind {
public:

	virtual ibHttpResponse Answer(const ibServedAddress& address, const ibHttpRequest& request, const ibString& prefix) const override
	{
		return ibHttpServiceCall::Answer(ibApplicationInstance::Get(false), address.m_object, request, prefix);
	}

	virtual void Check(const ibServedAddress& address) const override
	{
		if (FindService(ibApplicationInstance::GetActiveMetaData(), address.m_object) == nullptr)
			ibBackendCoreException::Error(_("The web address %s '%s' is not registered: the configuration has no HTTP service %s."),
				address.m_kind, address.m_path, address.m_object.str());
	}

	virtual std::vector<ibServedAddress> Offer() const override
	{
		std::vector<ibServedAddress> offers;
		const ibMetaDataConfigurationBase* const metaData = ibApplicationInstance::GetActiveMetaData();
		if (metaData == nullptr)
			return offers;
		for (const ibValueMetaObject* candidate : metaData->GetAnyArrayObject(g_metaHttpServiceCLSID)) {
			if (candidate->IsDeleted())
				continue;
			const ibValueMetaObjectHttpService* const service = static_cast<const ibValueMetaObjectHttpService*>(candidate);
			ibServedAddress offer;
			offer.m_kind = ibT("HTTPService");
			offer.m_path = service->GetServedPath();
			offer.m_object = service->GetGuid().GetGuid();
			offers.push_back(std::move(offer));
		}
		return offers;
	}
};

} // namespace

ibHttpResponse ibHttpServiceCall::Answer(ibApplicationInstance* applicationInstance, const ibGuid& object,
	const ibHttpRequest& request, const ibString& prefix)
{
	ibSessionRegistry* const registry = ibApplicationInstance::GetSessionRegistry(applicationInstance);
	if (applicationInstance == nullptr || registry == nullptr)
		return Status(503);

	// ⭐ WHO IS ASKING, BEFORE ANY SESSION — the check alone (the login's own, a password's hash and nothing more): a
	// caller not let in is answered without a session made for it, and whatever it asked for is not looked up.
	ibString user, password;
	ReadCredentials(request, user, password);
	ibUserInfo info;
	if (!applicationInstance->AuthenticateUser(user, password, info))
		return Unauthorized();

	// ⭐ WHAT ANSWERS IT, BEFORE ANY SESSION — the service by the entry's object, the template by the rest of the path,
	// the method by the request's, in the configuration the base runs, held while the request lasts: a path or a method
	// nothing answers is told so with no session made for it (a session brings up every common module).
	ibMetaDataConfigurationBase* const active = ibApplicationInstance::GetActiveMetaData(applicationInstance);
	if (active == nullptr)
		return Status(503);
	const std::shared_ptr<ibMetaDataConfigurationBase> metaData =
		std::static_pointer_cast<ibMetaDataConfigurationBase>(active->shared_from_this());
	const ibValueMetaObjectHttpService* const service = FindService(metaData.get(), object);
	if (service == nullptr)
		return Status(404);

	// /trade/es/desk/Transfer/12 — the rest after the entry's path: /Transfer/12.
	const ibString path = ibString::FromUTF8(request.m_path);
	if (path.length() < prefix.length())
		return Status(404);
	const ibString rest = path.Mid(prefix.length());

	std::vector<std::pair<ibString, ibString>> parameters;
	const ibValueMetaObjectUrlTemplate* const urlTemplate = service->FindTemplate(rest, parameters);
	if (urlTemplate == nullptr)
		return Status(404);

	const ibValueMetaObjectHttpServiceMethod* const method = urlTemplate->FindHttpMethod(request.m_method);
	if (method == nullptr) {
		ibHttpResponse refused = Status(405);
		refused.m_headers.emplace_back("Allow", urlTemplate->GetAllowed());
		return refused;
	}

	// A SESSION OF ITS OWN, for this request alone — its row in Active Users while it works, and gone with the holder.
	ibSessionHolder holder = applicationInstance->CreateSession<ibSession>(ibSessionKind::HttpService,
		ibGuid(ibGuid::newGuid()).str(), request.m_address);
	if (!holder)
		return Status(503);
	ibSession* const session = holder.Get();

	ibHttpResponse response;
	try {
		// ⭐ ON THE SESSION'S OWN WORKER — the user installed and the session brought up (its runtime built, the
		// configuration's session module run), then the handler: everything a session does runs on its worker. The
		// commit half of a login, as a job's run-as user is (ibJobManager): the check was made above.
		session->Submit([&]() {
			if (info.IsOk())
				registry->InstallUser(session, info, ibEmptyString);
			registry->NotifyAuthenticated(session);
			// The session took the configuration the base runs now — the one looked in above, unless an apply came in
			// between: then what was found is not this session's to call, and the caller asks again.
			if (session->GetMetaData() != metaData.get()) {
				response = Status(503);
				return;
			}
			response = Run(session, service, method, request, prefix, rest, parameters);
		}).get();
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

SERVED_KIND_REGISTER(ibServedHttpService, "HTTPService");
