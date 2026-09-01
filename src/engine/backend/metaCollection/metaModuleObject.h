#ifndef _METAMODULE_OBJECT_H__
#define _METAMODULE_OBJECT_H__

#include "metaObject.h"
#include "backend/serialize/dataBuilder.h"   // ibDataNode — holder serializes its metaobject's node

enum ibContentHelper {
	eProcedureHelper = 1,
	eFunctionHelper,

	eUnknownHelper = 100
};

#pragma region _property_
//base property for "inner module"
template <typename T>
class ibPropertyInnerModule : public ibProperty {
public:

	template <typename... Args>
	ibPropertyInnerModule(ibPropertyCategory* cat, Args&&... args)
		: ibProperty(cat,
			std::get<0>(std::forward_as_tuple(args...)),
			std::get<1>(std::forward_as_tuple(args...)),
			wxNullVariant), m_metaObject(nullptr)
	{
		ibValueMetaObject* parent =
			static_cast<ibValueMetaObject*>(m_owner);
		wxASSERT(parent);
		m_metaObject = parent->CreateMetaObjectAndSetParent<T>(args...);
	}

	ibPropertyInnerModule(ibPropertyCategory* cat, T* metaObject)
		: ibProperty(cat, metaObject->GetName(), metaObject->GetSynonym(), wxNullVariant), m_metaObject(metaObject)
	{
	}

	virtual ~ibPropertyInnerModule() {}

	// get meta object
	T* GetMetaObject() const { return m_metaObject; }

	// get meta object via pointer
	T* operator->() { return GetMetaObject(); }

	// set/get property data
	virtual bool SetDataValue(const ibValue& varPropVal) { return false; }
	virtual bool GetDataValue(ibValue& pvarPropVal) const {
		pvarPropVal = m_metaObject;
		return true;
	}

	//per-type node value — the held metaobject's whole node (a Child sub-node)
	virtual bool ReadNodeValue(const ibDataValue& value) override {
		const std::shared_ptr<ibDataNode>& child = value.AsChild();
		if (child) m_metaObject->LoadNode(*child);
		return true;
	}
	virtual bool WriteNodeValue(ibDataValue& value) const override {
		auto child = std::make_shared<ibDataNode>();
		m_metaObject->SaveNode(*child);
		value = ibDataValue::Child(child);
		return true;
	}

	// copy & paste — the module rides its WHOLE node (code + guid + ID). LoadNode is a full
	// deserialization, so the module ADOPTS both identities off the payload; a paste has to hand
	// back both, and ResetAll is the verb that does.
	//
	// The guid, because a module caches its compiled bytecode BY guid (sys_bytecode_cache /
	// g_byteCodeRegistry): keeping the source's shares the original's cache row and loads the wrong
	// owner's bytecode -> "Binding type mismatch for 'ThisObject'".
	//
	// ⚠ AND THE ID, which was the half left behind. A copied document's modules kept the SOURCE's
	// metaIDs — two objects in one configuration answering to the same number, with the copy
	// shadowed: ibFindMetaObjectById returned the original for both. It is not cosmetic, because
	// the physical column is named `fld<id>` (see ibMetaData::GenerateNewID, which never re-issues
	// one for exactly that reason). Found 2026-08-31 by copying a document through metadata_copy
	// and reading the ids back — the designer's own Ctrl+C / Ctrl+V walks this same road.
	//
	// Both are safe to reset for the same reason: an ordinary metaobject re-homes its bindings BY
	// guid so it must adopt the source's, while a module has no re-homed hops and nothing addresses
	// it by either identity — the module storage holds pointers, not keys.
	virtual bool PasteNodeValue(const ibDataValue& value) override {
		const std::shared_ptr<ibDataNode>& child = value.AsChild();
		if (child) m_metaObject->LoadNode(*child);
		m_metaObject->ResetAll();
		return true;
	}

private:
	ibValuePtr<T> m_metaObject;
};

#pragma endregion

class BACKEND_API ibValueMetaObjectModuleBase : public ibValueMetaObject {
	public:

	ibValueMetaObjectModuleBase(const wxString& name = wxEmptyString, const wxString& synonym = wxEmptyString, const wxString& comment = wxEmptyString)
		: ibValueMetaObject(name, synonym, comment) {
	}

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//events:
	virtual bool OnCreateMetaObject(ibMetaData* metaData, int flags);
	virtual bool OnLoadMetaObject(ibMetaData* metaData);
	virtual bool OnSaveMetaObject(int flags);
	virtual bool OnDeleteMetaObject();

	//get property
	virtual ibProperty* GetModuleProperty() const = 0;

	//module manager is started or exit 
	virtual bool OnBeforeRunMetaObject(int flags);
	virtual bool OnAfterCloseMetaObject();

	//set module code
	virtual void SetModuleText(const wxString& moduleText) = 0;
	virtual wxString GetModuleText() const = 0;



	//set default procedures
	void SetDefaultProcedure(const wxString& procName, const ibContentHelper& contentHelper, std::vector<wxString> args = {});

	// Register a default FUNCTION-shaped handler (helper kind eFunctionHelper, so the handler may
	// `Return` a value) — the function-side sibling of SetDefaultProcedure.
	void SetDefaultFunction(const wxString& funcName, std::vector<wxString> args = {});

	size_t GetDefaultProcedureCount() const {
		return m_contentHelper.size();
	}

	wxString GetDefaultProcedureName(size_t idx) const {
		if (idx > m_contentHelper.size())
			return wxEmptyString;

		auto it = m_contentHelper.begin();
		std::advance(it, idx);
		return it->first;
	}

	ibContentHelper GetDefaultProcedureType(size_t idx) const {
		if (idx > m_contentHelper.size())
			return ibContentHelper::eUnknownHelper;
		auto it = m_contentHelper.begin();
		std::advance(it, idx);
		return it->second.m_contentType;
	}

	std::vector<wxString> GetDefaultProcedureArgs(size_t idx) const {
		if (idx > m_contentHelper.size())
			return std::vector<wxString>();
		auto it = m_contentHelper.begin();
		std::advance(it, idx);
		return it->second.m_args;
	}

	virtual bool IsGlobalModule() const { return false; }

private:

	struct CContentData {
		ibContentHelper m_contentType;
		std::vector<wxString> m_args;
	};

	std::map<wxString, CContentData> m_contentHelper;
};

class BACKEND_API ibValueMetaObjectModule : public ibValueMetaObjectModuleBase {
	public:
	ibValueMetaObjectModule(const wxString& name = wxEmptyString, const wxString& synonym = wxEmptyString, const wxString& comment = wxEmptyString)
		: ibValueMetaObjectModuleBase(name, synonym, comment)
	{
	}

	//get property
	virtual ibProperty* GetModuleProperty() const { return m_propertyModule; }

	//set module code 
	virtual void SetModuleText(const wxString& moduleText) { m_propertyModule->SetValue(moduleText); }
	virtual wxString GetModuleText() const { return m_propertyModule->GetValueAsString(); }

protected:

	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

private:
	ibPropertyModule* m_propertyModule = ibPropertyObject::CreateProperty<ibPropertyModule>(m_categoryContext, wxT("Module"), _("Module"));
};

class BACKEND_API ibValueMetaObjectCommonModule : public ibValueMetaObjectModuleBase {
	public:
private:

public:

	ibValueMetaObjectCommonModule(const wxString& name = wxEmptyString, const wxString& synonym = wxEmptyString, const wxString& comment = wxEmptyString);

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//events:
	virtual bool OnCreateMetaObject(ibMetaData* metaData, int flags);
	virtual bool OnLoadMetaObject(ibMetaData* metaData);
	virtual bool OnSaveMetaObject(int flags);
	virtual bool OnDeleteMetaObject();

	virtual bool OnRenameMetaObject(const wxString& sNewName);

	//module manager is started or exit 
	virtual bool OnBeforeRunMetaObject(int flags);
	virtual bool OnAfterRunMetaObject(int flags);

	virtual bool OnBeforeCloseMetaObject();
	virtual bool OnAfterCloseMetaObject();

	//get property
	virtual ibProperty* GetModuleProperty() const { return m_propertyModule; }

	//set module code 
	virtual void SetModuleText(const wxString& moduleText) { m_propertyModule->SetValue(moduleText); }
	virtual wxString GetModuleText() const { return m_propertyModule->GetValueAsString(); }

	//prepare menu for item
	virtual bool CollectContextMenu(std::vector<ibMetaMenuItem>& items);

	// check gm
	virtual bool IsGlobalModule() const {
		return m_propertyGlobalModule->GetValueAsBoolean();
	}

	// Manager-module flag — false on plain common module, true on
	// ibValueMetaObjectManagerModule. Replaces the explicit managerModule
	// argument that was passed to mm->AddCommonModule.
	virtual bool IsManagerModule() const { return false; }

	/**
	* Property events
	*/
	virtual bool OnPropertyChanging(ibProperty* property, const wxVariant& newValue);
	virtual void OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue);

protected:

	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

private:
	ibPropertyModule* m_propertyModule = ibPropertyObject::CreateProperty<ibPropertyModule>(m_categoryContext, wxT("Module"), _("Module"));
	ibPropertyCategory* m_moduleCategory = ibPropertyObject::CreatePropertyCategory(wxT("Common module"), _("Common module"));
	ibPropertyBoolean* m_propertyGlobalModule = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_moduleCategory, wxT("GlobalModule"), _("Global module"), false);
};

class BACKEND_API ibValueMetaObjectManagerModule : public ibValueMetaObjectCommonModule {
	public:
	ibValueMetaObjectManagerModule(const wxString& name = wxEmptyString, const wxString& synonym = wxEmptyString, const wxString& comment = wxEmptyString)
		: ibValueMetaObjectCommonModule(name, synonym, comment)
	{
	}

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	virtual bool IsManagerModule() const override { return true; }

	//module manager is started or exit
	virtual bool OnBeforeRunMetaObject(int flags);
	virtual bool OnAfterRunMetaObject(int flags);

	virtual bool OnBeforeCloseMetaObject();
	virtual bool OnAfterCloseMetaObject();
};

#endif