#ifndef _CONSTANTS_H__
#define _CONSTANTS_H__

#include "metadata/metaObjects/attributes/metaAttributeObject.h"

class CConstantObjectValue;

class CMetaConstantObject : public CMetaAttributeObject {
	wxDECLARE_DYNAMIC_CLASS(CMetaConstantObject);

	enum
	{
		ID_METATREE_OPEN_CONSTANT_MANAGER = 19000,
	};

	CMetaModuleObject *m_moduleObject;

public:

	CMetaConstantObject();
	virtual ~CMetaConstantObject();

	//get class name
	virtual wxString GetClassName() const override { return wxT("constant"); }

	//support icons
	virtual wxIcon GetIcon();
	static wxIcon GetIconGroup();

	virtual bool OnCreateMetaObject(IMetadata *metaData);
	virtual bool OnLoadMetaObject(IMetadata *metaData);
	virtual bool OnSaveMetaObject();
	virtual bool OnDeleteMetaObject();

	//module manager is started or exit 
	virtual bool OnRunMetaObject(int flags);
	virtual bool OnCloseMetaObject();

	//get table name
	static wxString GetTableNameDB() {
		return wxT("consts");
	}

	//create empty object
	virtual CConstantObjectValue *CreateObjectValue();

	//get module object in compose object 
	virtual CMetaModuleObject *GetModuleObject() { return m_moduleObject; }

	//create and update table 
	virtual bool CreateAndUpdateTableDB(IConfigMetadata *srcMetaData, IMetaObject *srcMetaObject, int flags);

	//load & save metadata from DB 
	virtual bool LoadData(CMemoryReader &reader);
	virtual bool SaveData(CMemoryWriter &writer = CMemoryWriter());
	virtual bool DeleteData();

	//read & save property
	virtual void ReadProperty() override;
	virtual void SaveProperty() override;

	friend class CConstantObjectValue;

protected:

	//prepare menu for item
	virtual bool PrepareContextMenu(wxMenu *defultMenu);
	virtual void ProcessCommand(unsigned int id);
};

#include "common/moduleInfo.h"

class CConstantObjectValue : public CValue,
	public IModuleInfo {
	virtual bool InitializeObject(const CConstantObjectValue *source = NULL);
public:

	CConstantObjectValue(const CConstantObjectValue &source);
	CConstantObjectValue(CMetaConstantObject *metaObject);

	CValue GetConstValue();
	void SetConstValue(const CValue &cValue);

protected:

	CMetaConstantObject *m_metaObject;
};

#endif