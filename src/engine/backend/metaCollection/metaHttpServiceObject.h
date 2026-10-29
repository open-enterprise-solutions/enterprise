#ifndef __META_HTTP_SERVICE_OBJECT_H__
#define __META_HTTP_SERVICE_OBJECT_H__

// AN HTTP SERVICE — the addresses a configuration answers, and the code that answers them.
//
//     HTTPService      RootURL "desk", a module of its own
//       URLTemplate      Template "/Transfer/{id}/reset"
//         HTTPServiceMethod  HTTPMethod Post, Handler "TransferReset"
//
// A request to <publication>/hs/desk/Transfer/12/reset is answered by the service module's
//     Function TransferReset(Request)
// given an HTTPServiceRequest (its URLParameters hold id = "12") and returning an HTTPServiceResponse
// (valueHttp.h). The web server is the transport, the service is the base's (backend/httpService/httpServiceCall.h).
//
// ⭐ BUILT ON THE PREDEFINED SCHEDULED JOB, which already pairs a metaobject with a MANAGER module of its own:
// registered and compiled with the session's modules, so a handler calls common modules like any other code
// and is debugged as any module is. The service module is prepared with one function per method, named by
// its Handler and taking Request — what the module editor offers, as an event handler's procedure is offered.
//
// ⭐ A TEMPLATE IS SEGMENTS, each one of three: a literal (`Transfer`), a parameter (`{id}` — one segment,
// handed over in URLParameters), or `*` — the rest of the path, slashes and all, and only as the last segment.
// Of the templates a path matches, the most particular answers: a literal before a parameter, a parameter
// before `*`, read from the left; between two of the same shape the one declared first.

#include "metaModuleObject.h"   // ibPropertyInnerModule + ibValueMetaObjectManagerModule
#include "backend/system/systemEnum.h"   // ibHttpMethod — the client's, and Any

#include <utility>
#include <vector>

// WHICH METHOD A SERVICE METHOD ANSWERS — the client's HTTPMethod (systemManagerEnum.h), and Any.
class ibValueEnumHttpServiceMethodKind : public ibValueEnumeration<ibHttpMethod> {
public:
	ibValueEnumHttpServiceMethodKind() : ibValueEnumeration() {}

	// The protocol's words, the same in every language — as the client's are.
	virtual void CreateEnumeration() {
		AddEnumeration(ibHttpMethod::ibHttpMethod_Any, ibT("Any"), ibT("Any"));
		AddEnumeration(ibHttpMethod::ibHttpMethod_Get, ibT("Get"), ibT("Get"));
		AddEnumeration(ibHttpMethod::ibHttpMethod_Post, ibT("Post"), ibT("Post"));
		AddEnumeration(ibHttpMethod::ibHttpMethod_Put, ibT("Put"), ibT("Put"));
		AddEnumeration(ibHttpMethod::ibHttpMethod_Patch, ibT("Patch"), ibT("Patch"));
		AddEnumeration(ibHttpMethod::ibHttpMethod_Delete, ibT("Delete"), ibT("Delete"));
		AddEnumeration(ibHttpMethod::ibHttpMethod_Head, ibT("Head"), ibT("Head"));
		AddEnumeration(ibHttpMethod::ibHttpMethod_Options, ibT("Options"), ibT("Options"));
	}
};

class ibValueMetaObjectHttpServiceMethod;
class ibValueMetaObjectUrlTemplate;

//********************************************************************************************
//*                                     HTTP service                                         *
//********************************************************************************************

class BACKEND_API ibValueMetaObjectHttpService : public ibValueMetaObject {
public:

	ibValueMetaObjectHttpService(const ibString& name = ibEmptyString, const ibString& synonym = ibEmptyString, const ibString& comment = ibEmptyString);

	// The first segment after /hs/ — one segment, unique in the configuration.
	ibString GetRootUrl() const { return m_propertyRootUrl->GetValueAsString(); }

	// The module the handlers live in — a manager module, registered with the session's modules.
	const ibValueMetaObjectManagerModule* GetServiceModule() const { return m_propertyServiceModule->GetMetaObject(); }

	// THE TEMPLATE A PATH ASKS FOR — `path` is what follows the root (`/Transfer/12/reset`, empty for the root
	// itself), and the parameters it gives are put in `parameters` in the template's order. Null: none matches.
	const ibValueMetaObjectUrlTemplate* FindTemplate(const ibString& path, std::vector<std::pair<ibString, ibString>>& parameters) const;

	// The module prepared with one function per method of the service, named by its Handler.
	void PrepareModule();

	// The child it hosts — a URL template.
	virtual ibClassID ResolveChild(const ibClassID& clsid) const override {
		return clsid == g_metaUrlTemplateCLSID ? clsid : 0;
	}

	//support icons
	virtual ibPicture GetIcon() const;
	static ibPicture GetIconGroup();

	// designer context menu — "Open service module", as a job's module is opened.
	virtual bool CollectContextMenu(std::vector<ibMetaMenuItem>& items) override;

	// A root that is not one segment, or that another service already has, is refused.
	virtual bool OnPropertyChanging(ibProperty* property, const ibVariant& newValue) override;

	//lifecycle — forward to the inner module (its own registration)
	virtual bool OnCreateMetaObject(ibMetaData* metaData, int flags);
	virtual bool OnLoadMetaObject(ibMetaData* metaData);
	virtual bool OnSaveMetaObject(int flags);
	virtual bool OnDeleteMetaObject();

	virtual bool OnBeforeRunMetaObject(int flags);
	virtual bool OnAfterRunMetaObject(int flags);
	virtual bool OnBeforeCloseMetaObject();
	virtual bool OnAfterCloseMetaObject();

protected:

	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

private:

	ibPropertyCategory* m_categoryService = ibPropertyObject::CreatePropertyCategory(ibT("HTTPService"), _("HTTP service"));
	ibPropertyString* m_propertyRootUrl = ibPropertyObject::CreateProperty<ibPropertyString>(m_categoryService, ibT("RootURL"), _("Root URL"),
		_("The service's address: one segment after /hs/ in the address the web server publishes the base under - <publication>/hs/<root URL>/... Unique in the configuration."), ibEmptyString);

	// The service's module — a MANAGER module (registered, compiled with the session), as a job's is.
	ibPropertyInnerModule<ibValueMetaObjectManagerModule>* m_propertyServiceModule =
		ibPropertyObject::CreateProperty<ibPropertyInnerModule<ibValueMetaObjectManagerModule>>(
			m_categoryContext, ibT("ServiceModule"), _("Service module"), _("The service's code: a function for each method, named by its Handler, given the HTTPServiceRequest and returning an HTTPServiceResponse."));

	friend class ibMetaData;
};

//********************************************************************************************
//*                                     URL template                                         *
//********************************************************************************************

class BACKEND_API ibValueMetaObjectUrlTemplate : public ibValueMetaObject {
public:

	ibValueMetaObjectUrlTemplate(const ibString& name = ibEmptyString, const ibString& synonym = ibEmptyString, const ibString& comment = ibEmptyString);

	ibString GetTemplate() const { return m_propertyTemplate->GetValueAsString(); }

	// THE METHOD A REQUEST'S METHOD ASKS FOR — its own, else one that answers Any. Null: none, and the answer is
	// 405 with the methods the template has (GetAllowed).
	const ibValueMetaObjectHttpServiceMethod* FindHttpMethod(const ibString& method) const;
	// …the methods it answers, as an Allow header lists them; Any answers every one.
	ibString GetAllowed() const;

	// ⭐ THE MATCH, ON ITS OWN — what FindTemplate asks of each template, and what a test asks without a
	// configuration. True when `path` is one of the template's; `rank` then says how particular the match is
	// (lower is more particular: a literal segment counts before a parameter, a parameter before `*`), and
	// `parameters` hold the template's parameters by name.
	static bool Match(const ibString& urlTemplate, const ibString& path, std::vector<int>& rank,
		std::vector<std::pair<ibString, ibString>>& parameters);
	// …and whether a text is a template at all: from '/', segments of a literal, a {parameter} or a last `*`.
	static bool IsTemplate(const ibString& urlTemplate);

	// The child it hosts — a method.
	virtual ibClassID ResolveChild(const ibClassID& clsid) const override {
		return clsid == g_metaHttpServiceMethodCLSID ? clsid : 0;
	}

	//support icons
	virtual ibPicture GetIcon() const;
	static ibPicture GetIconGroup();

	// A template that is not one is refused.
	virtual bool OnPropertyChanging(ibProperty* property, const ibVariant& newValue) override;

protected:

	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

private:

	ibPropertyCategory* m_categoryTemplate = ibPropertyObject::CreatePropertyCategory(ibT("URLTemplate"), _("URL template"));
	ibPropertyString* m_propertyTemplate = ibPropertyObject::CreateProperty<ibPropertyString>(m_categoryTemplate, ibT("Template"), _("Template"),
		_("The paths this template answers, after the service's root: segments that are literal (/Transfer), a parameter in braces (/{id} - one segment, given to the handler in URLParameters), or * as the last one (the rest of the path). /Transfer/{id}/reset, /*. The most particular template a path matches answers it."), ibT("/"));
};

//********************************************************************************************
//*                                     HTTP service method                                  *
//********************************************************************************************

class BACKEND_API ibValueMetaObjectHttpServiceMethod : public ibValueMetaObject {
public:

	ibValueMetaObjectHttpServiceMethod(const ibString& name = ibEmptyString, const ibString& synonym = ibEmptyString, const ibString& comment = ibEmptyString);

	ibHttpMethod GetHttpMethod() const { return m_propertyHttpMethod->GetValueAsEnum(); }
	// The function of the service module that answers it.
	ibString GetHandler() const { return m_propertyHandler->GetValueAsString(); }

	// The service it belongs to — its template's owner.
	ibValueMetaObjectHttpService* GetService() const;

	// Hosts no children.
	virtual ibClassID ResolveChild(const ibClassID&) const override { return 0; }

	//support icons
	virtual ibPicture GetIcon() const;
	static ibPicture GetIconGroup();

	// A new method is answered by <template><method> until somebody names it otherwise, as its function is
	// prepared in the service module.
	virtual bool OnCreateMetaObject(ibMetaData* metaData, int flags);
	// …and a new Handler prepares the module for it.
	virtual void OnPropertyChanged(ibProperty* property, const ibVariant& oldValue, const ibVariant& newValue) override;

protected:

	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

private:

	ibPropertyCategory* m_categoryMethod = ibPropertyObject::CreatePropertyCategory(ibT("HTTPServiceMethod"), _("HTTP service method"));
	ibPropertyEnum<ibValueEnumHttpServiceMethodKind>* m_propertyHttpMethod = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumHttpServiceMethodKind>>(m_categoryMethod, ibT("HTTPMethod"), _("HTTP method"),
		_("The request method this answers on its template: Get, Post, Put, Patch, Delete, Head, Options - or Any, every method the template has no method of its own for."), ibHttpMethod::ibHttpMethod_Get);
	ibPropertyString* m_propertyHandler = ibPropertyObject::CreateProperty<ibPropertyString>(m_categoryMethod, ibT("Handler"), _("Handler"),
		_("The function of the service module that answers: Function <Handler>(Request), given the HTTPServiceRequest and returning an HTTPServiceResponse."), ibEmptyString);
};

#endif // !__META_HTTP_SERVICE_OBJECT_H__
