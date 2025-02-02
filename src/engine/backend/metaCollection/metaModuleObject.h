#ifndef _METAMODULEOBJECT_H__
#define _METAMODULEOBJECT_H__

#include "metaObject.h"

enum eContentHelper {
	eProcedureHelper = 1,
	eFunctionHelper,

	eUnknownHelper = 100
};

class BACKEND_API CMetaObjectModule : public IMetaObject {
	wxDECLARE_DYNAMIC_CLASS(CMetaObjectModule);
public:

	CMetaObjectModule(const wxString& name = wxEmptyString, const wxString& synonym = wxEmptyString, const wxString& comment = wxEmptyString);

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//events:
	virtual bool OnCreateMetaObject(IMetaData* metaData);
	virtual bool OnLoadMetaObject(IMetaData* metaData);
	virtual bool OnSaveMetaObject();
	virtual bool OnDeleteMetaObject();

	//module manager is started or exit 
	virtual bool OnBeforeRunMetaObject(int flags);
	virtual bool OnAfterCloseMetaObject();

	virtual void SetModuleText(const wxString& moduleText) { m_moduleData = moduleText; }
	virtual wxString GetModuleText() { return m_moduleData; }

	//set default procedures 
	void SetDefaultProcedure(const wxString& procName, const eContentHelper& contentHelper, std::vector<wxString> args = {});

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

	eContentHelper GetDefaultProcedureType(size_t idx) const {
		if (idx > m_contentHelper.size())
			return eContentHelper::eUnknownHelper;
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

	virtual bool IsGlobalModule() const {
		return false; 
	}

protected:

	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());

protected:

	wxString m_moduleData;

private:

	struct ContentData
	{
		eContentHelper m_contentType;
		std::vector<wxString> m_args;
	};

	std::map<wxString, ContentData> m_contentHelper;
};

class BACKEND_API CMetaObjectCommonModule : public CMetaObjectModule {
	wxDECLARE_DYNAMIC_CLASS(CMetaObjectCommonModule);
private:
	enum
	{
		ID_METATREE_OPEN_MODULE = 19000,
	};

protected:

	PropertyCategory* m_moduleCategory = IPropertyObject::CreatePropertyCategory("Module");
	Property* m_properyGlobalModule = IPropertyObject::CreateProperty(m_moduleCategory, "global_module", PropertyType::PT_BOOL, false);

public:

	CMetaObjectCommonModule(const wxString& name = wxEmptyString, const wxString& synonym = wxEmptyString, const wxString& comment = wxEmptyString);

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//events:
	virtual bool OnCreateMetaObject(IMetaData* metaData);
	virtual bool OnLoadMetaObject(IMetaData* metaData);
	virtual bool OnSaveMetaObject();
	virtual bool OnDeleteMetaObject();

	virtual bool OnRenameMetaObject(const wxString& sNewName);

	//module manager is started or exit 
	virtual bool OnBeforeRunMetaObject(int flags);
	virtual bool OnAfterCloseMetaObject();

	//prepare menu for item
	virtual bool PrepareContextMenu(wxMenu* defaultMenu);
	virtual void ProcessCommand(unsigned int id);

	// check gm
	virtual bool IsGlobalModule() const {
		return m_properyGlobalModule->GetValueAsBoolean();
	}

	/**
	* Property events
	*/
	virtual bool OnPropertyChanging(Property* property, const wxVariant& newValue);
	virtual void OnPropertyChanged(Property* property, const wxVariant& oldValue, const wxVariant& newValue);

protected:

	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());
};

class BACKEND_API CMetaObjectManagerModule : public CMetaObjectCommonModule {
	wxDECLARE_DYNAMIC_CLASS(CMetaObjectManagerModule);
public:
	CMetaObjectManagerModule(const wxString& name = wxEmptyString, const wxString& synonym = wxEmptyString, const wxString& comment = wxEmptyString)
		: CMetaObjectCommonModule(name, synonym, comment)
	{
	}

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//module manager is started or exit 
	virtual bool OnBeforeRunMetaObject(int flags);
	virtual bool OnAfterCloseMetaObject();
};

#endif