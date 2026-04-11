#ifndef _SIZER_H_
#define _SIZER_H_

#include "control.h"

class FRONTEND_API ibValueSizer : public ibValueControl {
	wxDECLARE_ABSTRACT_CLASS(ibValueSizer);
public:

	ibValueSizer() : ibValueControl() {}

	virtual int GetComponentType() const { return COMPONENT_TYPE_SIZER; }

	//load & save object in control 
	virtual bool LoadData(ibReaderMemory& reader);
	virtual bool SaveData(ibWriterMemory writer = ibWriterMemory());

protected:
	void UpdateSizer(wxSizer* sizer);

protected:
	ibPropertyCategory* m_categorySizer = ibPropertyObject::CreatePropertyCategory(wxT("SizerItem"), _("Sizer"));
	ibPropertySize* m_propertyMinSize = ibPropertyObject::CreateProperty<ibPropertySize>(m_categorySizer, wxT("MinimumSize"), _("Minimum size"), _("Sets the minimum size of the window, to indicate to the sizer layout mechanism that this is the minimum required size."), wxDefaultSize);
};

//////////////////////////////////////////////////////////////////////////////

class FRONTEND_API ibValueSizerItem : public ibValueFrame {
	wxDECLARE_DYNAMIC_CLASS(ibValueSizerItem);
public:

	void SetProportion(int proportion) {
		m_propertyProportion->SetValue(proportion);
	}

	int GetProportion() const {
		return m_propertyProportion->GetValueAsUInteger();
	}

	void SetFlagBorder(long flag_border) const {
		m_propertyFlagBorderLeft->SetValue((flag_border & (wxLEFT)) != 0);
		m_propertyFlagBorderRight->SetValue((flag_border & (wxRIGHT)) != 0);
		m_propertyFlagBorderTop->SetValue((flag_border & (wxUP)) != 0);
		m_propertyFlagBorderBottom->SetValue((flag_border & (wxDOWN)) != 0);
	}

	long GetFlagBorder() const {
		long flag = 0;
		if (m_propertyFlagBorderLeft->GetValueAsBoolean()) flag |= wxLEFT;
		if (m_propertyFlagBorderRight->GetValueAsBoolean()) flag |= wxRIGHT;
		if (m_propertyFlagBorderTop->GetValueAsBoolean()) flag |= wxUP;
		if (m_propertyFlagBorderBottom->GetValueAsBoolean()) flag |= wxDOWN;
		return flag;
	}

	void SetFlagState(const wxStretch& s) const {
		m_propertyFlagState->SetValue(s);
	}

	wxStretch GetFlagState() const {
		return m_propertyFlagState->GetValueAsEnum();
	}

	int GetBorder() const {
		return m_propertyBorder->GetValueAsUInteger();
	}

	void SetBorder(long border) const {
		m_propertyBorder->SetValue(border);
	}

	ibValueSizerItem();

