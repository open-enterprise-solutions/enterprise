#ifndef _TABLES_H__
#define _TABLES_H__

#include "metadata/metaObjects/metaObject.h"
#include "metadata/metaObjects/attributes/metaAttributeObject.h"
#include "common/tableAttributes.h"

class CMetaTableObject : public IMetaObject,
	public ITableAttribute
{
	wxDECLARE_DYNAMIC_CLASS(CMetaTableObject);
private:
	CMetaDefaultAttributeObject* m_numberLine;
private:
	std::vector<IMetaObject*> m_aMetaObjects;
public:

	CMetaDefaultAttributeObject* GetNumberLine() const {
		return m_numberLine;
	}

	bool IsNumberLine(const meta_identifier_t& id) const {
		return id == m_numberLine->GetMetaID();
	}

	//get table class
	CLASS_ID GetTableClsid() const;

	//ctor 
	CMetaTableObject();
	virtual ~CMetaTableObject();

	//get class name
	virtual wxString GetClassName() const override {
		return wxT("tabularSection");
	}

	//support icons
	virtual wxIcon GetIcon();
	static wxIcon GetIconGroup();

	//events:
	virtual bool OnCreateMetaObject(IMetadata* metaData);
	virtual bool OnLoadMetaObject(IMetadata* metaData);
	virtual bool OnSaveMetaObject();
	virtual bool OnDeleteMetaObject();

	//for designer 
	virtual bool OnReloadMetaObject();

	//module manager is started or exit 
	virtual bool OnBeforeRunMetaObject(int flags);
	virtual bool OnAfterCloseMetaObject();

	//create in this metaObject 
	virtual void AppendChild(IMetaObject* parentObj) { m_aMetaObjects.push_back(parentObj); }
	virtual void RemoveChild(IMetaObject* child) {
		auto itFounded = std::find(m_aMetaObjects.begin(), m_aMetaObjects.end(), child);
		if (itFounded != m_aMetaObjects.end()) m_aMetaObjects.erase(itFounded);
	}

	virtual std::vector<IMetaObject*> GetObjects() const {
		return m_aMetaObjects;
	}

	//override base objects 
	virtual std::vector<IMetaAttributeObject*> GetGenericAttributes() const {
		return GetObjectAttributes();
	}

	//get attributes, form etc.. 
	virtual std::vector<IMetaAttributeObject*> GetObjectAttributes() const;

	//find attributes, tables etc 
	virtual IMetaAttributeObject* FindAttributeByName(const wxString& docPath) const
	{
		for (auto obj : GetObjectAttributes()) {
			if (docPath == obj->GetDocPath())
				return obj;
		}
		return NULL;
	}

	//find in current metaObject
	virtual IMetaAttributeObject* FindAttribute(const meta_identifier_t &id) const;

	//special functions for DB 
	virtual wxString GetTableNameDB() const {
		IMetaObject* parentMeta = GetParent();
		wxASSERT(parentMeta);
		return wxString::Format("%s%i_VT%i", parentMeta->GetClassName(), parentMeta->GetMetaID(), GetMetaID());
	}

	virtual bool HasAttributes() { return true; }
	virtual wxArrayString GetAttributes();

	//read & save property
	virtual void ReadProperty() override;
	virtual void SaveProperty() override;

protected:

	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());
};

#endif