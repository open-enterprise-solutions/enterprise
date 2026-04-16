////////////////////////////////////////////////////////////////////////////
//	Author		: Tetracode Dev
//	Description : Web control proxy — replaces wxObject for web rendering
//	              Each proxy represents one control in the server-side form tree.
//	              Serializes to JSON for the frontend.
////////////////////////////////////////////////////////////////////////////

#ifndef _IB_WEB_CONTROL_PROXY_H__
#define _IB_WEB_CONTROL_PROXY_H__

#include <wx/object.h>
#include <wx/string.h>

#include <string>
#include <vector>
#include <map>

// Forward declare
class ibValueFrame;

// JSON proxy for a single form control.
// Created by ibWebVisualHost::Create() instead of wxButton/wxTextCtrl/etc.
// Lives on the server for the duration of the session.

class ibWebControlProxy : public wxObject {
public:

	ibWebControlProxy()
		: m_controlId(0)
		, m_frame(nullptr)
	{
	}

	~ibWebControlProxy() {
		for (auto* child : m_children)
			delete child;
	}

	// Identity
	int m_controlId;
	std::string m_type;        // "form", "button", "textbox", "tablebox", "sizer", etc.
	std::string m_name;

	// Visual properties
	std::map<std::string, std::string> m_props;

	// Hierarchy
	ibWebControlProxy* m_parent = nullptr;
	std::vector<ibWebControlProxy*> m_children;

	// Back-pointer to the C++ control (for event handling)
	ibValueFrame* m_frame;

	// Add child
	void AddChild(ibWebControlProxy* child) {
		child->m_parent = this;
		m_children.push_back(child);
	}
};

#endif // _IB_WEB_CONTROL_PROXY_H__
