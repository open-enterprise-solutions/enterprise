#ifndef _METAMODULE_OBJECT_H__
#define _METAMODULE_OBJECT_H__

#include "metaObject.h"

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

	//get property for grid 
	virtual wxObject* GetPGProperty() const {
		if (ibPropertyModule::ms_propertyModule != nullptr)
			return ibPropertyModule::ms_propertyModule(m_metaObject, m_propLabel, m_propName, m_propValue);
		return nullptr;
	}

	// set/get property data
	virtual bool SetDataValue(const ibValue& varPropVal) { return false; }
	virtual bool GetDataValue(ibValue& pvarPropVal) const {
		pvarPropVal = m_metaObject;
		return true;
	}

	//load & save object in control 
	virtual bool LoadData(ibReaderMemory& reader) { return m_metaObject->GetModuleProperty()->LoadData(reader); }
	virtual bool SaveData(ibWriterMemory& writer) { return m_metaObject->GetModuleProperty()->SaveData(writer); }

private:
	ibValuePtr<T> m_metaObject;
};

#pragma endregion

class BACKEND_API ibValueMetaObjectModuleBase : public ibValueMetaObject {
	wxDECLARE_ABSTRACT_CLASS(ibValueMetaObjectModuleBase);
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
	wxDECLARE_DYNAMIC_CLASS(ibValueMetaObjectModule);
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

	virtual bool LoadData(ibReaderMemory& reader);
	virtual bool SaveData(ibWriterMemory& writer);

private:
	ibPropertyModule* m_propertyModule = ibPropertyObject::CreateProperty<ibPropertyModule>(m_categoryContext, wxT("Module"), _("Module"));
};

class BACKEND_API ibValueMetaObjectCommonModule : public ibValueMetaObjectModuleBase {
	wxDECLARE_DYNAMIC_CLASS(ibValueMetaObjectCommonModule);
private:
	enum
	{
		ID_METATREE_OPEN_MODULE = 19000,
	};

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
	virtual bool PrepareContextMenu(wxMenu* defaultMenu);
	virtual void ProcessCommand(unsigned int id);

	// check gm
	virtual bool IsGlobalModule() const {
		return m_propertyGlobalModule->GetValueAsBoolean();
	}

	/**
	* Property events
	*/
	virtual bool OnPropertyChanging(ibProperty* property, const wxVariant& newValue);
	virtual void OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue);

protected:

	virtual bool LoadData(ibReaderMemory& reader);
	virtual bool SaveData(ibWriterMemory& writer);

private:
	ibPropertyModule* m_propertyModule = ibPropertyObject::CreateProperty<ibPropertyModule>(m_categoryContext, wxT("Module"), _("Module"));
	ibPropertyCategory* m_moduleCategory = ibPropertyObject::CreatePropertyCategory(wxT("Common module"), _("Common module"));
	ibPropertyBoolean* m_propertyGlobalModule = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_moduleCategory, wxT("GlobalModule"), _("Global module"), false);
};

class BACKEND_API ibValueMetaObjectManagerModule : public ibValueMetaObjectCommonModule {
	wxDECLARE_DYNAMIC_CLASS(ibValueMetaObjectManagerModule);
public:
	ibValueMetaObjectManagerModule(const wxString& name = wxEmptyString, const wxString& synonym = wxEmptyString, const wxString& comment = wxEmptyString)
		: ibValueMetaObjectCommonModule(name, synonym, comment)
	{
	}

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//module manager is started or exit 
	virtual bool OnBeforeRunMetaObject(int flags);
	virtual bool OnAfterRunMetaObject(int flags);

	virtual bool OnBeforeCloseMetaObject();
	virtual bool OnAfterCloseMetaObject();
};

#endif