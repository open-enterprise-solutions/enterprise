////////////////////////////////////////////////////////////////////////////
//	Author		: Tetracode Dev
//	Description : Web visual host — server-side form rendering
//	              Standalone class (not inheriting ibVisualHost which
//	              requires wxScrolledWindow). Same recursive control
//	              traversal pattern, but outputs JSON instead of wxWidgets.
////////////////////////////////////////////////////////////////////////////

#ifndef _IB_WEB_VISUAL_HOST_H__
#define _IB_WEB_VISUAL_HOST_H__

#include "ibWebControlProxy.h"

#include "frontend/visualView/ctrl/form.h"

#include <json.hpp>

#include <unordered_map>

using json = nlohmann::json;

class FRONTEND_API ibWebVisualHost {
public:

	ibWebVisualHost(ibValueForm* valueForm);
	~ibWebVisualHost();

	// Build the proxy tree by recursively walking the form's control hierarchy
	bool CreateWebHost();

	// Serialize entire form layout to JSON for frontend
	json GetFormLayout() const;

	// Serialize a single control
	json ControlToJson(const ibWebControlProxy* proxy) const;

	// Handle event from frontend → call ibProcUnit → return updates
	json HandleEvent(int controlId, const std::string& eventName, const json& eventData);

	// Lookup
	ibWebControlProxy* FindProxy(int controlId) const;

	// Access
	ibValueForm* GetValueForm() const { return m_valueForm; }

private:

	// Create proxy for one control
	ibWebControlProxy* CreateProxy(ibValueFrame* control);

	// Read properties from control into proxy
	void ReadControlProperties(ibValueFrame* control, ibWebControlProxy* proxy);

	// Recursive builder
	void BuildChildren(ibValueFrame* parent, ibWebControlProxy* parentProxy);

	// Map CLSID → type name
	static std::string MapControlType(const ibClassID& clsid);

	ibValueForm* m_valueForm;
	ibWebControlProxy* m_rootProxy;
	std::unordered_map<int, ibWebControlProxy*> m_proxyById;
};

#endif // _IB_WEB_VISUAL_HOST_H__
