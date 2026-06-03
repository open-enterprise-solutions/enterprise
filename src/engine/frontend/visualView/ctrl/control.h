#ifndef _BASE_CONTROL_H_
#define _BASE_CONTROL_H_

#include "frame.h"

class FRONTEND_API ibValueControl : public ibValueFrame {
	public:

	ibValueControl();
	virtual ~ibValueControl();

	/**
	* Support control name
	*/

	virtual bool GetControlNameAsString(wxString& result) const {
		return m_propertyName->GetValueAsString(result);
	}

	virtual bool SetControlNameAsString(const wxString& result) const {
		m_propertyName->SetValue(result);
		return true;
	}

	/**
	* Property events
	*/
	virtual bool OnPropertyChanging(ibProperty* property, const wxVariant& newValue);
	virtual void OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue);

	/**
	* Support form
	*/
	virtual ibValueForm* GetOwnerForm() const {
		return m_formOwner;
	}

	virtual void SetOwnerForm(ibValueForm* ownerForm);

	// allow getting value in control
	virtual bool HasValueInControl() const { return false; }

	/*
	* Get/set value in control
	*/
	virtual bool SetControlValue(const ibValue& varControlVal = ibValue()) { return false; }
	virtual bool GetControlValue(ibValue& pvarControlVal) const {
		return false;
	}

	//get metaData
	virtual const ibMetaData* GetMetaData() const override;

	/**
	* Can delete object
	*/
	virtual bool CanDeleteControl() const {
		return true;
	}

	/**
	* Get type form
	*/
	virtual ibFormID GetTypeForm() const;

protected:
	
	//frame owner 
	ibValueForm* m_formOwner;

	ibPropertyUString* m_propertyName = ibPropertyObject::CreateProperty<ibPropertyUString>(m_category, wxT("Name"), _("Name"), _("Object name"), wxT(""));
};

#endif // !_BASE_H_
