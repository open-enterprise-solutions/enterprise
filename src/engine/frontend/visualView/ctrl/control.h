#ifndef _BASE_CONTROL_H_
#define _BASE_CONTROL_H_

#include "frame.h"

class FRONTEND_API ibTypeControlFactory;   // AutoBindNewSource binds through the caller's factory subobject

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
	virtual bool GetControlValue(ibValue& pvarControlVal) const { return false; }

	//get metaData
	virtual const ibMetaData* GetMetaData() const override;

	/**
	* Can delete object
	*/
	virtual bool CanDeleteControl() const { return true; }

	/**
	* Get type form
	*/
	virtual ibFormID GetTypeForm() const;

protected:

	// Source controls (checkbox / textbox) call this from their CREATION event when the first-created flag
	// is set: a just-added control with no source provisions a form attribute NAMED after it, TYPED by its
	// own value, and binds. The caller passes its OWN factory subobject (`this` — implicit upcast, NOT a
	// cross-cast); the source-set lives there.
	void AutoBindNewSource(ibTypeControlFactory* factory);
	
	// A control that BINDS to a source (textbox / checkbox / tablebox — an ibTypeControlFactory) with NO
	// source picked is INCOMPLETE: the runtime never renders it, so a dangling unbound control can neither
	// reach the user nor later try to read a source it hasn't got. Default false — a control with no source
	// requirement (label, sizer, button …) always renders. Source controls OVERRIDE this to test their own
	// bound path. The gate is applied once per window family: ibValueWindow (plain widget) hides itself,
	// ibValueWindowComposite (tablebox) hides its whole chrome wrapper — so children fold in with the parent.
	virtual bool IsSourceMissing() const { return false; }

	//frame owner 
	ibValueForm* m_formOwner;

	ibPropertyUString* m_propertyName = ibPropertyObject::CreateProperty<ibPropertyUString>(m_category, wxT("Name"), _("Name"), _("Object name"), wxT(""));
};

#endif // !_BASE_H_
