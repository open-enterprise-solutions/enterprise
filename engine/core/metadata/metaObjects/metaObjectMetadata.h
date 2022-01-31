#ifndef _METAOBJECT_METADATA_H__
#define _METAOBJECT_METADATA_H__

#include "metaObject.h"

//*****************************************************************************************
//*                                  metadata object                                      *
//*****************************************************************************************

#define configurationDefaultName _("ñonfiguration")

class CMetaObject : public IMetaObject
{
	wxDECLARE_DYNAMIC_CLASS(CMetaObject);

	enum
	{
		ID_METATREE_OPEN_INIT_MODULE = 19000,
	};

	OptionList *GetVersions(Property *prop) {

		OptionList *opt = new OptionList;
		opt->AddOption(_("oes 1_0_0"), version_oes_1_0_0);
		opt->AddOption(_("oes last"), version_oes_last);
		return opt;
	}

	std::vector<IMetaObject *> m_aMetaObjects;

public:

	CMetaObject();
	virtual ~CMetaObject();

	virtual wxString GetFullName() { return configurationDefaultName; }
	virtual wxString GetModuleName() { return configurationDefaultName; }

	//base objects 
	virtual std::vector<IMetaObject *> GetObjects(const CLASS_ID &clsid) const { return IMetaObject::GetObjects(clsid); }
	virtual std::vector<IMetaObject *> GetObjects() const { return m_aMetaObjects; }

	//get object class 
	virtual wxString GetClassName() const override { return wxT("metadata"); }

	//support icons
	virtual wxIcon GetIcon();
	static wxIcon GetIconGroup();

	//events
	virtual bool OnCreateMetaObject(IMetadata *metaData);
	virtual bool OnLoadMetaObject(IMetadata *metaData);
	virtual bool OnSaveMetaObject();
	virtual bool OnDeleteMetaObject();

	//module manager is started or exit 
	virtual bool OnRunMetaObject(int flags);
	virtual bool OnCloseMetaObject();

	//read&save property
	virtual void ReadProperty() override;
	virtual void SaveProperty() override;

	//prepare menu for item
	virtual bool PrepareContextMenu(wxMenu *defultMenu);
	virtual void ProcessCommand(unsigned int id);

	//create in this metaObject 
	virtual void AppendChild(IMetaObject *child) { m_aMetaObjects.push_back(child); }
	virtual void RemoveChild(IMetaObject *child) {
		auto itFounded = std::find(m_aMetaObjects.begin(), m_aMetaObjects.end(), child);
		if (itFounded != m_aMetaObjects.end()) m_aMetaObjects.erase(itFounded);
	}

public:

	virtual CMetaModuleObject *GetModuleObject() { return m_commonModule; }

protected:


	//load & save metadata from DB 
	virtual bool LoadData(CMemoryReader &reader);
	virtual bool SaveData(CMemoryWriter &writer = CMemoryWriter());

private:

	CMetaModuleObject *m_commonModule;
};

#endif 