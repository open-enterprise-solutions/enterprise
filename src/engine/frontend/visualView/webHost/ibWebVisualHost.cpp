////////////////////////////////////////////////////////////////////////////
//	Author		: Tetracode Dev
//	Description : Web visual host — creates JSON proxy tree from form
////////////////////////////////////////////////////////////////////////////

#include "ibWebVisualHost.h"

#include "frontend/visualView/ctrl/frame.h"
#include "frontend/visualView/ctrl/control.h"

#include "backend/clsid.h"
#include "backend/backend_exception.h"

//***********************************************************************
//*                        Control type mapping                        *
//***********************************************************************

std::string ibWebVisualHost::MapControlType(const ibClassID& clsid)
{
	static const std::map<ibClassID, std::string> s_typeMap = {
		{ string_to_clsid("CT_FRME"), "form" },
		{ string_to_clsid("CT_BXSR"), "boxsizer" },
		{ string_to_clsid("CT_GRSR"), "gridsizer" },
		{ string_to_clsid("CT_NTBK"), "notebook" },
		{ string_to_clsid("CT_GRBX"), "gridbox" },
		{ string_to_clsid("CT_STXT"), "statictext" },
		{ string_to_clsid("CT_SLIN"), "staticline" },
		{ string_to_clsid("CT_TXED"), "textbox" },
		{ string_to_clsid("CT_BUTN"), "button" },
		{ string_to_clsid("CT_CHBX"), "checkbox" },
		{ string_to_clsid("CT_RDBT"), "radiobutton" },
		{ string_to_clsid("CT_CHCE"), "choice" },
		{ string_to_clsid("CT_CMBX"), "combobox" },
		{ string_to_clsid("CT_LSTB"), "listbox" },
		{ string_to_clsid("CT_SLDR"), "slider" },
		{ string_to_clsid("CT_GAGE"), "gauge" },
		{ string_to_clsid("CT_TBLB"), "tablebox" },
		{ string_to_clsid("CT_TLBR"), "toolbar" },
		{ string_to_clsid("CT_TOOL"), "tool" },
		{ string_to_clsid("CT_HTML"), "htmlbox" },
		{ string_to_clsid("CT_CHRT"), "chart" },
	};

	auto it = s_typeMap.find(clsid);
	return (it != s_typeMap.end()) ? it->second : "unknown";
}

//***********************************************************************
//*                         Constructor / Destructor                   *
//***********************************************************************

ibWebVisualHost::ibWebVisualHost(ibValueForm* valueForm)
	: m_valueForm(valueForm)
	, m_rootProxy(nullptr)
{
}

ibWebVisualHost::~ibWebVisualHost()
{
	delete m_rootProxy;
}

//***********************************************************************
//*                         Proxy creation                             *
//***********************************************************************

ibWebControlProxy* ibWebVisualHost::CreateProxy(ibValueFrame* control)
{
	auto* proxy = new ibWebControlProxy();
	proxy->m_controlId = control->GetControlID();
	proxy->m_type = MapControlType(control->GetClassType());
	proxy->m_name = control->GetControlName().ToStdString();
	proxy->m_frame = control;

	ReadControlProperties(control, proxy);

	m_proxyById[proxy->m_controlId] = proxy;
	return proxy;
}

void ibWebVisualHost::ReadControlProperties(ibValueFrame* control, ibWebControlProxy* proxy)
{
	// Read properties through the control's property interface
	for (long i = 0; i < control->GetNProps(); i++) {
		wxString propName = control->GetPropName(i);
		ibValue propVal;
		if (control->GetPropVal(i, propVal)) {
			wxString sval = propVal.GetString();
			if (!sval.IsEmpty()) {
				std::string key = propName.Lower().ToStdString();
				proxy->m_props[key] = sval.ToStdString();
			}
		}
	}
}

//***********************************************************************
//*                     Build proxy tree                               *
//***********************************************************************

void ibWebVisualHost::BuildChildren(ibValueFrame* parent, ibWebControlProxy* parentProxy)
{
	for (unsigned int i = 0; i < parent->GetChildCount(); i++) {
		ibValueFrame* child = parent->GetChild(i);
		ibWebControlProxy* childProxy = CreateProxy(child);
		parentProxy->AddChild(childProxy);
		BuildChildren(child, childProxy);
	}
}

bool ibWebVisualHost::CreateWebHost()
{
	if (m_valueForm == nullptr)
		return false;

	// Clean up
	delete m_rootProxy;
	m_rootProxy = nullptr;
	m_proxyById.clear();

	// Create root proxy
	m_rootProxy = CreateProxy(m_valueForm);

	// Recursively build children
	BuildChildren(m_valueForm, m_rootProxy);

	return true;
}

//***********************************************************************
//*                     JSON serialization                             *
//***********************************************************************

json ibWebVisualHost::ControlToJson(const ibWebControlProxy* proxy) const
{
	json j;
	j["id"] = proxy->m_controlId;
	j["type"] = proxy->m_type;

	if (!proxy->m_name.empty())
		j["name"] = proxy->m_name;

	if (!proxy->m_props.empty()) {
		json props = json::object();
		for (const auto& [key, value] : proxy->m_props)
			props[key] = value;
		j["props"] = props;
	}

	if (!proxy->m_children.empty()) {
		json children = json::array();
		for (const auto* child : proxy->m_children)
			children.push_back(ControlToJson(child));
		j["children"] = children;
	}

	return j;
}

json ibWebVisualHost::GetFormLayout() const
{
	if (m_rootProxy == nullptr)
		return json::object();

	return ControlToJson(m_rootProxy);
}

//***********************************************************************
//*                     Event handling                                 *
//***********************************************************************

ibWebControlProxy* ibWebVisualHost::FindProxy(int controlId) const
{
	auto it = m_proxyById.find(controlId);
	return (it != m_proxyById.end()) ? it->second : nullptr;
}

json ibWebVisualHost::HandleEvent(int controlId, const std::string& eventName,
	const json& /*eventData*/)
{
	json result;
	result["handled"] = false;

	if (m_valueForm == nullptr) {
		result["error"] = "No form";
		return result;
	}

	ibProcUnit* procUnit = m_valueForm->GetFormProcUnit();
	if (procUnit == nullptr) {
		// No compiled module — form has no script logic
		result["handled"] = true;
		return result;
	}

	try {
		wxString funcName = wxString::FromUTF8(eventName.c_str());
		ibValue retVal;
		procUnit->CallAsFunc(funcName, retVal);
		result["handled"] = true;
	}
	catch (const ibBackendException*) {
		// Handler not found or error — not fatal
		result["handled"] = false;
	}
	catch (...) {
		result["handled"] = false;
	}

	// Collect updates — re-read all proxies
	json updates = json::array();
	for (auto& [id, proxy] : m_proxyById) {
		if (proxy->m_frame != nullptr) {
			auto oldProps = proxy->m_props;
			proxy->m_props.clear();
			ReadControlProperties(proxy->m_frame, proxy);
			if (proxy->m_props != oldProps) {
				json upd;
				upd["id"] = id;
				upd["props"] = json::object();
				for (const auto& [k, v] : proxy->m_props)
					upd["props"][k] = v;
				updates.push_back(upd);
			}
		}
	}

	if (!updates.empty())
		result["updates"] = updates;

	return result;
}
