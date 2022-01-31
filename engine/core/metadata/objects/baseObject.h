#ifndef _BASE_OBJECT_H__
#define _BASE_OBJECT_H__

#include "reference/reference.h"
#include "metadata/metaObjectsDefines.h"
#include "compiler/valueGuid.h"

//********************************************************************************************
//*                                     Defines                                              *
//********************************************************************************************

class IMetaTypeObjectValueSingle;

class IDataObjectSource;

class IDataObjectValue;
class IDataObjectList;

class IValueTabularSection;

//special names 
#define guidName wxT("UUID")
#define guidRef wxT("UUIDREF")

enum
{
	METAOBJECT_NORMAL = 1,
	METAOBJECT_EXTERNAL,
};

//********************************************************************************************
//*                                  Factory & metadata                                      *
//********************************************************************************************

#include "common/actionInfo.h"
#include "common/tableInfo.h"

class IMetaObjectValue
	: public IMetaObject {

	wxDECLARE_ABSTRACT_CLASS(IMetaObjectValue);

protected:

	std::vector<IMetaObject *> m_aMetaObjects;

private:

	virtual OptionList *GetFormType() = 0;

public:

	//runtime support:
	IMetaTypeObjectValueSingle *GetTypeObject(enum eMetaObjectType refType);

public:

	IMetaObjectValue();
	virtual ~IMetaObjectValue();

	//meta events
	virtual bool OnLoadMetaObject(IMetadata *metaData);
	virtual bool OnSaveMetaObject();
	virtual bool OnDeleteMetaObject();

	//module manager is started or exit 
	virtual bool OnRunMetaObject(int flags);
	virtual bool OnCloseMetaObject();

	//form events 
	virtual void OnCreateMetaForm(IMetaFormObject *metaForm) {}
	virtual void OnRemoveMetaForm(IMetaFormObject *metaForm) {}

	//base objects 
	virtual std::vector<IMetaObject *> GetObjects(const CLASS_ID &clsid) const { return IMetaObject::GetObjects(clsid); }
	virtual std::vector<IMetaObject *> GetObjects() const { return m_aMetaObjects; }

	//get attributes, form etc.. 
	virtual std::vector<IMetaAttributeObject *> GetObjectAttributes() const;
	virtual std::vector<CMetaTableObject *> GetObjectTables() const;
	virtual std::vector<CMetaFormObject *> GetObjectForms() const;
	virtual std::vector<CMetaGridObject *> GetObjectTemplates() const;

	//find attributes, tables etc 
	virtual IMetaAttributeObject *FindAttributeByName(const wxString &docPath) const
	{
		for (auto obj : GetObjectAttributes()) {
			if (docPath == obj->GetDocPath())
				return obj;
		}
		return NULL;
	}

	virtual CMetaTableObject *FindTableByName(const wxString &docPath) const
	{
		for (auto obj : GetObjectTables()) {
			if (docPath == obj->GetDocPath())
				return obj;
		}
		return NULL;
	}

	//find in current metaObject
	IMetaObject *FindMetaObjectByID(meta_identifier_t id);

	//get module object in compose object 
	virtual CMetaModuleObject *GetModuleObject() { return NULL; }
	virtual CMetaCommonModuleObject *GetModuleManager() { return NULL; }

	//Get metaObject by def id
	virtual CMetaFormObject *GetDefaultFormByID(form_identifier_t id) = 0;

	//create associate value 
	virtual IDataObjectValue *CreateObjectValue() = 0; //create object 

	//create form with data 
	virtual CValueForm *CreateObjectValue(IMetaFormObject *metaForm) = 0; //create with form

	//create in this metaObject 
	virtual void AppendChild(IMetaObject *child) { m_aMetaObjects.push_back(child); }
	virtual void RemoveChild(IMetaObject *child) {
		auto itFounded = std::find(m_aMetaObjects.begin(), m_aMetaObjects.end(), child);
		if (itFounded != m_aMetaObjects.end()) m_aMetaObjects.erase(itFounded);
	}

	//support form 
	virtual CValueForm *GetObjectForm(const wxString &formName = wxEmptyString, IValueFrame *ownerControl = NULL, const Guid &formGuid = Guid()) = 0;
	virtual CValueForm *GetGenericForm(const wxString &formName = wxEmptyString, IValueFrame *ownerControl = NULL, const Guid &formGuid = Guid());

protected:

	//support form 
	virtual CValueForm *GetObjectForm(meta_identifier_t id, IValueFrame *ownerControl, const Guid &formGuid = Guid());
	virtual CValueForm *GetGenericForm(meta_identifier_t id, IValueFrame *ownerControl, const Guid &formGuid = Guid());

protected:

	friend class CMetaFormObject;
	friend class IDataObjectValue;
};

enum
{
	OBJECT_NORMAL = 1,
	OBJECT_GROUP
};

//metaObject with reference 
class IMetaObjectRefValue : public IMetaObjectValue {
	wxDECLARE_ABSTRACT_CLASS(IMetaObjectRefValue);
public:

	IMetaObjectRefValue() : IMetaObjectValue() {}

	//meta events
	virtual bool OnLoadMetaObject(IMetadata *metaData);
	virtual bool OnDeleteMetaObject();

	virtual bool OnRunMetaObject(int flags);
	virtual bool OnCloseMetaObject();

	virtual bool ProcessChoice(IValueFrame *ownerValue, meta_identifier_t id = wxNOT_FOUND);

	//find attributes, tables etc 
	virtual CMetaEnumerationObject *FindEnumByName(const wxString &docPath) const
	{
		for (auto obj : GetObjectEnums()) {
			if (docPath == obj->GetDocPath())
				return obj;
		}
		return NULL;
	}

	//other objects
	virtual std::vector<CMetaEnumerationObject *> GetObjectEnums() const;

	//find in current metaObject
	IMetaObject *FindMetaObjectByID(meta_identifier_t id);

	//create associate value 
	virtual IDataObjectValue *CreateObjectValue() override;

	//create empty group
	virtual IDataObjectRefValue *CreateGroupObjectRefValue() { return NULL; }
	virtual IDataObjectRefValue *CreateGroupObjectRefValue(const Guid &guid) { return NULL; }

	//create empty object
	virtual IDataObjectRefValue *CreateObjectRefValue() = 0;  //create object 
	virtual IDataObjectRefValue *CreateObjectRefValue(const Guid &guid) = 0; //create object and read by guid 

	virtual CValueReference *FindObjectValue(const Guid &guid); //find by guid and ret reference 

	//ref object object
	virtual bool IsRefObject() const { return true; }

	//if object is enum return true 
	virtual bool isEnumeration() { return false; }

	//get attribute code 
	virtual IMetaAttributeObject *GetAttributeForCode() const {
		return NULL;
	}

	//searched attributes 
	virtual std::vector<IMetaAttributeObject *> GetSearchedAttributes() const = 0;

	//support form 
	virtual CValueForm *GetListForm(const wxString &formName = wxEmptyString, IValueFrame *ownerControl = NULL, const Guid &formGuid = Guid()) = 0;
	virtual CValueForm *GetSelectForm(const wxString &formName = wxEmptyString, IValueFrame *ownerControl = NULL, const Guid &formGuid = Guid()) = 0;

	//descriptions...
	virtual wxString GetDescription(const IObjectValueInfo *objValue) const = 0;

	//special functions for DB 
	virtual wxString GetTableNameDB();

	//create and update table 
	virtual bool CreateAndUpdateTableDB(IConfigMetadata *srcMetaData, IMetaObject *srcMetaObject, int flags);

	//load & save metadata from DB 
	virtual bool LoadData(CMemoryReader &reader) { return true; }
	virtual bool SaveData(CMemoryWriter &writer = CMemoryWriter()) { return true; }
	virtual bool DeleteData() { return true; }

	//process default query
	int ProcessAttribute(const wxString &tableName, IMetaAttributeObject *srcAttr, IMetaAttributeObject *dstAttr);
	int ProcessEnumeration(const wxString &tableName, meta_identifier_t id, CMetaEnumerationObject *srcEnum, CMetaEnumerationObject *dstEnum);
	int ProcessTable(const wxString &tabularName, CMetaTableObject *srcTable, CMetaTableObject *dstTable);

protected:

	//support form 
	virtual CValueForm *GetListForm(meta_identifier_t id, IValueFrame *ownerControl, const Guid &formGuid = Guid());
	virtual CValueForm *GetSelectForm(meta_identifier_t id, IValueFrame *ownerControl, const Guid &formGuid = Guid());
};

//********************************************************************************************
//*                                      Base data                                           *
//********************************************************************************************

#include "common/srcExplorer.h"

class IDataObjectSource
{
public:

	IDataObjectSource() {}
	virtual ~IDataObjectSource() {}

	//override default type object 
	virtual bool SaveModify() { return true; }

	//get metadata from object 
	virtual IMetaObjectValue *GetMetaObject() const = 0;

	//get unique identifier 
	virtual Guid GetGuid() const = 0;

	//standart override 
	virtual CMethods* GetPMethods() const = 0;
	virtual void PrepareNames() const = 0;
	virtual CValue Method(methodArg_t &aParams) = 0;

	virtual void SetAttribute(attributeArg_t &aParams, CValue &cVal) = 0;
	virtual CValue GetAttribute(attributeArg_t &aParams) = 0;

	virtual inline bool IsEmpty() const = 0;

	//support source data 
	virtual CSourceExplorer GetSourceExplorer() const = 0;
	virtual bool GetTable(IValueTable *&tableValue, meta_identifier_t id) = 0;

	//support source set/get data 
	virtual void SetValueByMetaID(meta_identifier_t id, const CValue &cVal) {};
	virtual CValue GetValueByMetaID(meta_identifier_t id) const { return CValue(); };

	//update data 
	virtual void UpdateData() {}

	//counter
	virtual void IncrRef() = 0;
	virtual void DecrRef() = 0;

	//operator 
	virtual operator CValue() const = 0;
};

//********************************************************************************************
//*                                      Object                                              *
//********************************************************************************************

#include "common/moduleInfo.h"
#include "common/valueInfo.h"

class IDataObjectValue : public CValue, public IActionSource,
	public IDataObjectSource, public IObjectValueInfo, public IModuleInfo {
	wxDECLARE_ABSTRACT_CLASS(IDataObjectValue);
public:

	//override copy constructor
	IDataObjectValue(const Guid &guid, bool newObject = true);
	IDataObjectValue(const IDataObjectValue &source);
	virtual ~IDataObjectValue();

	//support actions 
	virtual actionData_t GetActions(form_identifier_t formType) { return actionData_t(); }
	virtual void ExecuteAction(action_identifier_t action, CValueForm *srcForm) {}

	virtual bool IsObject() { return true; }

	virtual IDataObjectValue *CopyObjectValue() = 0;

	//standart override 
	virtual CMethods* GetPMethods() const { return CValue::GetPMethods(); };
	virtual void PrepareNames() const { CValue::PrepareNames(); };
	virtual CValue Method(methodArg_t &aParams) { return CValue::Method(aParams); };

	virtual void SetAttribute(attributeArg_t &aParams, CValue &cVal) { CValue::SetAttribute(aParams, cVal); };
	virtual CValue GetAttribute(attributeArg_t &aParams) { return CValue::GetAttribute(aParams); };

	//check is empty
	virtual inline bool IsEmpty() const override { return CValue::IsEmpty(); }

	//support source data 
	virtual CSourceExplorer GetSourceExplorer() const;
	virtual bool GetTable(IValueTable *&tableValue, meta_identifier_t id);

	//support source set/get data 
	virtual void SetValueByMetaID(meta_identifier_t id, const CValue &cVal);
	virtual CValue GetValueByMetaID(meta_identifier_t id) const;

	//counter
	virtual void IncrRef() { CValue::IncrRef(); }
	virtual void DecrRef() { CValue::DecrRef(); }

	//get metadata from object 
	virtual IMetaObjectValue *GetMetaObject() const = 0;

	//get unique identifier 
	virtual Guid GetGuid() const = 0;

	//get frame
	virtual CValueForm *GetFrame() const;

	//support show 
	virtual void ShowFormValue(const wxString &formName = wxEmptyString, IValueFrame *owner = NULL) = 0;
	virtual CValueForm *GetFormValue(const wxString &formName = wxEmptyString, IValueFrame *owner = NULL) = 0;

	//default showing
	virtual void ShowValue() override { ShowFormValue(); }

	//save modify 
	virtual bool SaveModify() override { return true; }

	//Get ref class 
	virtual CLASS_ID GetTypeID() const;

	virtual wxString GetTypeString() const;
	virtual wxString GetString() const;

	//operator 
	virtual operator CValue() const { return this; };

protected:

	friend class IMetaObjectValue;

protected:

	CMethods *m_methods;
};

//Object with reference type 
class IDataObjectRefValue : public IDataObjectValue {
	wxDECLARE_ABSTRACT_CLASS(IDataObjectRefValue);
protected:
	// code generator
	class CCodeGenerator {
		IMetaObjectRefValue *m_metaObject;
		IMetaAttributeObject *m_metaAttribute;
	public:
		CValue GenerateCode() const;
		meta_identifier_t GetMetaID() const { return m_metaAttribute->GetMetaID(); }

		CCodeGenerator(IMetaObjectRefValue *metaObject, IMetaAttributeObject *attribute) : m_metaObject(metaObject), m_metaAttribute(attribute) {}
	};

	CCodeGenerator *m_codeGenerator;

public:

	virtual bool InitializeObject(const IDataObjectValue *source = NULL);

	IDataObjectRefValue();
	IDataObjectRefValue(IMetaObjectRefValue *metaObject);
	IDataObjectRefValue(IMetaObjectRefValue *metaObject, const Guid &guid);
	IDataObjectRefValue(const IDataObjectRefValue &source);
	virtual ~IDataObjectRefValue();

	//Get ref class 
	virtual CLASS_ID GetTypeID() const;

	virtual wxString GetTypeString() const;
	virtual wxString GetString() const;

	//check is empty
	virtual inline bool IsEmpty() const override { return !m_objGuid.isValid(); }

	//support source data 
	virtual CSourceExplorer GetSourceExplorer() const;
	virtual bool GetTable(IValueTable *&tableValue, meta_identifier_t id);

	//get metadata from object 
	virtual IMetaObjectValue *GetMetaObject() const { return m_metaObject; };

	//set modify 
	virtual void Modify(bool mod);

	//support source set/get data 
	virtual void SetValueByMetaID(meta_identifier_t id, const CValue &cVal);
	virtual CValue GetValueByMetaID(meta_identifier_t id) const;

	//get unique identifier 
	virtual Guid GetGuid() const { return m_objGuid; }

	//copy new object
	virtual IDataObjectValue *CopyObjectValue() = 0;

	//is new object?
	virtual bool IsNewObject() const { return m_bNewObject; }

	//get reference
	virtual CValueReference *GetReference();

protected:

	virtual bool ReadInDB();
	virtual bool SaveInDB();
	virtual bool DeleteInDB();

protected:

	void PrepareEmptyObject();
	void PrepareEmptyObject(const IDataObjectValue *source);

protected:

	friend class CValueTabularRefSection;

	bool m_objModified; 
	IMetaObjectRefValue *m_metaObject;
	reference_t* m_reference_impl;
};

//********************************************************************************************
//*                                        List & select                                     *
//********************************************************************************************

#endif 