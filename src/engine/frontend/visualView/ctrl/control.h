#ifndef _BASE_CONTROL_H_
#define _BASE_CONTROL_H_

#include "frame.h"

class FRONTEND_API IValueControl : public IValueFrame {
	wxDECLARE_ABSTRACT_CLASS(IValueControl);
protected:
	Property* m_propertyName = IPropertyObject::CreateProperty({ "name", "name", "The name of the control." }, PropertyType::PT_WXNAME);
public:

	IValueControl();
	virtual ~IValueControl();

	virtual void SetControlName(const wxString& controlName) {
		m_propertyName->SetValue(controlName);
	}

	virtual wxString GetControlName() const {
		return m_propertyName->GetValueAsString();
	}

	/**
	* Obtiene un hijo del objeto.
	*/
	IValueControl* GetChild(unsigned int idx) const;
	IValueControl* GetChild(unsigned int idx, const wxString& type) const;

	IValueControl* FindNearAncestor(const wxString& type) const {
		return wxDynamicCast(IPropertyObject::FindNearAncestor(type), IValueControl);
	}

	IValueControl* FindNearAncestorByBaseClass(const wxString& type) const {
		return wxDynamicCast(IPropertyObject::FindNearAncestorByBaseClass(type), IValueControl);
	}

	/**
	* Property events
	*/
	virtual bool OnPropertyChanging(Property* property, const wxVariant& newValue);
	virtual void OnPropertyChanged(Property* property, const wxVariant& oldValue, const wxVariant& newValue);

	/**
	* Support form
	*/
	virtual CValueForm* GetOwnerForm() const { 
		return m_formOwner;
	}

	virtual void SetOwnerForm(CValueForm* ownerForm);

	// allow getting value in control
	virtual bool HasValueInControl() const { 
		return false; 
	}
	
	/*
	* Get/set value in control
	*/	
	virtual bool SetControlValue(const CValue& varControlVal = CValue()) { return false; }
	virtual bool GetControlValue(CValue& pvarControlVal) const {
		return false;
	}

	//get metaData
	virtual IMetaData* GetMetaData() const override;

	/**
	* Can delete object
	*/
	virtual bool CanDeleteControl() const { 
		return true; 
	}

	//runtime 
	virtual CProcUnit* GetFormProcUnit() const;

	/**
	* Get type form
	*/
	virtual form_identifier_t GetTypeForm() const;

protected:

	//frame owner 
	CValueForm* m_formOwner;
};

#endif // !_BASE_H_
