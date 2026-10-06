#ifndef _BASE_CONTROL_H_
#define _BASE_CONTROL_H_

#include "frame.h"
#include "backend/propertyManager/property/propertyFunctionalOptions.h"

class SFRONTEND_API ibTypeControlFactory;   // AutoBindNewSource binds through the caller's factory subobject

class SFRONTEND_API ibValueControl : public ibValueFrame {
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

	
	// ⭐ AVAILABLE — what the functional options of this base say, asked by every place that draws or offers an
	// element beside its own Visible, and ANDed in rather than written into it, so neither the code nor the
	// user's "Change form" can bring such an element back. Here: the options the element names are not all
	// off, and the element it stands inside is available (a plain sizer too — switched off, it takes what it
	// lays out with it). A source control (textbox / checkbox / tablebox) overrides it and adds the field it is
	// bound to: a field this base does not use (functionalOptionGate.h) takes the control with it.
	virtual bool IsAvailable() const override;

protected:

	// A just-added source control (checkbox / textbox) with no source provisions a form attribute NAMED after
	// it, TYPED by its own value, and binds. The caller passes its OWN factory subobject (`this` — implicit
	// upcast, NOT a cross-cast); the source-set lives there.
	void AutoBindNewSource(ibTypeControlFactory* factory);

	// A control that BINDS to a source (textbox / checkbox / tablebox — an ibTypeControlFactory) with NO
	// source picked is INCOMPLETE: it is never shown, so a dangling unbound control can neither reach the
	// user nor later try to read a source it hasn't got. Default false — a control with no source
	// requirement (label, sizer, button …) is always shown. Source controls OVERRIDE this to test their own
	// bound path. The gate is applied once, in ibValueWindow::Update — the frame says the control is not
	// visible, and what it holds goes with it.
	//
	// ⚠ NOT IsAvailable, and not folded into it: an unbound control is the AUTHOR'S unfinished work, an
	// unavailable one is the BASE'S decision. Two meanings, two names — they meet only where an element is
	// drawn, side by side (Max, 2026-09-28: "one verb where it really is one").
	virtual bool IsUnbound() const { return false; }

protected:

	// Loaded and saved HERE, once, for every element: each kind's own Read/WriteData ends in this class's.
	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

	//frame owner 
	ibValueForm* m_formOwner;

	ibPropertyUString* m_propertyName = ibPropertyObject::CreateProperty<ibPropertyUString>(m_category, wxT("Name"), _("Name"), _("Object name"), wxT(""));

	// USER VISIBILITY — a group of its own: the functional options the element is available under. Empty =
	// available whatever the options are.
	ibPropertyCategory* m_categoryUserVisibility = ibPropertyObject::CreatePropertyCategory(wxT("UserVisibility"), _("User visibility"));
	ibPropertyFunctionalOptions* m_propertyFunctionalOptions = ibPropertyObject::CreateProperty<ibPropertyFunctionalOptions>(m_categoryUserVisibility, wxT("FunctionalOptions"), _("Functional options"), _("The functional options this element is available under: while every one of them is off, it is neither shown nor offered, and neither is what it holds. Empty - available whatever the options are. Works together with Visible and does not change it."));
};

#endif // !_BASE_H_
