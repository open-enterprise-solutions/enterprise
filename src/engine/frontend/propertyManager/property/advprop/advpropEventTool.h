#ifndef __ADVPROP_EVENT_TOOL_H__
#define __ADVPROP_EVENT_TOOL_H__

#include <wx/propgrid/propgrid.h>
#include "backend/backend_core.h"
#include "backend/propertyManager/propertyObject.h"   // ibPropertyChoiceList — the backend list this ctor converts

class BACKEND_API ibPropertyObject;

// -----------------------------------------------------------------------
// ibPGEventToolProperty
// -----------------------------------------------------------------------

class ibPGEventToolProperty : public wxPGProperty {
private:

	mutable class wxEventToolPropertyData {
	public:
		void SetNumber(const long& lAction) {
			m_lAction = lAction;
			m_strEventAction.clear();
			m_customEvent = false;
		}
		long GetNumber() const { return m_lAction; }
		void SetString(const wxString& strEventAction) {
			m_lAction = wxNOT_FOUND;
			m_strEventAction = strEventAction;
			m_customEvent = true;
		}
		wxString GetString() const { return m_strEventAction; }
		bool IsCustomAction() const { return m_customEvent; }
	private:
		int m_lAction = wxNOT_FOUND;
		bool m_customEvent = false;
		wxString m_strEventAction = wxEmptyString;
	} m_actionData;

public:

	bool IsCustomAction() const { return m_actionData.IsCustomAction(); }

	long GetNumber() const { return m_actionData.GetNumber(); }
	wxString GetString() const { return m_actionData.GetString(); }

	// Backend's choice list in, wxPGChoices built here — see ibPropertyChoiceList.
	ibPGEventToolProperty(const wxString& label = wxPG_LABEL,
		const wxString& name = wxPG_LABEL, const ibPropertyChoiceList& choices = ibPropertyChoiceList(),
		const wxVariant& value = wxNullVariant);

	virtual ~ibPGEventToolProperty();

	virtual wxString ValueToString(wxVariant& value,
		wxPGPropValFormatFlags flags = wxPGPropValFormatFlags::Null) const override;

	virtual bool StringToValue(wxVariant& variant,
		const wxString& text,
		wxPGPropValFormatFlags flags = wxPGPropValFormatFlags::Null) const override;
	
	virtual bool IntToValue(wxVariant& value,
		int number,
		wxPGPropValFormatFlags flags = wxPGPropValFormatFlags::Null) const override;

	virtual void OnSetValue() override;
	virtual wxPGEditorDialogAdapter* GetEditorDialog() const override;

private:
	WX_PG_DECLARE_PROPERTY_CLASS(ibPGEventToolProperty);
};

#endif