#ifndef _BASE_OBJECT_H__
#define _BASE_OBJECT_H__

#include "reference/reference.h"
#include "metadata/metaObjectsDefines.h"
#include "compiler/valueGuid.h"

#include "common/tableAttributes.h"

#include "common/actionInfo.h"
#include "common/moduleInfo.h"
#include "common/valueInfo.h"
#include "common/tableInfo.h"

//********************************************************************************************
//*                                     Defines                                              *
//********************************************************************************************

class CMetaConstantObject;

class IMetaTypeObjectValueSingle;

class IMetaObjectWrapperData;
class IMetaObjectRecordData;
class IMetaObjectRecordDataRef;
class IMetaObjectRegisterData;

class ISourceDataObject;

class IRecordDataObject;
class IRecordDataObjectExt;
class IRecordDataObjectRef;
class IRecordDataObjectFolderRef;

class CRecordKeyObject;
class IRecordManagerObject;
class IRecordSetObject;

class CSourceExplorer;

//special names 
#define guidName wxT("UUID")
#define guidRef wxT("UUIDREF")

//********************************************************************************************
//*                                  Factory & metadata                                      *
//********************************************************************************************

class IMetaCommandData {
public:
	//get default form 
	virtual CValueForm* GetDefaultCommandForm() = 0;
};

class IMetaObjectWrapperData
	: public IMetaObject, public IMetaCommandData, public ITableAttribute {
	wxDECLARE_ABSTRACT_CLASS(IMetaObjectWrapperData);
public:

	//guid to id 
	meta_identifier_t GetIdByGuid(const Guid& guid) const;
	Guid GetGuidByID(const meta_identifier_t& id) const;

	//runtime support:
	IMetaTypeObjectValueSingle* GetTypeObject(enum eMetaObjectType refType) const;

	//find in current metaObject
	IMetaObject* FindMetaObjectByID(const meta_identifier_t& id) const;
	IMetaObject* FindMetaObjectByID(const Guid& guid) const;

	virtual bool FilterChild(const CLASS_ID& clsid) const {
		if (
			clsid == g_metaFormCLSID ||
			clsid == g_metaTemplateCLSID
			)
			return true;
		return false;
	}

	//get data selector 
	virtual eSelectorDataType GetSelectorDataType() const {
		return eSelectorDataType::eSelectorDataType_reference;
	}

	//override base objects 
	virtual std::vector<IMetaAttributeObject*> GetGenericAttributes() const = 0;
	virtual std::vector<CMetaFormObject*> GetGenericForms() const = 0;

	//find attributes, tables etc 
	virtual IMetaAttributeObject* FindGenericAttributeByName(const wxString& docPath) const {
		for (auto obj : GetGenericAttributes()) {
			if (docPath == obj->GetDocPath())
				return obj;
		}
		return NULL;
	}

	//find in current metaObject
	virtual IMetaAttributeObject* FindGenericAttribute(const meta_identifier_t& id) const;

	//form events 
	virtual void OnCreateMetaForm(IMetaFormObject* metaForm) {}
	virtual void OnRemoveMetaForm(IMetaFormObject* metaForm) {}

	//Get form type
	virtual OptionList* GetFormType() = 0;

	//create form with data 
	virtual CValueForm* CreateObjectForm(IMetaFormObject* metaForm) = 0; //create with form

	//support form 
	virtual CValueForm* GetGenericForm(const wxString& formName = wxEmptyString, IValueFrame* ownerControl = NULL, const CUniqueKey& formGuid = wxNullGuid);

protected:

	//support form 
	virtual CValueForm* GetGenericForm(const meta_identifier_t& id, IValueFrame* ownerControl, const CUniqueKey& formGuid = wxNullGuid);
};

class IMetaObjectRecordData
	: public IMetaObjectWrapperData {
	wxDECLARE_ABSTRACT_CLASS(IMetaObjectRecordData);
public:

	virtual bool FilterChild(const CLASS_ID& clsid) const {
		if (
			clsid == g_metaAttributeCLSID ||
			clsid == g_metaTableCLSID
			)
			return true;
		return IMetaObjectWrapperData::FilterChild(clsid);
	}

	//get data selector 
	virtual eSelectorDataType GetSelectorDataType() const {
		return eSelectorDataType::eSelectorDataType_any;
	}

	//get module object in compose object 
	virtual CMetaModuleObject* GetModuleObject() const {
		return NULL;
	}

	virtual CMetaCommonModuleObject* GetModuleManager() const {
		return NULL;
	}

	//meta events
	virtual bool OnLoadMetaObject(IMetadata* metaData);
	virtual bool OnSaveMetaObject();
	virtual bool OnDeleteMetaObject();

	//module manager is started or exit 
	virtual bool OnBeforeRunMetaObject(int flags);
	virtual bool OnAfterCloseMetaObject();

	//override base objects 
	virtual std::vector<IMetaAttributeObject*> GetGenericAttributes() const;
	virtual std::vector<CMetaFormObject*> GetGenericForms() const;

	//get default attributes
	virtual std::vector<IMetaAttributeObject*> GetDefaultAttributes() const {
		return std::vector<IMetaAttributeObject*>();
	}

	//get attributes, form etc.. 
	virtual std::vector<IMetaAttributeObject*> GetObjectAttributes() const;
	virtual std::vector<CMetaTableObject*> GetObjectTables() const;
	virtual std::vector<CMetaFormObject*> GetObjectForms() const;
	virtual std::vector<CMetaGridObject*> GetObjectTemplates() const;

	//find attributes, tables etc 
	virtual IMetaAttributeObject* FindDefAttributeByName(const wxString& docPath) const {
		for (auto obj : GetDefaultAttributes()) {
			if (docPath == obj->GetDocPath())
				return obj;
		}
		return NULL;
	}

	virtual IMetaAttributeObject* FindAttributeByName(const wxString& docPath) const {
		for (auto obj : GetObjectAttributes()) {
			if (docPath == obj->GetDocPath())
				return obj;
		}
		return NULL;
	}

	virtual CMetaTableObject* FindTableByName(const wxString& docPath) const {
		for (auto obj : GetObjectTables()) {
			if (docPath == obj->GetDocPath())
				return obj;
		}
		return NULL;
	}

	//create single object
	virtual IRecordDataObject* CreateRecordDataObject() = 0;

	//Get metaObject by def id
	virtual CMetaFormObject* GetDefaultFormByID(const form_identifier_t& id) = 0;

	//get default form 
	virtual CValueForm* GetDefaultCommandForm() {
		return GetObjectForm();
	};

	//support form 
	virtual CValueForm* GetObjectForm(const wxString& formName = wxEmptyString, IValueFrame* ownerControl = NULL, const CUniqueKey& formGuid = wxNullGuid) = 0;

protected:

	//support form 
	virtual CValueForm* GetObjectForm(const meta_identifier_t& id, IValueFrame* ownerControl, const CUniqueKey& formGuid = wxNullGuid);

protected:
	friend class CMetaFormObject;
	friend class IRecordDataObject;
};

enum
{
	METAOBJECT_NORMAL = 1,
	METAOBJECT_EXTERNAL,
};

//metaObject with file 
class IMetaObjectRecordDataExt : public IMetaObjectRecordData {
	wxDECLARE_ABSTRACT_CLASS(IMetaObjectRecordDataExt);
protected:
	Role* m_roleUse = IMetaObject::CreateRole({ "use", _("use") });
protected:
	//external or default dataProcessor
	int m_objMode;
public:
	//get object mode 
	int GetObjectMode() const {
		return m_objMode;
	}
	//ctor
	IMetaObjectRecordDataExt(int objMode = METAOBJECT_NORMAL);

	//create associate value 
	IRecordDataObjectExt* CreateObjectValue(); //create object
	IRecordDataObjectExt* CreateObjectValue(IRecordDataObjectExt* objSrc);

	//create single object
	virtual IRecordDataObject* CreateRecordDataObject();

protected:
	//create empty object
	virtual IRecordDataObjectExt* CreateObjectExtValue() = 0;  //create object 
};

//metaObject with reference - for enumeration 
class IMetaObjectRecordDataRef : public IMetaObjectRecordData {
	wxDECLARE_ABSTRACT_CLASS(IMetaObjectRecordDataRef);
protected:
	PropertyCategory* m_categoryData = CreatePropertyCategory({ "data", _("data") });
protected:
	//ctor
	IMetaObjectRecordDataRef();
	virtual ~IMetaObjectRecordDataRef();
public:

	virtual CMetaDefaultAttributeObject* GetDataReference() const {
		return m_attributeReference;
	}

	virtual bool IsDataReference(const meta_identifier_t& id) const {
		return id == m_attributeReference->GetMetaID();
	}

	//get data selector 
	virtual eSelectorDataType GetSelectorDataType() const {
		return eSelectorDataType::eSelectorDataType_reference;
	}

	//meta events
	virtual bool OnCreateMetaObject(IMetadata* metaData);
	virtual bool OnLoadMetaObject(IMetadata* metaData);
	virtual bool OnSaveMetaObject();
	virtual bool OnDeleteMetaObject();

	//module manager is started or exit 
	//after and before for designer 
	virtual bool OnBeforeRunMetaObject(int flags);
	virtual bool OnAfterRunMetaObject(int flags);

	virtual bool OnBeforeCloseMetaObject();
	virtual bool OnAfterCloseMetaObject();

	//create single object
	virtual IRecordDataObject* CreateRecordDataObject() {
		wxASSERT_MSG(false, "IMetaObjectRecordDataRef::CreateRecordDataObject");
		return NULL;
	}

	//process choice 
	virtual bool ProcessChoice(IValueFrame* ownerValue, 
		const meta_identifier_t& id = wxNOT_FOUND, eSelectMode selMode = eSelectMode::eSelectMode_Items);
	virtual bool ProcessListChoice(IValueFrame* ownerValue, 
		const meta_identifier_t& id = wxNOT_FOUND, eSelectMode selMode = eSelectMode::eSelectMode_Items);

	//find attributes, tables etc 
	virtual CMetaEnumerationObject* FindEnumByName(const wxString& docPath) const {
		for (auto obj : GetObjectEnums()) {
			if (docPath == obj->GetDocPath())
				return obj;
		}
		return NULL;
	}

	//override base objects 
	virtual std::vector<IMetaAttributeObject*> GetGenericAttributes() const;
	virtual std::vector<CMetaFormObject*> GetGenericForms() const;

	//get attribute code 
	virtual IMetaAttributeObject* GetAttributeForCode() const {
		return NULL;
	}

	//override base objects 
	virtual std::vector<IMetaAttributeObject*> GetDefaultAttributes() const = 0;
	//other objects
	virtual std::vector<CMetaEnumerationObject*> GetObjectEnums() const;
	//searched attributes 
	virtual std::vector<IMetaAttributeObject*> GetSearchedAttributes() const = 0;

	//get default form 
	virtual CValueForm* GetDefaultCommandForm() {
		return GetListForm();
	};

	virtual CReferenceDataObject* FindObjectValue(const Guid& guid); //find by guid and ret reference 

	//support form 
	virtual CValueForm* GetListForm(const wxString& formName = wxEmptyString, IValueFrame* ownerControl = NULL, const CUniqueKey& formGuid = wxNullGuid) = 0;
	virtual CValueForm* GetSelectForm(const wxString& formName = wxEmptyString, IValueFrame* ownerControl = NULL, const CUniqueKey& formGuid = wxNullGuid) = 0;

	//descriptions...
	virtual wxString GetDescription(const IObjectValueInfo* objValue) const = 0;

	//special functions for DB 
	virtual wxString GetTableNameDB() const;

	//create and update table 
	virtual bool CreateAndUpdateTableDB(class IConfigMetadata* srcMetaData, IMetaObject* srcMetaObject, int flags);

	//load & save metadata from DB 
	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());
	virtual bool DeleteData() { return true; }

	//process default query
	int ProcessAttribute(const wxString& tableName, IMetaAttributeObject* srcAttr, IMetaAttributeObject* dstAttr);
	int ProcessEnumeration(const wxString& tableName, const meta_identifier_t& id, CMetaEnumerationObject* srcEnum, CMetaEnumerationObject* dstEnum);
	int ProcessTable(const wxString& tabularName, CMetaTableObject* srcTable, CMetaTableObject* dstTable);

protected:

	//support form 
	virtual CValueForm* GetListForm(const meta_identifier_t& id, IValueFrame* ownerControl, const CUniqueKey& formGuid = wxNullGuid);
	virtual CValueForm* GetSelectForm(const meta_identifier_t& id, IValueFrame* ownerControl, const CUniqueKey& formGuid = wxNullGuid);

protected:

	CMetaDefaultAttributeObject* m_attributeReference;
};

//metaObject with reference and deletion mark 
class IMetaObjectRecordDataMutableRef : public IMetaObjectRecordDataRef {
	wxDECLARE_ABSTRACT_CLASS(IMetaObjectRecordDataMutableRef);
protected:
	struct genData_t {
		std::set<meta_identifier_t> m_data;
	public:

		bool LoadData(CMemoryReader& dataReader);
		bool LoadFromVariant(const wxVariant& variant);
		bool SaveData(CMemoryWriter& dataWritter);
		void SaveToVariant(wxVariant& variant, IMetadata* metaData) const;
	};
	genData_t m_genData;
private:
	Role* m_roleRead = IMetaObject::CreateRole({ "read", _("read") });
	Role* m_roleInsert = IMetaObject::CreateRole({ "insert", _("insert") });
	Role* m_roleUpdate = IMetaObject::CreateRole({ "update", _("update") });
	Role* m_roleDelete = IMetaObject::CreateRole({ "delete", _("delete") });
protected:
	Property* m_propertyGeneration = IPropertyObject::CreateGenerationProperty(m_categoryData, { "baseOn", _("base on") });
private:
	CMetaDefaultAttributeObject* m_attributeDeletionMark;
protected:
	//ctor
	IMetaObjectRecordDataMutableRef();
	virtual ~IMetaObjectRecordDataMutableRef();
public:

	CMetaDefaultAttributeObject* GetDataDeletionMark() const {
		return m_attributeDeletionMark;
	}

	bool IsDataDeletionMark(const meta_identifier_t& id) const {
		return id == m_attributeDeletionMark->GetMetaID();
	}

	std::set<meta_identifier_t> GetGenMetaId() const {
		return m_genData.m_data;
	}

	//events: 
	virtual bool OnCreateMetaObject(IMetadata* metaData);
	virtual bool OnLoadMetaObject(IMetadata* metaData);
	virtual bool OnSaveMetaObject();
	virtual bool OnDeleteMetaObject();

	//module manager is started or exit 
	virtual bool OnBeforeRunMetaObject(int flags);
	virtual bool OnAfterRunMetaObject(int flags);

	virtual bool OnBeforeCloseMetaObject();
	virtual bool OnAfterCloseMetaObject();

	//create associate value 
	IRecordDataObjectRef* CreateObjectValue();
	IRecordDataObjectRef* CreateObjectValue(const Guid& guid);
	IRecordDataObjectRef* CreateObjectValue(IRecordDataObjectRef* objSrc, bool generate = false);

	//create single object
	virtual IRecordDataObject* CreateRecordDataObject();

protected:

	/**
	* Property events
	*/
	virtual void OnPropertyCreated(Property* property);
	virtual bool OnPropertyChanging(Property* property, const wxVariant& newValue);
	virtual void OnPropertyChanged(Property* property);

	//load & save metadata from DB 
	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());

	//create empty object
	virtual IRecordDataObjectRef* CreateObjectRefValue(const Guid& objGuid = wxNullGuid) = 0; //create object and read by guid 
};

#include "objectEnum.h"

//metaObject with reference and deletion mark and group/object type
class IMetaObjectRecordDataFolderMutableRef : public IMetaObjectRecordDataMutableRef {
	wxDECLARE_ABSTRACT_CLASS(IMetaObjectRecordDataFolderMutableRef);
public:

	CMetaDefaultAttributeObject* GetDataCode() const {
		return m_attributeCode;
	}

	virtual bool IsDataCode(const meta_identifier_t& id) const {
		return id == m_attributeCode->GetMetaID();
	}

	CMetaDefaultAttributeObject* GetDataDescription() const {
		return m_attributeDescription;
	}

	virtual bool IsDataDescription(const meta_identifier_t& id) const {
		return id == m_attributeDescription->GetMetaID();
	}

	CMetaDefaultAttributeObject* GetDataParent() const {
		return m_attributeParent;
	}

	virtual bool IsDataParent(const meta_identifier_t& id) const {
		return id == m_attributeParent->GetMetaID();
	}

	CMetaDefaultAttributeObject* GetDataIsFolder() const {
		return m_attributeIsFolder;
	}

	virtual bool IsDataFolder(const meta_identifier_t& id) const {
		return id == m_attributeIsFolder->GetMetaID();
	}

	IMetaObjectRecordDataFolderMutableRef();
	virtual ~IMetaObjectRecordDataFolderMutableRef();

	//process choice 
	virtual bool ProcessChoice(IValueFrame* ownerValue,
		const meta_identifier_t& id = wxNOT_FOUND, eSelectMode selMode = eSelectMode::eSelectMode_Items);
	virtual bool ProcessListChoice(IValueFrame* ownerValue,
		const meta_identifier_t& id = wxNOT_FOUND, eSelectMode selMode = eSelectMode::eSelectMode_Items);

	class CMetaGroupAttributeObject : public CMetaAttributeObject {
		wxDECLARE_DYNAMIC_CLASS(CMetaGroupAttributeObject);
	private:
		OptionList* GetUseItem(PropertyOption*) {
			OptionList* opt_list = new OptionList;
			opt_list->AddOption(_("for item"), eUseItem_Item);
			opt_list->AddOption(_("for folder"), eUseItem_Folder);
			opt_list->AddOption(_("for folder and item"), eUseItem_Folder_Item);
			return opt_list;
		}
	protected:
		PropertyCategory* m_categoryGroup = IPropertyObject::CreatePropertyCategory({ "group", _("group") });
		Property* m_propertyUse = IPropertyObject::CreateProperty(m_categoryGroup, { "use",  "use" }, &CMetaGroupAttributeObject::GetUseItem, eUseItem::eUseItem_Item);
	public:
		eUseItem GetAttrUse() const {
			return (eUseItem)m_propertyUse->GetValueAsInteger();
		}
	protected:
		virtual bool LoadData(CMemoryReader& reader);
		virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());
	};

	class CMetaGroupTableObject : public CMetaTableObject {
		wxDECLARE_DYNAMIC_CLASS(CMetaGroupTableObject);
	private:
		OptionList* GetUseItem(PropertyOption*) {
			OptionList* opt_list = new OptionList;
			opt_list->AddOption(_("for item"), eUseItem_Item);
			opt_list->AddOption(_("for folder"), eUseItem_Folder);
			opt_list->AddOption(_("for folder and item"), eUseItem_Folder_Item);
			return opt_list;
		}
	protected:
		PropertyCategory* m_categoryGroup = IPropertyObject::CreatePropertyCategory({ "group", _("group") });
		Property* m_propertyUse = IPropertyObject::CreateProperty(m_categoryGroup, { "use",  "use" }, &CMetaGroupTableObject::GetUseItem, eUseItem::eUseItem_Item);
	public:
		eUseItem GetTableUse() const {
			return (eUseItem)m_propertyUse->GetValueAsInteger();
		}
	protected:
		virtual bool LoadData(CMemoryReader& reader);
		virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());
	};

	virtual bool FilterChild(const CLASS_ID& clsid) const {
		if (
			clsid == g_metaFolderAttributeCLSID ||
			clsid == g_metaFolderTableCLSID
			)
			return true;
		return IMetaObjectWrapperData::FilterChild(clsid);
	}

	//events: 
	virtual bool OnCreateMetaObject(IMetadata* metaData);
	virtual bool OnLoadMetaObject(IMetadata* metaData);
	virtual bool OnSaveMetaObject();
	virtual bool OnDeleteMetaObject();

	//module manager is started or exit 
	virtual bool OnBeforeRunMetaObject(int flags);
	virtual bool OnAfterRunMetaObject(int flags);

	virtual bool OnBeforeCloseMetaObject();
	virtual bool OnAfterCloseMetaObject();

	//create associate value 	
	IRecordDataObjectFolderRef* CreateObjectValue(eObjectMode mode);
	IRecordDataObjectFolderRef* CreateObjectValue(eObjectMode mode, const Guid& guid);
	IRecordDataObjectFolderRef* CreateObjectValue(eObjectMode mode, IRecordDataObjectRef* objSrc, bool generate = false);

	//support form 
	virtual CValueForm* GetFolderForm(const wxString& formName = wxEmptyString, IValueFrame* ownerControl = NULL, const CUniqueKey& formGuid = wxNullGuid) = 0;
	virtual CValueForm* GetFolderSelectForm(const wxString& formName = wxEmptyString, IValueFrame* ownerControl = NULL, const CUniqueKey& formGuid = wxNullGuid) = 0;

protected:

	//load & save metadata from DB 
	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());

	//support form 
	virtual CValueForm* GetFolderForm(const meta_identifier_t& id, IValueFrame* ownerControl, const CUniqueKey& formGuid = wxNullGuid);
	virtual CValueForm* GetFolderSelectForm(const meta_identifier_t& id, IValueFrame* ownerControl, const CUniqueKey& formGuid = wxNullGuid);

	//create empty object
	virtual IRecordDataObjectFolderRef* CreateObjectRefValue(eObjectMode mode, const Guid& objGuid = wxNullGuid) = 0; //create object and read by guid 
	virtual IRecordDataObjectRef* CreateObjectRefValue(const Guid& objGuid = wxNullGuid) final;

protected:

	//default attributes 
	CMetaDefaultAttributeObject* m_attributeCode;
	CMetaDefaultAttributeObject* m_attributeDescription;
	CMetaDefaultAttributeObject* m_attributeParent;
	CMetaDefaultAttributeObject* m_attributeIsFolder;
};

//metaObject with key   
class IMetaObjectRegisterData :
	public IMetaObjectWrapperData {
	wxDECLARE_ABSTRACT_CLASS(IMetaObjectRegisterData);
private:
	Role* m_roleRead = IMetaObject::CreateRole({ "read", _("read") });
	Role* m_roleUpdate = IMetaObject::CreateRole({ "update", _("update") });
protected:
	IMetaObjectRegisterData();
	virtual ~IMetaObjectRegisterData();
public:

	CMetaDefaultAttributeObject* GetRegisterActive() const {
		return m_attributeLineActive;
	}

	bool IsRegisterActive(const meta_identifier_t& id) const {
		return id == m_attributeLineActive->GetMetaID();
	}

	CMetaDefaultAttributeObject* GetRegisterPeriod() const {
		return m_attributePeriod;
	}

	bool IsRegisterPeriod(const meta_identifier_t& id) const {
		return id == m_attributePeriod->GetMetaID();
	}

	CMetaDefaultAttributeObject* GetRegisterRecorder() const {
		return m_attributeRecorder;
	}

	bool IsRegisterRecorder(const meta_identifier_t& id) const {
		return id == m_attributeRecorder->GetMetaID();
	}

	CMetaDefaultAttributeObject* GetRegisterLineNumber() const {
		return m_attributeLineNumber;
	}

	bool IsRegisterLineNumber(const meta_identifier_t& id) const {
		return id == m_attributeLineNumber->GetMetaID();
	}

	///////////////////////////////////////////////////////////////////

	//override base objects 
	virtual std::vector<IMetaAttributeObject*> GetGenericAttributes() const;
	virtual std::vector<CMetaFormObject*> GetGenericForms() const;

	//get default attributes
	virtual std::vector<IMetaAttributeObject*> GetDefaultAttributes() const = 0;

	//get attributes, form etc.. 
	virtual std::vector<IMetaAttributeObject*> GetObjectDimensions() const;
	virtual std::vector<IMetaAttributeObject*> GetObjectResources() const;
	virtual std::vector<IMetaAttributeObject*> GetObjectAttributes() const;

	//get dimension keys 
	virtual std::vector<IMetaAttributeObject*> GetGenericDimensions() const = 0;

	virtual std::vector<CMetaFormObject*> GetObjectForms() const;
	virtual std::vector<CMetaGridObject*> GetObjectTemplates() const;

	//find attributes, tables etc 
	virtual IMetaAttributeObject* FindDefAttributeByName(const wxString& docPath) const {
		for (auto obj : GetDefaultAttributes()) {
			if (docPath == obj->GetDocPath())
				return obj;
		}
		return NULL;
	}

	virtual IMetaAttributeObject* FindDimensionByName(const wxString& docPath) const {
		for (auto obj : GetObjectDimensions()) {
			if (docPath == obj->GetDocPath())
				return obj;
		}
		return NULL;
	}

	virtual IMetaAttributeObject* FindResourceByName(const wxString& docPath) const {
		for (auto obj : GetObjectResources()) {
			if (docPath == obj->GetDocPath())
				return obj;
		}

		return NULL;
	}

	virtual IMetaAttributeObject* FindAttributeByName(const wxString& docPath) const {
		for (auto obj : GetObjectAttributes()) {
			if (docPath == obj->GetDocPath())
				return obj;
		}
		return NULL;
	}

	//find in current metaObject
	virtual IMetaAttributeObject* FindAttribute(const meta_identifier_t& id) const;

	//has record manager 
	virtual bool HasRecordManager() const {
		return false;
	}

	//has recorder 
	virtual bool HasRecorder() const {
		return true;
	}

	IRecordSetObject* CreateRecordSetObjectValue(bool needInitialize = true);
	IRecordSetObject* CreateRecordSetObjectValue(const CUniquePairKey& uniqueKey, bool needInitialize = true);
	IRecordSetObject* CreateRecordSetObjectValue(IRecordSetObject* source, bool needInitialize = true);

	IRecordManagerObject* CreateRecordManagerObjectValue();
	IRecordManagerObject* CreateRecordManagerObjectValue(const CUniquePairKey& uniqueKey);
	IRecordManagerObject* CreateRecordManagerObjectValue(IRecordManagerObject* source);

	//get module object in compose object 
	virtual CMetaModuleObject* GetModuleObject() const { return NULL; }
	virtual CMetaCommonModuleObject* GetModuleManager() const { return NULL; }

	//Get metaObject by def id
	virtual CMetaFormObject* GetDefaultFormByID(const form_identifier_t& id) = 0;

	virtual bool FilterChild(const CLASS_ID& clsid) const {
		if (
			clsid == g_metaDimensionCLSID ||
			clsid == g_metaResourceCLSID ||
			clsid == g_metaAttributeCLSID
			)
			return true;
		return IMetaObjectWrapperData::FilterChild(clsid);
	}


	virtual wxString GetClassName() const {
		return wxT("register");
	}

	//meta events
	virtual bool OnCreateMetaObject(IMetadata* metaData);
	virtual bool OnLoadMetaObject(IMetadata* metaData);
	virtual bool OnSaveMetaObject();
	virtual bool OnDeleteMetaObject();

	//module manager is started or exit 
	virtual bool OnBeforeRunMetaObject(int flags);
	virtual bool OnAfterCloseMetaObject();

	//get default form 
	virtual CValueForm* GetDefaultCommandForm() {
		return GetListForm();
	};

	//support form 
	virtual CValueForm* GetListForm(const wxString& formName = wxEmptyString, IValueFrame* ownerControl = NULL, const CUniqueKey& formGuid = wxNullGuid) = 0;

	//special functions for DB 
	virtual wxString GetTableNameDB() const;

	//create and update table 
	virtual bool CreateAndUpdateTableDB(IConfigMetadata* srcMetaData, IMetaObject* srcMetaObject, int flags);

	//process default query
	int ProcessDimension(const wxString& tableName, IMetaAttributeObject* srcAttr, IMetaAttributeObject* dstAttr);
	int ProcessResource(const wxString& tableName, IMetaAttributeObject* srcAttr, IMetaAttributeObject* dstAttr);
	int ProcessAttribute(const wxString& tableName, IMetaAttributeObject* srcAttr, IMetaAttributeObject* dstAttr);

protected:

	bool UpdateCurrentRecords(const wxString& tableName, IMetaObjectRegisterData* dst);

	///////////////////////////////////////////////////////////////////

	virtual IRecordSetObject* CreateRecordSetObjectRegValue(const CUniquePairKey& uniqueKey = wxNullUniquePairKey) = 0;
	virtual IRecordManagerObject* CreateRecordManagerObjectRegValue(const CUniquePairKey& uniqueKey = wxNullUniquePairKey) { return NULL; }

	//load & save metadata from DB 
	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());
	virtual bool DeleteData() { return true; }

protected:

	//support form 
	virtual CValueForm* GetListForm(const meta_identifier_t& id, IValueFrame* ownerControl, const CUniqueKey& formGuid = wxNullGuid);

protected:

	//default attributes 
	CMetaDefaultAttributeObject* m_attributePeriod;
	CMetaDefaultAttributeObject* m_attributeRecorder;
	CMetaDefaultAttributeObject* m_attributeLineNumber;
	CMetaDefaultAttributeObject* m_attributeLineActive;
};

//********************************************************************************************
//*                                      Base data                                           *
//********************************************************************************************

#include "common/srcExplorer.h"

class ISourceDataObject
{
public:

	ISourceDataObject() {}
	virtual ~ISourceDataObject() {}

	//override default type object 
	virtual bool SaveModify() {
		return true;
	}

	//get metadata from object 
	virtual IMetaObjectWrapperData* GetMetaObject() const = 0;

	//get unique identifier 
	virtual CUniqueKey GetGuid() const = 0;

	//standart override 
	virtual CMethods* GetPMethods() const = 0;
	virtual void PrepareNames() const = 0;
	virtual CValue Method(methodArg_t& aParams) = 0;

	virtual void SetAttribute(attributeArg_t& aParams, CValue& cVal) = 0;
	virtual CValue GetAttribute(attributeArg_t& aParams) = 0;

	virtual inline bool IsEmpty() const = 0;

	//support source data 
	virtual CSourceExplorer GetSourceExplorer() const = 0;
	virtual bool GetModel(IValueModel*& tableValue, const meta_identifier_t& id) = 0;

	//support source set/get data 
	virtual void SetValueByMetaID(const meta_identifier_t& id, const CValue& cVal) {}
	virtual CValue GetValueByMetaID(const meta_identifier_t& id) const {
		return CValue();
	}

	//counter
	virtual void IncrRef() = 0;
	virtual void DecrRef() = 0;

	//Get ref class 
	virtual CLASS_ID GetClassType() const = 0;

	virtual wxString GetTypeString() const = 0;
	virtual wxString GetString() const = 0;

	//operator 
	virtual operator CValue() const = 0;
};

//********************************************************************************************
//*                                      Object                                              *
//********************************************************************************************

#pragma region objects 
//Object with metaobject 
class IRecordDataObject : public CValue, public IActionSource,
	public ISourceDataObject, public IObjectValueInfo, public IModuleInfo {
	wxDECLARE_ABSTRACT_CLASS(IRecordDataObject);
protected:
	//override copy constructor
	IRecordDataObject(const Guid& objGuid, bool newObject);
	IRecordDataObject(const IRecordDataObject& source);
public:
	virtual ~IRecordDataObject();

	//support actions 
	virtual actionData_t GetActions(const form_identifier_t& formType) { return actionData_t(); }
	virtual void ExecuteAction(const action_identifier_t& action, CValueForm* srcForm) {}

	virtual IRecordDataObject* CopyObjectValue() = 0;

	//standart override 
	virtual CMethods* GetPMethods() const {
		return CValue::GetPMethods();
	}

	virtual void PrepareNames() const {
		CValue::PrepareNames();
	}

	virtual CValue Method(methodArg_t& aParams) {
		return CValue::Method(aParams);
	}

	virtual void SetAttribute(attributeArg_t& aParams, CValue& cVal) {
		CValue::SetAttribute(aParams, cVal);
	}

	virtual CValue GetAttribute(attributeArg_t& aParams) {
		return CValue::GetAttribute(aParams);
	}

	//check is empty
	virtual inline bool IsEmpty() const override {
		return CValue::IsEmpty();
	}

	//support source data 
	virtual CSourceExplorer GetSourceExplorer() const;
	virtual bool GetModel(IValueModel*& tableValue, const meta_identifier_t& id);

	//support source set/get data 
	virtual void SetValueByMetaID(const meta_identifier_t& id, const CValue& cVal);
	virtual CValue GetValueByMetaID(const meta_identifier_t& id) const;

	//counter
	virtual void IncrRef() { CValue::IncrRef(); }
	virtual void DecrRef() { CValue::DecrRef(); }

	//get metadata from object 
	virtual IMetaObjectRecordData* GetMetaObject() const = 0;

	//get unique identifier 
	virtual CUniqueKey GetGuid() const = 0;

	//get frame
	virtual CValueForm* GetForm() const;

	//support show 
	virtual void ShowFormValue(const wxString& formName = wxEmptyString, IValueFrame* owner = NULL) = 0;
	virtual CValueForm* GetFormValue(const wxString& formName = wxEmptyString, IValueFrame* owner = NULL) = 0;

	//default showing
	virtual void ShowValue() override {
		ShowFormValue();
	}

	//save modify 
	virtual bool SaveModify() override {
		return true;
	}

	//Get ref class 
	virtual CLASS_ID GetClassType() const;

	virtual wxString GetTypeString() const;
	virtual wxString GetString() const;

	//operator 
	virtual operator CValue() const {
		return this;
	}

protected:
	virtual void PrepareEmptyObject();
protected:
	friend class IMetaObjectRecordData;
protected:
	CMethods* m_methods;
};

//Object with file
class IRecordDataObjectExt : public IRecordDataObject {
	wxDECLARE_ABSTRACT_CLASS(IRecordDataObjectExt);
protected:
	//override copy constructor
	IRecordDataObjectExt(IMetaObjectRecordDataExt* metaObject);
	IRecordDataObjectExt(const IRecordDataObjectExt& source);
public:
	virtual ~IRecordDataObjectExt();
	virtual bool InitializeObject();
	virtual bool InitializeObject(IRecordDataObjectExt* source);

	//get unique identifier 
	virtual CUniqueKey GetGuid() const {
		return m_objGuid;
	}

	//check is empty
	virtual inline bool IsEmpty() const override {
		return false;
	}

	//copy new object
	virtual IRecordDataObjectExt* CopyObjectValue();

	//get metadata from object 
	virtual IMetaObjectRecordDataExt* GetMetaObject() const {
		return m_metaObject;
	}
protected:
	IMetaObjectRecordDataExt* m_metaObject;
};

//Object with reference type 
class IRecordDataObjectRef : public IRecordDataObject {
	wxDECLARE_ABSTRACT_CLASS(IRecordDataObjectRef);
protected:

	// code generator
	class CCodeGenerator {
		IMetaObjectRecordDataMutableRef* m_metaObject;
		IMetaAttributeObject* m_metaAttribute;
	public:
		CValue GenerateCode() const;
		meta_identifier_t GetMetaID() const {
			return m_metaAttribute->GetMetaID();
		}
		CCodeGenerator(IMetaObjectRecordDataMutableRef* metaObject, IMetaAttributeObject* attribute) : m_metaObject(metaObject), m_metaAttribute(attribute) {}
	};

	CCodeGenerator* m_codeGenerator;

protected:
	IRecordDataObjectRef(IMetaObjectRecordDataMutableRef* metaObject, const Guid& objGuid);
	IRecordDataObjectRef(const IRecordDataObjectRef& src);
public:
	virtual ~IRecordDataObjectRef();

	virtual bool InitializeObject();
	virtual bool InitializeObject(IRecordDataObjectRef* source, bool generate = false);

	//Get ref class 
	virtual CLASS_ID GetClassType() const;

	virtual wxString GetTypeString() const;
	virtual wxString GetString() const;

	//check is empty
	virtual inline bool IsEmpty() const override {
		return !m_objGuid.isValid();
	}

	//support source data 
	virtual CSourceExplorer GetSourceExplorer() const;
	virtual bool GetModel(IValueModel*& tableValue, const meta_identifier_t& id);

	//get metadata from object 
	virtual IMetaObjectRecordDataMutableRef* GetMetaObject() const {
		return m_metaObject;
	};

	//set modify 
	virtual void Modify(bool mod);

	//default methods
	virtual bool Generate();

	//filling object 
	virtual bool Filling(CValue& cValue = CValue()) const;

	//support source set/get data 
	virtual void SetValueByMetaID(const meta_identifier_t& id, const CValue& cVal);
	virtual CValue GetValueByMetaID(const meta_identifier_t& id) const;

	//get unique identifier 
	virtual CUniqueKey GetGuid() const {
		return m_objGuid;
	}

	//copy new object
	virtual IRecordDataObjectRef* CopyObjectValue();

	//is new object?
	virtual bool IsNewObject() const {
		return m_newObject;
	}

	//get reference
	virtual CReferenceDataObject* GetReference() const;

protected:
	void SetDeletionMark(bool deletionMark = true);
protected:
	virtual bool ReadData();
	virtual bool SaveData();
	virtual bool DeleteData();
protected:
	virtual void PrepareEmptyObject();
	virtual void PrepareEmptyObject(const IRecordDataObjectRef* source);
protected:

	friend class CTabularSectionDataObjectRef;

	bool m_objModified;
	IMetaObjectRecordDataMutableRef* m_metaObject;
	reference_t* m_reference_impl;
};

//Object with reference type and group/object type 
class IRecordDataObjectFolderRef : public IRecordDataObjectRef {
	wxDECLARE_ABSTRACT_CLASS(IRecordDataObjectFolderRef);
protected:
	IRecordDataObjectFolderRef(IMetaObjectRecordDataFolderMutableRef* metaObject, const Guid& objGuid, eObjectMode objMode = eObjectMode::OBJECT_ITEM);
	IRecordDataObjectFolderRef(const IRecordDataObjectFolderRef& src);
public:
	virtual ~IRecordDataObjectFolderRef();

	//support source data 
	virtual CSourceExplorer GetSourceExplorer() const;
	virtual bool GetModel(IValueModel*& tableValue, const meta_identifier_t& id);

	//get metadata from object 
	virtual IMetaObjectRecordDataFolderMutableRef* GetMetaObject() const {
		return (IMetaObjectRecordDataFolderMutableRef*)m_metaObject;
	};

	//copy new object
	virtual IRecordDataObjectRef* CopyObjectValue();

	//support source set/get data 
	virtual void SetValueByMetaID(const meta_identifier_t& id, const CValue& cVal);
	virtual CValue GetValueByMetaID(const meta_identifier_t& id) const;

protected:
	virtual bool ReadData();
protected:
	virtual void PrepareEmptyObject();
	virtual void PrepareEmptyObject(const IRecordDataObjectRef* source);
protected:
	//folder or object catalog
	eObjectMode m_objMode;
};
#pragma endregion

//Object with register type 
#pragma region registers 
class CRecordKeyObject : public CValue {
	wxDECLARE_ABSTRACT_CLASS(CRecordKeyObject);
public:
	CRecordKeyObject(IMetaObjectRegisterData* metaObject);
	virtual ~CRecordKeyObject();

	//standart override 
	virtual CMethods* GetPMethods() const;

	virtual void PrepareNames() const;
	virtual CValue Method(methodArg_t& aParams);

	virtual void SetAttribute(attributeArg_t& aParams, CValue& cVal);
	virtual CValue GetAttribute(attributeArg_t& aParams);

	//check is empty
	virtual inline bool IsEmpty() const override;

	//Get unique key 
	CUniquePairKey GetUniqueKey() {
		return CUniquePairKey(m_metaObject, m_keyValues);
	}

	//get metadata from object 
	virtual IMetaObjectRegisterData* GetMetaObject() const {
		return m_metaObject;
	};

	//Get ref class 
	virtual CLASS_ID GetClassType() const;

	virtual wxString GetTypeString() const;
	virtual wxString GetString() const;

protected:
	IMetaObjectRegisterData* m_metaObject;
	CMethods* m_methods;

	std::map<meta_identifier_t, CValue> m_keyValues;
};

class IRecordSetObject : public IValueTable, public IModuleInfo {
	wxDECLARE_ABSTRACT_CLASS(IRecordSetObject);
public:

	virtual IValueModelColumnCollection* GetColumnCollection() const {
		return m_dataColumnCollection;
	}

	virtual IValueModelReturnLine* GetRowAt(const wxDataViewItem& line) {
		if (!line.IsOk())
			return NULL;
		return new CRecordSetRegisterReturnLine(this, line);
	}

	class CRecordSetRegisterColumnCollection : public IValueTable::IValueModelColumnCollection {
		wxDECLARE_DYNAMIC_CLASS(CRecordSetRegisterColumnCollection);
	public:

		class CValueRecordSetRegisterColumnInfo : public IValueTable::IValueModelColumnCollection::IValueModelColumnInfo {
			wxDECLARE_DYNAMIC_CLASS(CValueRecordSetRegisterColumnInfo);
			IMetaAttributeObject* m_metaAttribute;
		public:

			virtual unsigned int GetColumnID() const { return m_metaAttribute->GetMetaID(); }
			virtual wxString GetColumnName() const { return m_metaAttribute->GetName(); }
			virtual wxString GetColumnCaption() const { return m_metaAttribute->GetSynonym(); }
			virtual CValueTypeDescription* GetColumnTypes() const { return m_metaAttribute->GetValueTypeDescription(); }
			virtual int GetColumnWidth() const { return 0; }

			CValueRecordSetRegisterColumnInfo();
			CValueRecordSetRegisterColumnInfo(IMetaAttributeObject* metaAttribute);
			virtual ~CValueRecordSetRegisterColumnInfo();

			virtual wxString GetTypeString() const {
				return wxT("recordSetRegisterColumnInfo");
			}

			virtual wxString GetString() const {
				return wxT("recordSetRegisterColumnInfo");
			}

			friend CRecordSetRegisterColumnCollection;
		};

	public:

		CRecordSetRegisterColumnCollection();
		CRecordSetRegisterColumnCollection(IRecordSetObject* ownerTable);
		virtual ~CRecordSetRegisterColumnCollection();

		virtual CValueTypeDescription* GetColumnTypes(unsigned int col) const
		{
			CValueRecordSetRegisterColumnInfo* columnInfo = m_columnInfo.at(col);
			wxASSERT(columnInfo);
			return columnInfo->GetColumnTypes();
		}

		virtual IValueModelColumnInfo* GetColumnInfo(unsigned int idx) const
		{
			if (m_columnInfo.size() < idx)
				return NULL;

			auto foundedIt = m_columnInfo.begin();
			std::advance(foundedIt, idx);
			return foundedIt->second;
		}

		virtual unsigned int GetColumnCount() const {
			IMetaObjectWrapperData* metaTable = m_ownerTable->GetMetaObject();
			wxASSERT(metaTable);
			auto attributes =
				metaTable->GetGenericAttributes();
			return attributes.size();
		}

		virtual wxString GetTypeString() const {
			return wxT("recordSetRegisterColumn");
		}

		virtual wxString GetString() const {
			return wxT("recordSetRegisterColumn");
		}

		//array support 
		virtual CValue GetAt(const CValue& cKey);
		virtual void SetAt(const CValue& cKey, CValue& cVal);

		friend class IRecordSetObject;

	protected:

		IRecordSetObject* m_ownerTable;
		CMethods* m_methods;

		std::map<meta_identifier_t, CValueRecordSetRegisterColumnInfo*> m_columnInfo;
	};

	class CRecordSetRegisterReturnLine : public IValueModelReturnLine {
		wxDECLARE_DYNAMIC_CLASS(CRecordSetRegisterReturnLine);
	public:
		IRecordSetObject* m_ownerTable; 
	public:

		CRecordSetRegisterReturnLine(IRecordSetObject* ownerTable = NULL, const wxDataViewItem&line = wxDataViewItem(NULL));
		virtual ~CRecordSetRegisterReturnLine();

		virtual IValueTable* GetOwnerModel() const {
			return m_ownerTable;
		}

		//set meta/get meta
		virtual void SetValueByMetaID(const meta_identifier_t& id, const CValue& cVal);
		virtual CValue GetValueByMetaID(const meta_identifier_t& id) const;

		virtual CMethods* GetPMethods() const { PrepareNames(); return m_methods; } //получить ссылку на класс помощник разбора имен атрибутов и методов
		virtual void PrepareNames() const;                         //этот метод автоматически вызывается для инициализации имен атрибутов и методов

		virtual void SetAttribute(attributeArg_t& aParams, CValue& cVal); //установка атрибута
		virtual CValue GetAttribute(attributeArg_t& aParams); //значение атрибута

		virtual wxString GetTypeString() const {
			return wxT("recordSetRegisterRow");
		}

		virtual wxString GetString() const {
			return wxT("recordSetRegisterRow");
		}

		friend class IRecordSetObject;
	private:
		CMethods* m_methods;
	};

	class CRecordSetRegisterKeyValue : public CValue {
		wxDECLARE_DYNAMIC_CLASS(CRecordSetRegisterReturnLine);
	public:
		class CRecordSetRegisterKeyDescriptionValue : public CValue {
			wxDECLARE_DYNAMIC_CLASS(CRecordSetRegisterKeyDesriptionValue);
		public:

			CRecordSetRegisterKeyDescriptionValue(IRecordSetObject* recordSet = NULL, const meta_identifier_t& id = wxNOT_FOUND);
			virtual ~CRecordSetRegisterKeyDescriptionValue();

			virtual inline bool IsEmpty() const {
				return false;
			}

			//****************************************************************************
			//*                              Support methods                             *
			//****************************************************************************

			virtual CMethods* GetPMethods() const;                          // получить ссылку на класс помощник разбора имен атрибутов и методов
			virtual void PrepareNames() const;                             // этот метод автоматически вызывается для инициализации имен атрибутов и методов
			virtual CValue Method(methodArg_t& aParams);       // вызов метода

			virtual void SetAttribute(attributeArg_t& aParams, CValue& cVal);//установка атрибута
			virtual CValue GetAttribute(attributeArg_t& aParams);//значение атрибута

			virtual wxString GetTypeString() const {
				return wxT("recordSetRegisterKeyDescription");
			}

			virtual wxString GetString() const {
				return wxT("recordSetRegisterKeyDescription");
			}

		protected:
			IRecordSetObject* m_recordSet;
			CMethods* m_methods;

			meta_identifier_t m_metaId;
		};
	public:

		CRecordSetRegisterKeyValue(IRecordSetObject* recordSet = NULL);
		virtual ~CRecordSetRegisterKeyValue();

		virtual inline bool IsEmpty() const {
			return false;
		}

		//****************************************************************************
		//*                              Support methods                             *
		//****************************************************************************

		virtual CMethods* GetPMethods() const;                          // получить ссылку на класс помощник разбора имен атрибутов и методов
		virtual void PrepareNames() const;                             // этот метод автоматически вызывается для инициализации имен атрибутов и методов
		virtual CValue Method(methodArg_t& aParams);       // вызов метода

		virtual void SetAttribute(attributeArg_t& aParams, CValue& cVal);//установка атрибута
		virtual CValue GetAttribute(attributeArg_t& aParams);//значение атрибута

		virtual wxString GetTypeString() const {
			return wxT("recordSetRegisterKey");
		}

		virtual wxString GetString() const {
			return wxT("recordSetRegisterKey");
		}

	protected:
		IRecordSetObject* m_recordSet;
		CMethods* m_methods;
	};

	CRecordSetRegisterColumnCollection* m_dataColumnCollection;
	CRecordSetRegisterKeyValue* m_recordSetKeyValue;
protected:
	IRecordSetObject(IMetaObjectRegisterData* metaObject, const CUniquePairKey& uniqueKey);
	IRecordSetObject(const IRecordSetObject& source);
public:
	virtual ~IRecordSetObject();

	virtual void CreateEmptyKey();
	virtual bool InitializeObject(const IRecordSetObject* source = NULL, bool newRecord = false);

	bool FindKeyValue(const meta_identifier_t& id) const;
	void SetKeyValue(const meta_identifier_t& id, const CValue& cValue);
	CValue GetKeyValue(const meta_identifier_t& id) const;
	void EraseKeyValue(const meta_identifier_t& id);

	//copy new object
	virtual IRecordSetObject* CopyRegisterValue();

	bool Selected() const {
		return m_selected;
	}

	void Read() {
		ReadData();
	}

	//counter
	virtual void IncrRef() {
		CValue::IncrRef();
	}

	virtual void DecrRef() {
		CValue::DecrRef();
	}

	virtual inline bool IsEmpty() const;

	//set modify 
	virtual void Modify(bool mod);

	//is modified 
	virtual bool IsModified() const;

	//get metadata from object 
	virtual IMetaObjectRegisterData* GetMetaObject() const {
		return m_metaObject;
	};

	virtual bool AutoCreateColumns() const { return false; }
	virtual bool EditableLine(const wxDataViewItem& item, unsigned int col) const {
		return false;
	}

	virtual void ActivateItem(CValueForm* formOwner,
		const wxDataViewItem& item, unsigned int col) {
		IValueTable::RowValueStartEdit(item, col);
	}

	virtual void AddValue(unsigned int before = 0) {}
	virtual void CopyValue() {}
	virtual void EditValue() {}
	virtual void DeleteValue() {}

	// implementation of base class virtuals to define model
	virtual void GetValueByRow(wxVariant& variant,
		const wxDataViewItem& row, unsigned int col) const override;
	virtual bool SetValueByRow(const wxVariant& variant,
		const wxDataViewItem& row, unsigned int col) override;

	//support def. methods (in runtime)
	virtual long AppendRow(unsigned int before = 0);

	virtual bool LoadDataFromTable(IValueTable* srcTable);
	virtual IValueTable* SaveDataToTable() const;

	//default methods
	virtual bool WriteRecordSet(bool replace = true, bool clearTable = true) = 0;
	virtual bool DeleteRecordSet() = 0;

	//array
	virtual CValue GetAt(const CValue& cKey);

	//Get ref class 
	virtual CLASS_ID GetClassType() const;

	virtual wxString GetTypeString() const;
	virtual wxString GetString() const;
	
	//operator 
	virtual operator CValue() const {
		return this;
	}

	//Работа с итераторами 
	virtual bool HasIterator() const override { 
		return true; 
	}

	virtual CValue GetItEmpty() override {
		return new CRecordSetRegisterReturnLine(this, wxDataViewItem(NULL));
	}

	virtual CValue GetItAt(unsigned int idx) override {
		if (idx > (unsigned int)GetRowCount())
			return CValue();
		return new CRecordSetRegisterReturnLine(this, GetItem(idx));
	}

	virtual unsigned int GetItSize() const override {
		return GetRowCount();
	}

protected:

	virtual bool ExistData();
	virtual bool ExistData(number_t& lastNum); //for records
	virtual bool ReadData();
	virtual bool SaveData(bool replace = true, bool clearTable = true);
	virtual bool DeleteData();

	////////////////////////////////////////

	virtual bool SaveVirtualTable() {
		return true;
	}

	virtual bool DeleteVirtualTable() {
		return true;
	}

protected:

	//set meta/get meta
	virtual void SetValueByMetaID(long line, const meta_identifier_t& id, const CValue& cVal);
	virtual CValue GetValueByMetaID(long line, const meta_identifier_t& id) const;

protected:

	friend class IRecordManagerObject;

	bool m_objModified;
	bool m_selected;

	std::map<meta_identifier_t, CValue> m_keyValues;

	IMetaObjectRegisterData* m_metaObject;
	CMethods* m_methods;
};

class IRecordManagerObject : public CValue,
	public ISourceDataObject, public IActionSource {
	wxDECLARE_ABSTRACT_CLASS(IRecordManagerObject);
protected:
	IRecordManagerObject(IMetaObjectRegisterData* metaObject, const CUniquePairKey& uniqueKey);
	IRecordManagerObject(const IRecordManagerObject& source);
public:

	IRecordSetObject* GetRecordSet() const {
		return m_recordSet;
	}

	virtual ~IRecordManagerObject();

	virtual void CreateEmptyKey();
	virtual bool InitializeObject(const IRecordManagerObject* source = NULL, bool newRecord = false);

	//copy new object
	virtual IRecordManagerObject* CopyRegisterValue();

	//counter
	virtual void IncrRef() {
		CValue::IncrRef();
	}

	virtual void DecrRef() {
		CValue::DecrRef();
	}

	//get unique identifier 
	virtual CUniqueKey GetGuid() const {
		return m_objGuid;
	};

	//save modify 
	virtual bool SaveModify() override {
		return WriteRegister();
	}

	//default methods
	virtual CValue CopyRegister() = 0;
	virtual bool WriteRegister(bool replace = true) = 0;
	virtual bool DeleteRegister() = 0;

	//check is empty
	virtual inline bool IsEmpty() const override;

	//set modify 
	virtual void Modify(bool mod);

	//is modified 
	virtual bool IsModified() const;

	//support source set/get data 
	virtual void SetValueByMetaID(const meta_identifier_t& id, const CValue& cVal);
	virtual CValue GetValueByMetaID(const meta_identifier_t& id) const;

	//support source data 
	virtual CSourceExplorer GetSourceExplorer() const;
	virtual bool GetModel(IValueModel*& tableValue, const meta_identifier_t& id);

	//get metadata from object 
	virtual IMetaObjectRegisterData* GetMetaObject() const {
		return m_metaObject;
	};

	//get frame
	virtual CValueForm* GetForm() const;

	//support show 
	virtual void ShowFormValue(const wxString& formName = wxEmptyString, IValueFrame* owner = NULL) = 0;
	virtual CValueForm* GetFormValue(const wxString& formName = wxEmptyString, IValueFrame* owner = NULL) = 0;

	//default showing
	virtual void ShowValue() override {
		ShowFormValue();
	}

	//Get ref class 
	virtual CLASS_ID GetClassType() const;

	virtual wxString GetTypeString() const;
	virtual wxString GetString() const;

	//operator 
	virtual operator CValue() const {
		return this;
	}

protected:

	virtual void PrepareEmptyObject(const IRecordManagerObject* source);

protected:

	virtual bool ExistData();
	virtual bool ReadData();
	virtual bool SaveData(bool replace = true);
	virtual bool DeleteData();

protected:

	CUniquePairKey m_objGuid;

	IMetaObjectRegisterData* m_metaObject;
	IRecordSetObject* m_recordSet;

	IValueTable::IValueModelReturnLine* m_recordLine;

	CMethods* m_methods;
};
#pragma endregion

typedef IMetaObjectRecordDataFolderMutableRef::CMetaGroupAttributeObject CMetaGroupAttributeObject;
typedef IMetaObjectRecordDataFolderMutableRef::CMetaGroupTableObject CMetaGroupTableObject;

#endif 