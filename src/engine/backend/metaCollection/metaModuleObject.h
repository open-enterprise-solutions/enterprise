#ifndef _METAMODULE_OBJECT_H__
#define _METAMODULE_OBJECT_H__

#include "metaObject.h"
#include "core/serialize/dataBuilder.h"   // ibDataNode — holder serializes its metaobject's node
#include "protocol/protocol.h"            // ibProtocol::ibSchema::ibProperties::Object — what the holder's link opens

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
			ibVariant()), m_metaObject(nullptr)
	{
		ibValueMetaObject* parent =
			static_cast<ibValueMetaObject*>(m_owner);
		wxASSERT(parent);
		m_metaObject = parent->CreateMetaObjectAndSetParent<T>(args...);
		m_propHelp = m_metaObject->GetComment();   // a third argument is the module's comment, and the property's help
	}

	ibPropertyInnerModule(ibPropertyCategory* cat, T* metaObject)
		: ibProperty(cat, metaObject->GetName(), metaObject->GetSynonym(), metaObject->GetComment(), ibVariant()), m_metaObject(metaObject)
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
		const auto child = value.AsChild();
		if (child) m_metaObject->LoadNode(*child);
		return true;
	}
	virtual bool WriteNodeValue(ibDataValue& value) const override {
		auto child = std::make_shared<ibDataNode>();
		m_metaObject->SaveNode(*child);
		value = ibDataValue::Child(child);
		return true;
	}

	// …and what its link opens: the module it holds (Object).
	virtual bool GetProtocolValue(ibDataNode& row, ibReader reader) override {
		if (!ibProperty::GetProtocolValue(row, reader))
			return false;
		row.SetValue(ibProtocol::ibSchema::ibProperties::Object, static_cast<std::int32_t>(m_metaObject->GetMetaID()));
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
		const auto child = value.AsChild();
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

	ibValueMetaObjectModuleBase(const ibString& name = ibEmptyString, const ibString& synonym = ibEmptyString, const ibString& comment = ibEmptyString)
		: ibValueMetaObject(name, synonym, comment) {
	}

	//support icons
	virtual ibPicture GetIcon() const;
	static ibPicture GetIconGroup();

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

	// ⭐ THE WRITE STAYS WITH WHOEVER OWNS THE CELL — a module's property holds the text and
	// nothing else, a form's holds the text AND the control tree — so each says it with the setter
	// that names which half is meant. What every one of them must do AFTER is the same, and that
	// half is shared below.
	virtual void SetModuleText(const ibString& moduleText) = 0;
	virtual ibString GetModuleText() const = 0;

protected:

	// The shared half: say that whatever was built from this text is stale. Called by every
	// SetModuleText — the one thing a writer must not forget, and the reason they are worth
	// reading side by side.
	void InvalidateBuiltFromText();

public:

	//set default procedures
	void SetDefaultProcedure(const ibString& procName, const ibContentHelper& contentHelper, std::vector<ibString> args = {});

	// Register a default FUNCTION-shaped handler (helper kind eFunctionHelper, so the handler may
	// `Return` a value) — the function-side sibling of SetDefaultProcedure.
	void SetDefaultFunction(const ibString& funcName, std::vector<ibString> args = {});

	size_t GetDefaultProcedureCount() const {
		return m_contentHelper.size();
	}

	ibString GetDefaultProcedureName(size_t idx) const {
		if (idx > m_contentHelper.size())
			return ibEmptyString;

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

	std::vector<ibString> GetDefaultProcedureArgs(size_t idx) const {
		if (idx > m_contentHelper.size())
			return std::vector<ibString>();
		auto it = m_contentHelper.begin();
		std::advance(it, idx);
		return it->second.m_args;
	}

	// Forget them all — for an owner whose procedures follow a choice made later: an event handler
	// offers the procedure of the event it is set to, and only that one.
	void ClearDefaultProcedures() { m_contentHelper.clear(); }

	virtual bool IsGlobalModule() const { return false; }
	virtual bool IsPrivilegedModule() const { return false; }

private:

	struct CContentData {
		ibContentHelper m_contentType;
		std::vector<ibString> m_args;
	};

	std::map<ibString, CContentData> m_contentHelper;
};

class BACKEND_API ibValueMetaObjectModule : public ibValueMetaObjectModuleBase {
	public:
	ibValueMetaObjectModule(const ibString& name = ibEmptyString, const ibString& synonym = ibEmptyString, const ibString& comment = ibEmptyString)
		: ibValueMetaObjectModuleBase(name, synonym, comment)
	{
	}

	//get property
	virtual ibProperty* GetModuleProperty() const { return m_propertyModule; }

	//set module code
	virtual void SetModuleText(const ibString& moduleText) override {
		m_propertyModule->SetValue(moduleText); InvalidateBuiltFromText();
	}
	virtual ibString GetModuleText() const { return m_propertyModule->GetValueAsString(); }

protected:

	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

private:
	ibPropertyModule* m_propertyModule = ibPropertyObject::CreateProperty<ibPropertyModule>(m_categoryContext, ibT("Module"), _("Module"),
		_("The module's code: procedures, functions and variables of its owner (an object, manager, record set or command). Its event handlers are called by the platform by name; its exported methods are callable on the owner's values."));
};

class BACKEND_API ibValueMetaObjectCommonModule : public ibValueMetaObjectModuleBase {
	public:
private:

public:

	ibValueMetaObjectCommonModule(const ibString& name = ibEmptyString, const ibString& synonym = ibEmptyString, const ibString& comment = ibEmptyString);

	//support icons
	virtual ibPicture GetIcon() const;
	static ibPicture GetIconGroup();

	//events:
	virtual bool OnCreateMetaObject(ibMetaData* metaData, int flags);
	virtual bool OnLoadMetaObject(ibMetaData* metaData);
	virtual bool OnSaveMetaObject(int flags);
	virtual bool OnDeleteMetaObject();

	virtual bool OnRenameMetaObject(const ibString& sNewName);

	//module manager is started or exit 
	virtual bool OnBeforeRunMetaObject(int flags);
	virtual bool OnAfterRunMetaObject(int flags);

	virtual bool OnBeforeCloseMetaObject();
	virtual bool OnAfterCloseMetaObject();

	//get property
	virtual ibProperty* GetModuleProperty() const { return m_propertyModule; }

	//set module code
	virtual void SetModuleText(const ibString& moduleText) override {
		m_propertyModule->SetValue(moduleText); InvalidateBuiltFromText();
	}
	virtual ibString GetModuleText() const { return m_propertyModule->GetValueAsString(); }

	//prepare menu for item
	virtual bool CollectContextMenu(std::vector<ibMetaMenuItem>& items);

	// check gm
	virtual bool IsGlobalModule() const {
		return m_propertyGlobalModule->GetValueAsBoolean();
	}

	// A call into it runs in the session's trusted window, checking no access (ibRuntimeModuleDataObject::ExecAsProc).
	// Never a global module: that one is compiled into the configuration's module and called by bare name, past the
	// door that opens the window.
	virtual bool IsPrivilegedModule() const {
		return m_propertyPrivilegedModule->GetValueAsBoolean() && !IsGlobalModule();
	}

	// Manager-module flag — false on plain common module, true on
	// ibValueMetaObjectManagerModule. Replaces the explicit managerModule
	// argument that was passed to mm->AddCommonModule.
	virtual bool IsManagerModule() const { return false; }

	/**
	* Property events
	*/
	virtual bool OnPropertyChanging(ibProperty* property, const ibVariant& newValue);
	virtual void OnPropertyChanged(ibProperty* property, const ibVariant& oldValue, const ibVariant& newValue);

protected:

	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

private:
	ibPropertyModule* m_propertyModule = ibPropertyObject::CreateProperty<ibPropertyModule>(m_categoryContext, ibT("Module"), _("Module"),
		_("The common module's code: procedures and functions shared by the whole configuration. Only exported methods are visible outside it, called as ModuleName.Method(...) (or by bare name when the module is global)."));
	ibPropertyCategory* m_moduleCategory = ibPropertyObject::CreatePropertyCategory(ibT("Common module"), _("Common module"));
	ibPropertyBoolean* m_propertyGlobalModule = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_moduleCategory, ibT("GlobalModule"), _("Global module"),
		_("Whether the module's exported methods are callable by bare name from any code, without the ModuleName. prefix. Off by default: a global module's names share one namespace with every other global name."),
		false);
	ibPropertyBoolean* m_propertyPrivilegedModule = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_moduleCategory, ibT("PrivilegedModule"), _("Privileged module"),
		_("Whether a call into the module runs with no access checks - neither the rights on objects nor the record-level restrictions - for everything the call runs. For code that has to see past the user's rights, such as checking them. A module is global or privileged, not both: switching one on switches the other off."),
		false);
};

class BACKEND_API ibValueMetaObjectManagerModule : public ibValueMetaObjectCommonModule {
	public:
	ibValueMetaObjectManagerModule(const ibString& name = ibEmptyString, const ibString& synonym = ibEmptyString, const ibString& comment = ibEmptyString)
		: ibValueMetaObjectCommonModule(name, synonym, comment)
	{
	}

	//support icons
	virtual ibPicture GetIcon() const;
	static ibPicture GetIconGroup();

	virtual bool IsManagerModule() const override { return true; }

	//module manager is started or exit
	virtual bool OnBeforeRunMetaObject(int flags);
	virtual bool OnAfterRunMetaObject(int flags);

	virtual bool OnBeforeCloseMetaObject();
	virtual bool OnAfterCloseMetaObject();
};

#endif