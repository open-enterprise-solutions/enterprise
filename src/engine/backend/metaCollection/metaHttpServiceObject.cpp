////////////////////////////////////////////////////////////////////////////
//	Description : HTTP service — the addresses a configuration answers, and the code that answers them
////////////////////////////////////////////////////////////////////////////

#include "metaHttpServiceObject.h"

#include "backend/metaData.h"
#include "backend/system/value/valueHttp.h"   // ibHttpMethodName — a method's word

#include <algorithm>
#include <wx/wxcrt.h>   // wxIsalnum — a parameter's name, a root's letters

namespace {

// A path's segments, read between its slashes: `/Transfer/12/reset` is Transfer, 12, reset; the root (`/`, or
// nothing) none. A slash at the end says nothing — `/Transfer/` is `/Transfer`.
std::vector<ibString> Segments(const ibString& path)
{
	std::vector<ibString> segments;
	size_t from = 0;
	while (from < path.length()) {
		size_t to = path.find(ibT('/'), from);
		if (to == ibString::npos)
			to = path.length();
		if (to > from)
			segments.push_back(path.Mid(from, to - from));
		from = to + 1;
	}
	return segments;
}

// A segment in braces — a parameter — and its name.
bool IsParameter(const ibString& segment, ibString& name)
{
	if (segment.length() < 2 || segment[0] != ibT('{') || segment.Last() != ibT('}'))
		return false;
	name = segment.Mid(1, segment.length() - 2);
	return true;
}

// The kinds of segment, in the order they are preferred.
enum ibSegmentRank { ibSegmentRank_Literal = 0, ibSegmentRank_Parameter = 1, ibSegmentRank_Rest = 2 };

// What every child of `owner` of the kind `clsid` is, the deleted ones aside.
template <class T>
std::vector<T*> Children(const ibValueMetaObject* owner, const ibClassID& clsid)
{
	std::vector<T*> children;
	for (unsigned int idx = 0; idx < owner->GetChildCount(); idx++) {
		ibValueMetaObject* const child = owner->GetChild(idx);
		if (child != nullptr && !child->IsDeleted() && child->GetClassType() == clsid)
			children.push_back(static_cast<T*>(child));
	}
	return children;
}

} // namespace

//***********************************************************************
//*                             HTTP service                            *
//***********************************************************************

ibValueMetaObjectHttpService::ibValueMetaObjectHttpService(const ibString& name, const ibString& synonym, const ibString& comment)
	: ibValueMetaObject(name, synonym, comment)
{
}

const ibValueMetaObjectUrlTemplate* ibValueMetaObjectHttpService::FindTemplate(const ibString& path,
	std::vector<std::pair<ibString, ibString>>& parameters) const
{
	const ibValueMetaObjectUrlTemplate* found = nullptr;
	std::vector<int> best;
	for (const ibValueMetaObjectUrlTemplate* urlTemplate : Children<ibValueMetaObjectUrlTemplate>(this, g_metaUrlTemplateCLSID)) {
		std::vector<int> rank;
		std::vector<std::pair<ibString, ibString>> given;
		if (!ibValueMetaObjectUrlTemplate::Match(urlTemplate->GetTemplate(), path, rank, given))
			continue;
		// The most particular answers; between equals, the first declared — so only a better one replaces it.
		if (found == nullptr || rank < best) {
			found = urlTemplate;
			best = std::move(rank);
			parameters = std::move(given);
		}
	}
	return found;
}

void ibValueMetaObjectHttpService::PrepareModule()
{
	ibValueMetaObjectManagerModule* const module = m_propertyServiceModule->GetMetaObject();
	if (module == nullptr)
		return;

	// ONE FUNCTION PER METHOD, the ones the service has now — a method gone takes its function off the list, and
	// the code written in the module stays as it was.
	module->ClearDefaultProcedures();
	for (const ibValueMetaObjectUrlTemplate* urlTemplate : Children<ibValueMetaObjectUrlTemplate>(this, g_metaUrlTemplateCLSID)) {
		for (const ibValueMetaObjectHttpServiceMethod* method : Children<ibValueMetaObjectHttpServiceMethod>(urlTemplate, g_metaHttpServiceMethodCLSID)) {
			if (!method->GetHandler().IsEmpty())
				module->SetDefaultFunction(method->GetHandler(), { ibT("Request") });
		}
	}
}

bool ibValueMetaObjectHttpService::OnPropertyChanging(ibProperty* property, const ibVariant& newValue)
{
	if (property == m_propertyRootUrl) {
		// ONE SEGMENT OF AN ADDRESS: letters, digits and the marks an address carries as they are (RFC 3986's
		// unreserved) — no slash, no space, nothing that has to be escaped to be read back.
		const ibString root = newValue.GetString();
		if (root.IsEmpty())
			return false;
		for (size_t idx = 0; idx < root.length(); idx++) {
			const ibChar c = root[idx];
			if (!wxIsalnum(c) && c != ibT('-') && c != ibT('_') && c != ibT('.') && c != ibT('~'))
				return false;
		}
		// …and the configuration's alone: a request names its service by it.
		if (m_metaData != nullptr) {
			for (const ibValueMetaObject* other : m_metaData->GetAnyArrayObject(g_metaHttpServiceCLSID)) {
				if (other != this && !other->IsDeleted()
					&& static_cast<const ibValueMetaObjectHttpService*>(other)->GetRootUrl() == root)
					return false;
			}
		}
	}
	return ibValueMetaObject::OnPropertyChanging(property, newValue);
}

//***********************************************************************
//*        lifecycle — forward to the inner module (own load/save)      *
//***********************************************************************

bool ibValueMetaObjectHttpService::OnCreateMetaObject(ibMetaData* metaData, int flags)
{
	if (!ibValueMetaObject::OnCreateMetaObject(metaData, flags))
		return false;
	// A new service answers under its own name until somebody says otherwise — a root nobody else has.
	if (m_propertyRootUrl->IsEmptyProperty())
		m_propertyRootUrl->SetValue(GetName());
	return (*m_propertyServiceModule)->OnCreateMetaObject(metaData, flags);
}

bool ibValueMetaObjectHttpService::OnLoadMetaObject(ibMetaData* metaData)
{
	if (!(*m_propertyServiceModule)->OnLoadMetaObject(metaData))
		return false;
	return ibValueMetaObject::OnLoadMetaObject(metaData);
}

bool ibValueMetaObjectHttpService::OnSaveMetaObject(int flags)
{
	if (!(*m_propertyServiceModule)->OnSaveMetaObject(flags))
		return false;
	return ibValueMetaObject::OnSaveMetaObject(flags);
}

bool ibValueMetaObjectHttpService::OnDeleteMetaObject()
{
	if (!(*m_propertyServiceModule)->OnDeleteMetaObject())
		return false;
	return ibValueMetaObject::OnDeleteMetaObject();
}

bool ibValueMetaObjectHttpService::OnBeforeRunMetaObject(int flags)
{
	// The module registers itself into the module storage here, so a request finds it in its session.
	if (!(*m_propertyServiceModule)->OnBeforeRunMetaObject(flags))
		return false;
	return ibValueMetaObject::OnBeforeRunMetaObject(flags);
}

bool ibValueMetaObjectHttpService::OnAfterRunMetaObject(int flags)
{
	if (!(*m_propertyServiceModule)->OnAfterRunMetaObject(flags))
		return false;
	if (!ibValueMetaObject::OnAfterRunMetaObject(flags))
		return false;

	// The functions follow the methods loaded — prepared here, once the templates and their methods are read.
	PrepareModule();
	return true;
}

bool ibValueMetaObjectHttpService::OnBeforeCloseMetaObject()
{
	if (!(*m_propertyServiceModule)->OnBeforeCloseMetaObject())
		return false;
	return ibValueMetaObject::OnBeforeCloseMetaObject();
}

bool ibValueMetaObjectHttpService::OnAfterCloseMetaObject()
{
	if (!(*m_propertyServiceModule)->OnAfterCloseMetaObject())
		return false;
	return ibValueMetaObject::OnAfterCloseMetaObject();
}

bool ibValueMetaObjectHttpService::ReadData(const ibDataNode& node)
{
	if (!ibValueMetaObject::ReadData(node))
		return false;

	m_propertyServiceModule->SetNodeValue(node.GetProperty(m_propertyServiceModule->GetName()));
	m_propertyRootUrl->SetNodeValue(node.GetProperty(m_propertyRootUrl->GetName()));
	return true;
}

bool ibValueMetaObjectHttpService::WriteData(ibDataNode& node) const
{
	if (!ibValueMetaObject::WriteData(node))
		return false;

	node.SetProperty(m_propertyServiceModule->GetName(), m_propertyServiceModule->GetNodeValue());
	node.SetProperty(m_propertyRootUrl->GetName(),       m_propertyRootUrl->GetNodeValue());
	return true;
}

//***********************************************************************
//*                             URL template                            *
//***********************************************************************

ibValueMetaObjectUrlTemplate::ibValueMetaObjectUrlTemplate(const ibString& name, const ibString& synonym, const ibString& comment)
	: ibValueMetaObject(name, synonym, comment)
{
}

const ibValueMetaObjectHttpServiceMethod* ibValueMetaObjectUrlTemplate::FindHttpMethod(const ibString& method) const
{
	const ibValueMetaObjectHttpServiceMethod* any = nullptr;
	for (const ibValueMetaObjectHttpServiceMethod* candidate : Children<ibValueMetaObjectHttpServiceMethod>(this, g_metaHttpServiceMethodCLSID)) {
		const ibHttpMethod kind = candidate->GetHttpMethod();
		if (kind == ibHttpMethod_Any) {
			if (any == nullptr)
				any = candidate;
			continue;
		}
		// The request's method as it was written — the protocol's word is upper-case, and only that one is it.
		const char* const name = ibHttpMethodName(kind);
		if (name != nullptr && method == ibString(name))
			return candidate;
	}
	return any;
}

ibString ibValueMetaObjectUrlTemplate::GetAllowed() const
{
	static const ibHttpMethod s_methods[] = { ibHttpMethod_Get, ibHttpMethod_Post, ibHttpMethod_Put, ibHttpMethod_Patch,
		ibHttpMethod_Delete, ibHttpMethod_Head, ibHttpMethod_Options };

	ibString allowed;
	for (const ibHttpMethod method : s_methods) {
		if (FindHttpMethod(ibHttpMethodName(method)) == nullptr)
			continue;
		if (!allowed.IsEmpty())
			allowed += ibT(", ");
		allowed += ibString::FromUTF8(ibHttpMethodName(method));
	}
	return allowed;
}

bool ibValueMetaObjectUrlTemplate::Match(const ibString& urlTemplate, const ibString& path, std::vector<int>& rank,
	std::vector<std::pair<ibString, ibString>>& parameters)
{
	const std::vector<ibString> wanted = Segments(urlTemplate);
	const std::vector<ibString> given = Segments(path);

	rank.clear();
	parameters.clear();
	size_t at = 0;
	for (size_t idx = 0; idx < wanted.size(); idx++) {
		const ibString& segment = wanted[idx];
		ibString name;
		// THE REST — whatever is left, nothing included: `/files/*` answers /files as it answers /files/a/b.
		if (segment == ibT("*")) {
			rank.push_back(ibSegmentRank_Rest);
			return true;
		}
		if (at >= given.size())
			return false;
		if (IsParameter(segment, name)) {
			rank.push_back(ibSegmentRank_Parameter);
			parameters.emplace_back(name, given[at]);
		}
		else if (segment == given[at])
			rank.push_back(ibSegmentRank_Literal);
		else
			return false;
		at++;
	}
	return at == given.size();
}

bool ibValueMetaObjectUrlTemplate::IsTemplate(const ibString& urlTemplate)
{
	// From the root, with no empty segment between two slashes.
	if (urlTemplate.IsEmpty() || urlTemplate[0] != ibT('/') || urlTemplate.Contains(ibT("//")))
		return false;

	const std::vector<ibString> segments = Segments(urlTemplate);
	std::vector<ibString> names;
	for (size_t idx = 0; idx < segments.size(); idx++) {
		const ibString& segment = segments[idx];
		ibString name;
		// The rest of the path — only as the last segment.
		if (segment == ibT("*")) {
			if (idx + 1 != segments.size())
				return false;
			continue;
		}
		// A parameter: a name of letters, digits and '_', and no name twice.
		if (IsParameter(segment, name)) {
			if (name.IsEmpty() || std::find(names.begin(), names.end(), name) != names.end())
				return false;
			for (size_t c = 0; c < name.length(); c++) {
				if (!wxIsalnum(name[c]) && name[c] != ibT('_'))
					return false;
			}
			names.push_back(name);
			continue;
		}
		// A literal: nothing a template or an address reads as something else.
		for (size_t c = 0; c < segment.length(); c++) {
			const ibChar ch = segment[c];
			if (ch <= 0x20 || ch == 0x7F || ch == ibT('{') || ch == ibT('}') || ch == ibT('*') || ch == ibT('?') || ch == ibT('#'))
				return false;
		}
	}
	return true;
}

bool ibValueMetaObjectUrlTemplate::OnPropertyChanging(ibProperty* property, const ibVariant& newValue)
{
	if (property == m_propertyTemplate && !IsTemplate(newValue.GetString()))
		return false;
	return ibValueMetaObject::OnPropertyChanging(property, newValue);
}

bool ibValueMetaObjectUrlTemplate::ReadData(const ibDataNode& node)
{
	if (!ibValueMetaObject::ReadData(node))
		return false;

	m_propertyTemplate->SetNodeValue(node.GetProperty(m_propertyTemplate->GetName()));
	return true;
}

bool ibValueMetaObjectUrlTemplate::WriteData(ibDataNode& node) const
{
	if (!ibValueMetaObject::WriteData(node))
		return false;

	node.SetProperty(m_propertyTemplate->GetName(), m_propertyTemplate->GetNodeValue());
	return true;
}

//***********************************************************************
//*                          HTTP service method                        *
//***********************************************************************

ibValueMetaObjectHttpServiceMethod::ibValueMetaObjectHttpServiceMethod(const ibString& name, const ibString& synonym, const ibString& comment)
	: ibValueMetaObject(name, synonym, comment)
{
}

ibValueMetaObjectHttpService* ibValueMetaObjectHttpServiceMethod::GetService() const
{
	ibValueMetaObject* const urlTemplate = GetParent();
	ibValueMetaObject* const service = urlTemplate != nullptr ? urlTemplate->GetParent() : nullptr;
	if (service == nullptr || service->GetClassType() != g_metaHttpServiceCLSID)
		return nullptr;
	return static_cast<ibValueMetaObjectHttpService*>(service);
}

bool ibValueMetaObjectHttpServiceMethod::OnCreateMetaObject(ibMetaData* metaData, int flags)
{
	if (!ibValueMetaObject::OnCreateMetaObject(metaData, flags))
		return false;
	if (m_propertyHandler->IsEmptyProperty()) {
		const ibValueMetaObject* const urlTemplate = GetParent();
		m_propertyHandler->SetValue((urlTemplate != nullptr ? urlTemplate->GetName() : ibString()) + GetName());
	}
	if (ibValueMetaObjectHttpService* const service = GetService())
		service->PrepareModule();
	return true;
}

void ibValueMetaObjectHttpServiceMethod::OnPropertyChanged(ibProperty* property, const ibVariant& oldValue, const ibVariant& newValue)
{
	ibValueMetaObject::OnPropertyChanged(property, oldValue, newValue);
	if (property == m_propertyHandler) {
		if (ibValueMetaObjectHttpService* const service = GetService())
			service->PrepareModule();
	}
}

bool ibValueMetaObjectHttpServiceMethod::ReadData(const ibDataNode& node)
{
	if (!ibValueMetaObject::ReadData(node))
		return false;

	m_propertyHttpMethod->SetNodeValue(node.GetProperty(m_propertyHttpMethod->GetName()));
	m_propertyHandler->SetNodeValue(node.GetProperty(m_propertyHandler->GetName()));
	return true;
}

bool ibValueMetaObjectHttpServiceMethod::WriteData(ibDataNode& node) const
{
	if (!ibValueMetaObject::WriteData(node))
		return false;

	node.SetProperty(m_propertyHttpMethod->GetName(), m_propertyHttpMethod->GetNodeValue());
	node.SetProperty(m_propertyHandler->GetName(),    m_propertyHandler->GetNodeValue());
	return true;
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(ibValueMetaObjectHttpService, "HTTPService", g_metaHttpServiceCLSID);
METADATA_TYPE_REGISTER(ibValueMetaObjectUrlTemplate, "URLTemplate", g_metaUrlTemplateCLSID);
METADATA_TYPE_REGISTER(ibValueMetaObjectHttpServiceMethod, "HTTPServiceMethod", g_metaHttpServiceMethodCLSID);
ENUM_TYPE_REGISTER(ibValueEnumHttpServiceMethodKind, "HTTPServiceMethodKind", enum_to_clsid("EN_HSMT"));
