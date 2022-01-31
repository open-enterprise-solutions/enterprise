#ifndef _BASE_CONTROL_H_
#define _BASE_CONTROL_H_

#include "baseFrame.h"

class IValueControl : public IValueFrame {
	wxDECLARE_ABSTRACT_CLASS(IValueControl);
public:
	IValueControl();
	virtual ~IValueControl();

	/**
	* Obtiene un hijo del objeto.
	*/
	IValueControl* GetChild(unsigned int idx);
	IValueControl* GetChild(unsigned int idx, const wxString& type);

	IValueControl* FindNearAncestor(const wxString &type) { return wxDynamicCast(IObjectBase::FindNearAncestor(type), IValueControl); }
	IValueControl* FindNearAncestorByBaseClass(const wxString &type) { return wxDynamicCast(IObjectBase::FindNearAncestorByBaseClass(type), IValueControl); }

	/**
	* Support form
	*/
	virtual CValueForm* GetOwnerForm() const { return m_formOwner; }
	virtual void SetOwnerForm(CValueForm *ownerForm);

	// allow getting value in control
	virtual bool HasValueInControl() const { return false; }
	/*
	* Get/set value in control
	*/
	virtual CValue GetControlValue() const { return CValue(); }
	virtual void SetControlValue(CValue &vSelected) {}

	//get metadata
	virtual IMetadata *GetMetaData() const override;

	//get typelist 
	virtual OptionList *GetTypelist() const override;

	/**
	* Can delete object
	*/
	virtual bool CanDeleteControl() const { return true; }

	//runtime 
	virtual CProcUnit *GetFormProcUnit() const;

	/**
	* Get type form
	*/
	virtual form_identifier_t GetTypeForm() const;

protected:

	//frame owner 
	CValueForm* m_formOwner;
};

#endif // !_BASE_H_
