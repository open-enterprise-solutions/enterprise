#ifndef _METAFORMOBJECT_H__
#define _METAFORMOBJECT_H__

#include "metaModuleObject.h"
#include "backend/uniqueKey.h"

#include <functional>

#define defaultFormType wxNOT_FOUND
#define formDefaultName wxT("Form")

// -----------------------------------------------------------------------
// ibBackendCommandItem
// -----------------------------------------------------------------------

class BACKEND_API ibBackendCommandItem {
public:

	virtual ~ibBackendCommandItem() {}

	// THE single execution entry (nav sites call it directly): show a form OR run the command's own code. Each
	// metaobject PREDEFINES its behaviour by OVERRIDING this — a form/data metaobject uses the default below (opens
	// its form via GetFormByCommandType); a command OVERRIDES Execute to run its runtime handler instead. srcForm /
	// commandParameter are the context an override may use (the default form path ignores them), so a bare
	// Execute(cmdType) is the common call.
	// CONST — the metaobject is CONST (found through a const config); execution does NOT mutate it: it opens a form
	// (a fresh form value) or, for a command, spawns a TRANSIENT runtime (ibValueCommandDataObject) that lives only
	// for the call and vanishes. So a const command / object, resolved from the FORM's own const config, runs — no
	// activeMetaData, no const_cast.
	virtual bool Execute(ibInterfaceCommandType cmdType = ibInterfaceCommandType::ibInterfaceCommandType_Default,
	                     ibBackendValueForm* srcForm = nullptr, ibValue* commandParameter = nullptr) const;

protected:

	//get default form (the default Execute opens it; a command overrides Execute and returns nullptr here)
	virtual ibBackendValueForm* GetFormByCommandType(ibInterfaceCommandType cmdType = ibInterfaceCommandType::ibInterfaceCommandType_Default) const = 0;
};

// -----------------------------------------------------------------------
// ibValueMetaObjectFormBase
// -----------------------------------------------------------------------

class BACKEND_API ibSourceDataObject;

class BACKEND_API ibValueMetaObjectFormBase : public ibValueMetaObjectModuleBase {
	public:
private:


public:

#pragma region access_generic
	virtual bool AccessRight_Show() const { return true; }
	virtual bool AccessRight_Modify() const { return true; }
#pragma endregion

	ibValueMetaObjectFormBase(const wxString& strName = wxEmptyString, const wxString& synonym = wxEmptyString, const wxString& comment = wxEmptyString);

	bool LoadFormData(ibBackendValueForm* value) const;
	bool SaveFormData(ibBackendValueForm* value);

#pragma region _form_creator_h_

	static ibBackendValueForm* CreateAndBuildForm(const ibValueMetaObjectFormBase* creator,
		ibBackendControlFrame* ownerControl = nullptr,
		ibSourceDataObject* srcObject = nullptr, const ibUniqueKey& formGuid = wxNullGuid);

	static ibBackendValueForm* CreateAndBuildForm(const ibValueMetaObjectFormBase* creator, const ibFormID& form_id = defaultFormType,
		ibBackendControlFrame* ownerControl = nullptr,
		ibSourceDataObject* srcObject = nullptr, const ibUniqueKey& formGuid = wxNullGuid);

#pragma endregion

#pragma region _form_builder_h_
	// MY form value, bound to whatever source my kind implies — the one verb, answered by each
	// kind for itself: a common form stands alone, an object form asks the object that owns it.
	// A caller never asks "which kind are you" and then casts to say it; the access right is
	// answered by the same call.
	//
	// `formGuid` is the form's IDENTITY: empty lets it fall back to the source (the ordinary
	// open), a supplied one gives this instance an identity of its own — what an element placed
	// on the start page needs, since it may sit there beside a twin.
	virtual ibBackendValueForm* GetObjectForm(ibBackendControlFrame* ownerControl = nullptr,
		const ibUniqueKey& formGuid = wxNullGuid) const = 0;
#pragma endregion

	//set module code
	virtual void SetModuleText(const wxString& moduleText) = 0;
	virtual wxString GetModuleText() const = 0;

	//set form data 
	virtual void SetFormData(const wxMemoryBuffer& formData) const = 0;   // const: only mutates *m_propertyForm, not `this`
	virtual wxMemoryBuffer GetFormData() const = 0;

	// copy form data — the LIVE form's control tree AS a transparent node (Child), not a
	// blob: pull the live form, save it to a node directly (no base64 round-trip).
	ibDataValue CopyFormData() const;
	bool PasteFormData();

	// node <-> runtime-blob shim: the form blob already IS the binary-provider
	// node format, so the adapter is ONE provider round-trip. This lets the runtime stay
	// blob-based (SaveForm / LoadForm, the property cell, the prop-grid variant) while the
	// metadata serializes a transparent node tree — no base64 lump on disk / in JSON.
	static ibDataValue   FormBlobToNode(const wxMemoryBuffer& blob);
	static wxMemoryBuffer FormNodeToBlob(const ibDataValue& formNode);

	/**
	* Get type form
	*/
	virtual ibFormID GetTypeForm() const = 0;

	//prepare menu for item
	virtual bool CollectContextMenu(std::vector<ibMetaMenuItem>& items);
};

// -----------------------------------------------------------------------
// ibDeferredForm — lazy form-construction marker stored in the compile-value cache.
// -----------------------------------------------------------------------
// Eager form-build at OnAfterRunMetaObject time would assert on a null mm (the form's compile module must parent to
// the session root, which isn't compiled yet). The cache registers this descriptor instead and materializes the form
// on first FindCompileModule lookup. Lives HERE (not metaData.h): the form type is complete, so the constructor reads
// IsPasteMode inline — recording whether the form is being pasted right now, while the mark is still live.

class BACKEND_API ibValueMetaObjectGenericData;

class BACKEND_API ibDeferredForm {
public:
	// `build` = how THIS form materialises its runtime value. Both form kinds are DEFERRED (the build only
	// runs on first FindCompileModule, AFTER the whole config has run and every metaobject is registered) so
	// a form can safely resolve its attribute types / source hops against objects that register later in the
	// pass. The two kinds differ only in the entry: an OBJECT form goes through its owning GenericData (which
	// binds the source object), a COMMON form builds standalone — both land on CreateAndBuildForm underneath.
	// The form ptr is kept for the paste re-home: Construct reads its LIVE paste mark at build time.
	ibDeferredForm(ibValueMetaObjectFormBase* form, std::function<ibBackendValueForm*()> build) noexcept
		: m_form(form), m_build(std::move(build)) {}

	// Runs `build()`, wrapped into an ibValue* (out-of-line — needs formWrapper complete). If the form's metaobject
	// is marked as a paste at build time, the built controls re-home (PasteNode) and the stored blob is normalized to
	// raw. The paste completion (ibValueMetaObject::PasteObject) forces this build while the mark is still live, so
	// there is no captured flag — the live mark is the signal.
	ibValue* Construct() const;

	ibValueMetaObjectFormBase* Form() const { return m_form; }

private:
	ibValueMetaObjectFormBase*           m_form;
	std::function<ibBackendValueForm*()> m_build;
};

// -----------------------------------------------------------------------
// ibValueMetaObjectForm
// -----------------------------------------------------------------------

class BACKEND_API ibValueMetaObjectForm : public ibValueMetaObjectFormBase {
	public:

public:

	ibValueMetaObjectForm(const wxString& strName = wxEmptyString, const wxString& synonym = wxEmptyString, const wxString& comment = wxEmptyString);

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	/**
	* Property events
	*/
	virtual void OnPropertyCreated(ibProperty* property);
	virtual void OnPropertySelected(ibProperty* property);
	virtual void OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue);

	//events:
	virtual bool OnCreateMetaObject(ibMetaData* metaData, int flags);
	virtual bool OnLoadMetaObject(ibMetaData* metaData);
	virtual bool OnSaveMetaObject(int flags);
	virtual bool OnDeleteMetaObject();

	//module manager is started or exit 
	virtual bool OnBeforeRunMetaObject(int flags);
	virtual bool OnAfterRunMetaObject(int flags);

	virtual bool OnBeforeCloseMetaObject();
	virtual bool OnAfterCloseMetaObject();

	//get property
	virtual ibProperty* GetModuleProperty() const { return m_propertyForm; }

	//set module code 
	virtual void SetModuleText(const wxString& moduleText) { m_propertyForm->SetValue(moduleText); }
	virtual wxString GetModuleText() const { return m_propertyForm->GetValueAsString(); }

	//set form data 
	virtual void SetFormData(const wxMemoryBuffer& formData) const { m_propertyForm->SetValue(formData); }
	virtual wxMemoryBuffer GetFormData() const { return m_propertyForm->GetValueAsMemoryBuffer(); }

	/**
	* Get type form
	*/
	virtual ibFormID GetTypeForm() const {
		return m_properyFormType->GetValueAsInteger();
	}

#pragma region _form_builder_h_
	// An object form belongs to a business object, and THAT object knows which source my kind
	// implies — a list form gets the list, an object form a NEW object. Body out-of-line: the
	// owner's type must be complete.
	virtual ibBackendValueForm* GetObjectForm(ibBackendControlFrame* ownerControl = nullptr,
		const ibUniqueKey& formGuid = wxNullGuid) const override;
#pragma endregion

protected:

	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

private:

	bool FillGenericFormType(ibPropertyList* prop);
	bool FillFormType(ibPropertyList* prop) {
		prop->AppendItem(formDefaultName, defaultFormType, GetIcon());
		return FillGenericFormType(prop);
	}

	ibPropertyForm* m_propertyForm = ibPropertyObject::CreateProperty<ibPropertyForm>(m_categoryContext, wxT("FormData"), _("Form"));
	ibPropertyCategory* m_categoryForm = ibPropertyObject::CreatePropertyCategory(wxT("Form"), _("Form"));
	ibPropertyList* m_properyFormType = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryForm, wxT("FormType"), _("Type"), &ibValueMetaObjectForm::FillFormType);
};

// -----------------------------------------------------------------------
// ibValueMetaObjectCommonForm
// -----------------------------------------------------------------------

class BACKEND_API ibValueMetaObjectCommonForm :
	public ibValueMetaObjectFormBase, public ibBackendCommandItem {
	public:

	// A common form can be checked into a section — it opens from the navigation panel.
	// (A form belonging to an object cannot: it is reached through that object.)
	virtual bool IsInterfaceAllowed() const override { return true; }

#pragma region access_generic
	virtual bool AccessRight_Show() const { return AccessRight_Use(); }
	virtual bool AccessRight_Modify() const { return AccessRight_Use(); }
#pragma endregion

#pragma region access
	bool AccessRight_Use() const { return IsFullAccess() || AccessRight(m_roleUse); }
#pragma endregion

	ibValueMetaObjectCommonForm(const wxString& strName = wxEmptyString, const wxString& synonym = wxEmptyString, const wxString& comment = wxEmptyString);

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//events:
	virtual bool OnCreateMetaObject(ibMetaData* metaData, int flags);

	//module manager is started or exit 
	virtual bool OnBeforeRunMetaObject(int flags);
	virtual bool OnAfterRunMetaObject(int flags);

	virtual bool OnBeforeCloseMetaObject();
	virtual bool OnAfterCloseMetaObject();

	//get property
	virtual ibProperty* GetModuleProperty() const { return m_propertyForm; }

	//set module code 
	virtual void SetModuleText(const wxString& moduleText) { m_propertyForm->SetValue(moduleText); }
	virtual wxString GetModuleText() const { return m_propertyForm->GetValueAsString(); }

	//set form data 
	virtual void SetFormData(const wxMemoryBuffer& formData) const { m_propertyForm->SetValue(formData); }
	virtual wxMemoryBuffer GetFormData() const { return m_propertyForm->GetValueAsMemoryBuffer(); }

	/**
	* Get type form
	*/
	virtual ibFormID GetTypeForm() const { return defaultFormType; }

#pragma region _form_builder_h_
	//support form
	virtual ibBackendValueForm* GetObjectForm(ibBackendControlFrame* ownerControl = nullptr,
		const ibUniqueKey& formGuid = wxNullGuid) const override;
#pragma endregion

protected:

	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

	//get default form
	virtual ibBackendValueForm* GetFormByCommandType(ibInterfaceCommandType cmdType = ibInterfaceCommandType::ibInterfaceCommandType_Default) const {

		if (cmdType == ibInterfaceCommandType::ibInterfaceCommandType_Default)
			return GetObjectForm();

		return GetObjectForm();
	}

private:

	ibPropertyForm* m_propertyForm = ibPropertyObject::CreateProperty<ibPropertyForm>(m_categoryContext, wxT("FormData"), _("Form"));

#pragma region role
	ibRole* m_roleUse = ibValueMetaObject::CreateRole(wxT("Use"), _("Use"));
#pragma endregion
};

#endif 