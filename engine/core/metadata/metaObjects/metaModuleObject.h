#ifndef _METAMODULEOBJECT_H__
#define _METAMODULEOBJECT_H__

#include "metaObject.h"

enum eContentHelper {
	eProcedureHelper = 1,
	eFunctionHelper,

	eUnknownHelper = 100
};

class CMetaModuleObject : public IMetaObject
{
	wxDECLARE_DYNAMIC_CLASS(CMetaModuleObject);

public:

	CMetaModuleObject(const wxString& name = wxEmptyString, const wxString& synonym = wxEmptyString, const wxString& comment = wxEmptyString);

	virtual wxString GetClassName() const override { return wxT("module"); }

	//support icons
	virtual wxIcon GetIcon();
	static wxIcon GetIconGroup();

	//events:
	virtual bool OnCreateMetaObject(IMetadata* metaData);
	virtual bool OnLoadMetaObject(IMetadata* metaData);
	virtual bool OnSaveMetaObject();
	virtual bool OnDeleteMetaObject();

	//module manager is started or exit 
	virtual bool OnBeforeRunMetaObject(int flags);
	virtual bool OnAfterCloseMetaObject();

	virtual void SetModuleText(const wxString& moduleText) { m_moduleData = moduleText; }
	virtual wxString GetModuleText() { return m_moduleData; }

	//set default procedures 
	void SetDefaultProcedure(const wxString& procname, eContentHelper contentHelper, std::vector<wxString> args = {});

	size_t GetDefaultProcedureCount() const
	{
		return m_contentHelper.size();
	}

	wxString GetDefaultProcedureName(size_t idx) const
	{
		if (idx > m_contentHelper.size())
			return wxEmptyString;

		auto it = m_contentHelper.begin();
		std::advance(it, idx);
		return it->first;
	}

	eContentHelper GetDefaultProcedureType(size_t idx) const
	{
		if (idx > m_contentHelper.size())
			return eContentHelper::eUnknownHelper;

		auto it = m_contentHelper.begin();
		std::advance(it, idx);
		return it->second.m_contentType;
	}

	std::vector<wxString> GetDefaultProcedureArgs(size_t idx) const
	{
		if (idx > m_contentHelper.size())
			return std::vector<wxString>();

		auto it = m_contentHelper.begin();
		std::advance(it, idx);
		return it->second.m_args;
	}

	virtual bool IsGlobalModule() const { return false; }

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

class CMetaCommonModuleObject : public CMetaModuleObject
{
	wxDECLARE_DYNAMIC_CLASS(CMetaCommonModuleObject);

	enum
	{
		ID_METATREE_OPEN_MODULE = 19000,
	};

	bool m_globalModule;

public:

	CMetaCommonModuleObject(const wxString& name = wxEmptyString, const wxString& synonym = wxEmptyString, const wxString& comment = wxEmptyString);

	virtual wxString GetClassName() const override { return wxT("commonModule"); }

	//support icons
	virtual wxIcon GetIcon();
	static wxIcon GetIconGroup();

	//events:
	virtual bool OnCreateMetaObject(IMetadata* metaData);
	virtual bool OnLoadMetaObject(IMetadata* metaData);
	virtual bool OnSaveMetaObject();
	virtual bool OnDeleteMetaObject();

	virtual bool OnRenameMetaObject(const wxString& sNewName);

	//module manager is started or exit 
	virtual bool OnBeforeRunMetaObject(int flags);
	virtual bool OnAfterCloseMetaObject();

	//read&save property
	virtual void ReadProperty() override;
	virtual void SaveProperty() override;

	//prepare menu for item
	virtual bool PrepareContextMenu(wxMenu* defultMenu);
	virtual void ProcessCommand(unsigned int id);

	// check gm
	virtual bool IsGlobalModule() const { return m_globalModule; }

	/**
	* Property events
	*/
	virtual bool OnPropertyChanging(Property* property, const wxVariant& oldValue);
	virtual void OnPropertyChanged(Property* property);

protected:

	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());
};

class CMetaManagerModuleObject : public CMetaCommonModuleObject {
	wxDECLARE_DYNAMIC_CLASS(CMetaManagerModuleObject);
public:
	CMetaManagerModuleObject(const wxString& name = wxEmptyString, const wxString& synonym = wxEmptyString, const wxString& comment = wxEmptyString)
		: CMetaCommonModuleObject(name, synonym, comment)
	{
	}

	virtual wxString GetClassName() const override { return wxT("module"); }

	//support icons
	virtual wxIcon GetIcon();
	static wxIcon GetIconGroup();

	//module manager is started or exit 
	virtual bool OnBeforeRunMetaObject(int flags);
	virtual bool OnAfterCloseMetaObject();
};

#endif