#ifndef _FRMCLIENT_VIEW_BASE_CONTROL_H__
#define _FRMCLIENT_VIEW_BASE_CONTROL_H__

#include "frame.h"
#include "frmclient/backend/propertyManager/property/propertyString.h"

// THE CONTROL — the desktop's ibValueControl (frontend/visualView/ctrl/control.h): its name, kept as its property. Its
// functional options are the server's: what it offers, the frame draws.
class ibValueControl : public ibValueFrame {
public:

	ibValueControl(ibVisualHostClient& host, long long controlId) : ibValueFrame(host, controlId) {}

	/**
	* Support control name
	*/
	virtual bool GetControlNameAsString(wxString& result) const override {
		return m_propertyName->GetValueAsString(result);
	}

	virtual bool SetControlNameAsString(const wxString& result) const override {
		m_propertyName->SetValue(result);
		return true;
	}

protected:

	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

	ibPropertyUString* m_propertyName = ibPropertyObject::CreateProperty<ibPropertyUString>(m_category, wxT("Name"), _("Name"), _("Object name"), wxT(""));
};

#endif
