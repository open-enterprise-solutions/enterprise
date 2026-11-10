#ifndef __ADVPROP_EVENT_TOOL_H__
#define __ADVPROP_EVENT_TOOL_H__

#include "deskgui/univ/propgrid/propgrid.h"
#include "frmclient/backend/propertyManager/propertyObject.h"   // ibPropertyChoiceList — the backend list this ctor converts

// -----------------------------------------------------------------------
// ibPGEventToolProperty
// -----------------------------------------------------------------------

class ibPGEventToolProperty : public ibPGProperty {
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

	// Backend's choice list in, ibPGChoices built here — see ibPropertyChoiceList.
	ibPGEventToolProperty(const wxString& label = ibPG_LABEL,
		const wxString& name = ibPG_LABEL, const ibPropertyChoiceList& choices = ibPropertyChoiceList(),
		const ibVariant& value = ibVariant());

	virtual ~ibPGEventToolProperty();

	virtual wxString ValueToString(ibVariant& value,
		ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;

	virtual bool StringToValue(ibVariant& variant,
		const wxString& text,
		ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;
	
	virtual bool IntToValue(ibVariant& value,
		int number,
		ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;

	virtual void OnSetValue() override;
	virtual ibPGEditorDialogAdapter* GetEditorDialog() const override;

private:
	IB_PG_DECLARE_PROPERTY_CLASS(ibPGEventToolProperty);
};

#endif