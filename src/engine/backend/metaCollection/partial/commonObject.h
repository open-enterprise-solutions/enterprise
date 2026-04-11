#ifndef __COMMON_OBJECT_H__
#define __COMMON_OBJECT_H__

#include "reference/reference.h"

//special object 
#include "backend/metaCollection/metaModuleObject.h"
#include "backend/metaCollection/metaFormObject.h"
#include "backend/metaCollection/metaSpreadsheetObject.h"

//interface
#include "backend/metaCollection/metaInterfaceObject.h"

//attributes 
#include "backend/metaCollection/attribute/metaAttributeObject.h"
#include "backend/metaCollection/dimension/metaDimensionObject.h"
#include "backend/metaCollection/resource/metaResourceObject.h"

//enumeration 
#include "backend/metaCollection/enumeration/metaEnumObject.h"

//tables 
#include "backend/metaCollection/table/metaTableObject.h"

//spreadsheet guid 
#include "backend/system/value/valueSpreadsheet.h"
#include "backend/system/value/valueGuid.h"

#include "backend/appData.h"
#include "backend/actionInfo.h"
#include "backend/moduleInfo.h"
#include "backend/valueInfo.h"
#include "backend/tableInfo.h"

//********************************************************************************************
//*                                     Defines                                              *
//********************************************************************************************

class BACKEND_API ibValueMetaObjectConstant;

class BACKEND_API ibValueMetaObjectGenericData;
class BACKEND_API ibValueMetaObjectRecordData;
class BACKEND_API ibValueMetaObjectRecordDataRef;
class BACKEND_API ibValueMetaObjectRegisterData;

class BACKEND_API ibSourceDataObject;

class BACKEND_API ibValueManagerDataObject;

class BACKEND_API ibValueRecordDataObject;
class BACKEND_API ibValueRecordDataObjectExt;
class BACKEND_API ibValueRecordDataObjectRef;
class BACKEND_API ibValueRecordDataObjectHierarchyRef;

class BACKEND_API ibValueRecordKeyObject;
class BACKEND_API ibValueRecordManagerObject;
class BACKEND_API ibValueRecordSetObject;

class BACKEND_API ibSourceExplorer;

//special names 
#define guidName wxT("uuid")

//********************************************************************************************
//*                                  Factory & metaData                                      *
//********************************************************************************************

class BACKEND_API ibFormTypeList {

	struct ibFormTypeItem {
		bool m_isOk;
		wxString m_strName;
		wxString m_strLabel;
		wxString m_strHelp;
		long m_id;
	public:

		ibFormTypeItem() :
			m_isOk(true), m_strName(), m_strLabel(), m_id(-1)
		{
		}

		ibFormTypeItem(const wxString& name, const long& l) :
			m_isOk(true), m_strName(name), m_strLabel(name), m_id(l)
		{
		}

		ibFormTypeItem(const wxString& name, const wxString& label, const long& l) :
			m_isOk(true), m_strName(name), m_strLabel(label), m_id(l)
		{
		}

		ibFormTypeItem(const wxString& name, const wxString& label, const wxString& help, const long& l) :
			m_isOk(true), m_strName(name), m_strLabel(label), m_strHelp(help), m_id(l)
		{
		}

		ibFormTypeItem(const ibFormTypeItem& item) :
			m_isOk(true), m_strName(item.m_strName), m_strLabel(item.m_strLabel), m_strHelp(item.m_strHelp), m_id(item.m_id)
		{
		}

		ibFormTypeItem& operator = (const ibFormTypeItem& src) {
			m_strName = src.m_strName;
			m_strLabel = src.m_strLabel;
			m_strHelp = src.m_strHelp;
			m_id = src.m_id;
			return *this;
		}

		operator const long() const { return m_id; }
	};

	ibFormTypeItem GetItemAt(const unsigned int idx) const {
		if (idx > m_listTypeForm.size())
			return ibFormTypeItem();
		auto it = m_listTypeForm.begin();
		std::advance(it, idx);
		return *it;
	};

	ibFormTypeItem GetItemById(const long& id) const {
		auto it = std::find_if(m_listTypeForm.begin(), m_listTypeForm.end(),
			[id](const ibFormTypeItem& p) { return id == p.m_id; }
		);
		if (it != m_listTypeForm.end())
			return *it;
		return ibFormTypeItem();
	};

public:

	void ResetListItem() { m_listTypeForm.clear(); }

	void AppendItem(const wxString& name, const int& l) { (void)m_listTypeForm.emplace_back(name, l); }
	void AppendItem(const wxString& name, const wxString& label, const int& l) { (void)m_listTypeForm.emplace_back(name, label, l); }
	void AppendItem(const wxString& name, const wxString& label, const wxString& help, const int& l) { (void)m_listTypeForm.emplace_back(label, help, l); }

	bool HasValue(const long& l) const { return GetItemById(l); }

	wxString GetItemName(const unsigned int idx) const { return GetItemAt(idx).m_strName; }
	wxString GetItemLabel(const unsigned int idx) const { return GetItemAt(idx).m_strLabel; }
	wxString GetItemHelp(const unsigned int idx) const { return GetItemAt(idx).m_strHelp; }
	long GetItemId(const unsigned int idx) const { return GetItemAt(idx).m_id; }

	unsigned int GetItemCount() const { return (unsigned int)m_listTypeForm.size(); }

private:
	std::vector<ibFormTypeItem> m_listTypeForm;
};

#include "backend/metaCollection/metaObjectComposite.h"

class BACKEND_API ibValueMetaObjectGenericData
	: public ibValueMetaObjectCompositeData, public ibBackendCommandItem {
	wxDECLARE_ABSTRACT_CLASS(ibValueMetaObjectGenericData);
public:
	friend class ibMetaData;
public:

#pragma region access_generic
	virtual bool AccessRight_Show() const { return true; }
#pragma endregion

	virtual bool FilterChild(const ibClassID& clsid) const {
		if (
			clsid == g_metaFormCLSID ||
			clsid == g_metaTemplateCLSID
			)
			return true;
		return false;
	}

	//get data selector 
	virtual ibSelectorDataType GetFilterDataType() const {
		return ibSelectorDataType::ibSelectorDataType_reference;
	}

#pragma region __array_h__

	//form
	std::vector<ibValueMetaObjectFormBase*> GetFormArrayObject(
		std::vector<ibValueMetaObjectFormBase*> array = std::vector<ibValueMetaObjectFormBase*>()) const {
		FillArrayObjectByFilter<ibValueMetaObjectFormBase>(array, { g_metaFormCLSID });
		return array;
	}

	//grid
	std::vector<ibValueMetaObjectSpreadsheetBase*> GetTemplateArrayObject(
		std::vector<ibValueMetaObjectSpreadsheetBase*> array = std::vector<ibValueMetaObjectSpreadsheetBase*>()) const {
		FillArrayObjectByFilter<ibValueMetaObjectSpreadsheetBase>(array, { g_metaTemplateCLSID });
		return array;
	}

#pragma endregion
#pragma region __filter_h__

	//form
	template <typename _T1>
	ibValueMetaObjectFormBase* FindFormObjectByFilter(const _T1& id, const ibFormID& form_id = wxNOT_FOUND) const {
		ibValueMetaObjectFormBase* founded = FindObjectByFilter<ibValueMetaObjectFormBase>(id, { g_metaCommonFormCLSID, g_metaFormCLSID });
		if ((founded != nullptr && form_id == founded->GetTypeForm()) || form_id == wxNOT_FOUND)
			return founded;
		return nullptr;
	}

	//grid
	template <typename _T1>
	ibValueMetaObjectSpreadsheetBase* FindTemplateObjectByFilter(const _T1& id) const {
		return FindObjectByFilter<ibValueMetaObjectSpreadsheetBase>(id, { g_metaCommonTemplateCLSID, g_metaTemplateCLSID });
	}

#pragma endregion 

	//form events 
	virtual void OnCreateFormObject(ibValueMetaObjectFormBase* metaForm) {}
	virtual void OnRemoveMetaForm(ibValueMetaObjectFormBase* metaForm) {}

	//Get form type
	virtual ibFormTypeList GetFormType() const = 0;

	//Get metaObject by def id
	virtual ibValueMetaObjectFormBase* GetDefaultFormByID(const ibFormID& id) const { return nullptr; }

	//create form with data 
	virtual ibBackendValueForm* CreateObjectForm(ibValueMetaObjectFormBase* metaForm) {
		return ibValueMetaObjectGenericData::CreateAndBuildForm(
			metaForm != nullptr ? metaForm->GetName() : wxString(wxEmptyString),
			metaForm != nullptr ? metaForm->GetTypeForm() : defaultFormType,
			nullptr,
			CreateSourceObject(metaForm),
			metaForm != nullptr ? metaForm->GetGuid() : wxNullGuid
		);
	}

#pragma region _form_builder_h_
	//support form 
	ibBackendValueForm* GetGenericForm(const wxString& strFormName = wxEmptyString,
		ibBackendControlFrame* ownerControl = nullptr, const ibUniqueKey& formGuid = wxNullGuid);
#pragma endregion

#pragma region _form_creator_h_
	ibBackendValueForm* CreateAndBuildForm(const wxString& strFormName, const ibFormID& form_id = defaultFormType,
		ibBackendControlFrame* ownerControl = nullptr,
		ibSourceDataObject* srcObject = nullptr,
		const ibUniqueKey& formGuid = wxNullGuid
	);
#pragma endregion

#pragma region _template_builder_h_

	class ibValueSpreadsheetDocument* GetTemplate(const wxString& strFormName);

#pragma endregion

	virtual ibValueManagerDataObject* CreateManagerDataObjectValue() = 0;

protected:

	//create object data with meta form
	virtual ibSourceDataObject* CreateSourceObject(ibValueMetaObjectFormBase* metaObject) { return nullptr; }
};

class BACKEND_API ibValueMetaObjectRecordData
	: public ibValueMetaObjectGenericData {

	wxDECLARE_ABSTRACT_CLASS(ibValueMetaObjectRecordData);

public:

	virtual bool FilterChild(const ibClassID& clsid) const {
		if (
			clsid == g_metaAttributeCLSID ||
			clsid == g_metaTableCLSID
			)
			return true;
		return ibValueMetaObjectGenericData::FilterChild(clsid);
	}

	//get data selector 
	virtual ibSelectorDataType GetFilterDataType() const {
		return ibSelectorDataType::ibSelectorDataType_any;
	}

	//get module object in compose object 
	virtual ibValueMetaObjectModule* GetModuleObject() const { return nullptr; }
	virtual ibValueMetaObjectCommonModule* GetModuleManager() const { return nullptr; }

	//meta events
	virtual bool OnLoadMetaObject(ibMetaData* metaData);
	virtual bool OnSaveMetaObject(int flags);
	virtual bool OnDeleteMetaObject();

	//module manager is started or exit 
	virtual bool OnBeforeRunMetaObject(int flags);
	virtual bool OnAfterCloseMetaObject();

#pragma region __generic_h__

	using ibValueMetaObjectCompositeData::GetGenericAttributeArrayObject;
	//attribute
	virtual std::vector<ibValueMetaObjectAttributeBase*> GetGenericAttributeArrayObject(
		std::vector<ibValueMetaObjectAttributeBase*>& array) const {
		FillArrayObjectByPredefined(array);
		FillArrayObjectByFilter<ibValueMetaObjectAttributeBase>(array, { g_metaAttributeCLSID });
		return array;
	}

#pragma endregion

#pragma region __array_h__

	//any attribute
	std::vector<ibValueMetaObjectAttributeBase*> GetAnyAttributeArrayObject(
		std::vector<ibValueMetaObjectAttributeBase*> array = std::vector<ibValueMetaObjectAttributeBase*>()) const {
		FillArrayObjectByPredefined(array);
		FillArrayObjectByFilter<ibValueMetaObjectAttributeBase>(array, { g_metaAttributeCLSID });
		return array;
	}

	//attribute
	std::vector<ibValueMetaObjectAttributeBase*> GetAttributeArrayObject(
		std::vector<ibValueMetaObjectAttributeBase*> array = std::vector<ibValueMetaObjectAttributeBase*>()) const {
		FillArrayObjectByFilter<ibValueMetaObjectAttributeBase>(array, { g_metaAttributeCLSID });
		return array;
	}

	//table
	std::vector<ibValueMetaObjectTableData*> GetTableArrayObject(
		std::vector<ibValueMetaObjectTableData*> array = std::vector<ibValueMetaObjectTableData*>()) const {
		FillArrayObjectByFilter<ibValueMetaObjectTableData>(array, { g_metaTableCLSID });
		return array;
	}

#pragma endregion
#pragma region __filter_h__

	//any attribute 
	template <typename _T1>
	ibValueMetaObjectAttributeBase* FindAnyAttributeObjectByFilter(const _T1& id) const {
		return FindObjectByFilter<ibValueMetaObjectAttributeBase>(id, { g_metaAttributeCLSID, g_metaPredefinedAttributeCLSID });
	}

	//attribute 
	template <typename _T1>
	ibValueMetaObjectAttributeBase* FindAttributeObjectByFilter(const _T1& id) const {
		return FindObjectByFilter<ibValueMetaObjectAttributeBase>(id, { g_metaAttributeCLSID });
	}

	//table
	template <typename _T1>
	ibValueMetaObjectTableData* FindTableObjectByFilter(const _T1& id) const {
		return FindObjectByFilter<ibValueMetaObjectTableData>(id, { g_metaTableCLSID });
	}

#pragma endregion 

	//create single object
	virtual ibValueRecordDataObject* CreateRecordDataObjectValue() = 0;

#pragma region _form_builder_h_
	//support form 
	virtual ibBackendValueForm* GetObjectForm(const wxString& strFormName = wxEmptyString, ibBackendControlFrame* ownerControl = nullptr, const ibUniqueKey& formGuid = wxNullGuid) = 0;
#pragma endregion 

protected:

	//get default form 
	virtual ibBackendValueForm* GetFormByCommandType(ibInterfaceCommandType cmdType = ibInterfaceCommandType::ibInterfaceCommandType_Default) {

		if (cmdType == ibInterfaceCommandType::ibInterfaceCommandType_Create) {
			return GetObjectForm();
		}

		return GetObjectForm();
	}
};

enum ibObjectMode {
	OBJECT_ITEM = 1,
	OBJECT_FOLDER
};

//meta object with file 
class BACKEND_API ibValueMetaObjectRecordDataExt : public ibValueMetaObjectRecordData {
	wxDECLARE_ABSTRACT_CLASS(ibValueMetaObjectRecordDataExt);
public:

#pragma region access_generic
	virtual bool AccessRight_Show() const { return AccessRight_Use(); }
#pragma endregion
#pragma region access
	bool AccessRight_Use() const { return IsFullAccess() || AccessRight(m_roleUse); }
#pragma endregion

	//ctor
	ibValueMetaObjectRecordDataExt();

	//�reate from file?
	virtual bool IsExternalCreate() const { return false; }

	//module manager is started or exit 
	virtual bool OnBeforeRunMetaObject(int flags);
	virtual bool OnAfterCloseMetaObject();

	//create associate value 
	ibValueRecordDataObjectExt* CreateObjectValue(); //create object
	ibValueRecordDataObjectExt* CreateObjectValue(ibValueRecordDataObjectExt* objSrc);

	//create single object
	virtual ibValueRecordDataObject* CreateRecordDataObjectValue();

	//get command section 
	virtual ibInterfaceCommandSection GetCommandSection() const { return ibInterfaceCommandSection::ibInterfaceCommandSection_Service; }

protected:

	//create empty object
	virtual ibValueRecordDataObjectExt* CreateObjectExtValue() = 0;  //create object 

private:
#pragma region role
	ibRole* m_roleUse = ibValueMetaObject::CreateRole(wxT("Use"), _("Use"));
#pragma endregion
};

//meta object with reference 
class BACKEND_API ibValueMetaObjectRecordDataRef : public ibValueMetaObjectRecordData {
	wxDECLARE_ABSTRACT_CLASS(ibValueMetaObjectRecordDataRef);

protected:
	//ctor
	ibValueMetaObjectRecordDataRef();
	virtual ~ibValueMetaObjectRecordDataRef();
public:

	virtual ibValueMetaObjectAttributePredefined* GetDataReference() const { return m_propertyAttributeReference->GetMetaObject(); }
	virtual bool IsDataReference(const ibMetaID& id) const { return id == (*m_propertyAttributeReference)->GetMetaID(); }

	virtual bool HasQuickChoice() const {
		return m_propertyQuickChoice->GetValueAsBoolean();
	}

	//get data selector 
	virtual ibSelectorDataType GetFilterDataType() const { return ibSelectorDataType::ibSelectorDataType_reference; }

	//meta events
	virtual bool OnCreateMetaObject(ibMetaData* metaData, int flags);
	virtual bool OnLoadMetaObject(ibMetaData* metaData);
	virtual bool OnSaveMetaObject(int flags);
	virtual bool OnDeleteMetaObject();

	//module manager is started or exit 
	//after and before for designer 
	virtual bool OnBeforeRunMetaObject(int flags);
	virtual bool OnAfterRunMetaObject(int flags);

	virtual bool OnBeforeCloseMetaObject();
	virtual bool OnAfterCloseMetaObject();

	//create single object
	virtual ibValueRecordDataObject* CreateRecordDataObjectValue() {
		wxASSERT_MSG(false, "ibValueMetaObjectRecordDataRef::CreateRecordDataObjectValue");
		return nullptr;
	}

	//process choice 
	virtual bool ProcessChoice(ibBackendControlFrame* ownerValue,
		const wxString& strFormName = wxEmptyString, ibSelectMode selMode = ibSelectMode::ibSelectMode_Items);

#pragma region __generic_h__

	//searched 
	virtual std::vector<ibValueMetaObjectAttributeBase*> GetSearchedAttributeObjectArray(
		std::vector<ibValueMetaObjectAttributeBase*> array = std::vector<ibValueMetaObjectAttributeBase*>()) const {
		FillArrayObjectBySearched(array);
		return array;
	}

#pragma endregion 

	//get attribute code 
	virtual ibValueMetaObjectAttributeBase* GetAttributeForCode() const { return nullptr; }

	//find object value
	virtual class ibValueReferenceDataObject* FindObjectValue(const ibGuid& guid); //find by guid and ret reference 

#pragma region _form_builder_h_
	//support form 
	virtual ibBackendValueForm* GetListForm(const wxString& strFormName = wxEmptyString, ibBackendControlFrame* ownerControl = nullptr, const ibUniqueKey& formGuid = wxNullGuid) = 0;
	virtual ibBackendValueForm* GetSelectForm(const wxString& strFormName = wxEmptyString, ibBackendControlFrame* ownerControl = nullptr, const ibUniqueKey& formGuid = wxNullGuid) = 0;
#pragma endregion 

	//descriptions...
	virtual wxString GetDataPresentation(const ibValueDataObject* objValue) const = 0;

	//special functions for DB 
	virtual wxString GetTableNameDB() const;

protected:

	//get default form 
	virtual ibBackendValueForm* GetFormByCommandType(ibInterfaceCommandType cmdType = ibInterfaceCommandType::ibInterfaceCommandType_Default) {

		if (cmdType == ibInterfaceCommandType::ibInterfaceCommandType_Create)
			return GetObjectForm();
		else if (cmdType == ibInterfaceCommandType::ibInterfaceCommandType_List)
			return GetListForm();
		else if (cmdType == ibInterfaceCommandType::ibInterfaceCommandType_Select)
			return GetSelectForm();

		return GetListForm();
	}

	//searched array 
	virtual bool FillArrayObjectBySearched(std::vector<ibValueMetaObjectAttributeBase*>& array) const { return true; }

	//load & save metaData from DB 
	virtual bool LoadData(ibReaderMemory& reader);
	virtual bool SaveData(ibWriterMemory& writer);
	virtual bool DeleteData() { return true; }

protected:

	ibPropertyCategory* m_categoryData = ibPropertyObject::CreatePropertyCategory(wxT("Data"), _("Data"));
	ibPropertyCategory* m_categoryPresentation = ibPropertyObject::CreatePropertyCategory(wxT("Presentation"), _("Presentation"));
	ibPropertyBoolean* m_propertyQuickChoice = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryPresentation, wxT("QuickChoice"), _("Quick choice"), false);
	ibPropertyInnerAttribute<>* m_propertyAttributeReference = ibPropertyObject::CreateProperty<ibPropertyInnerAttribute<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateSpecialType(wxT("Reference"), _("Reference"), wxEmptyString, ibValue::GetIDByVT(ibValueTypes::TYPE_EMPTY)));
};

//meta object with reference - for enumeration
class BACKEND_API ibValueMetaObjectRecordDataEnumRef : public ibValueMetaObjectRecordDataRef {
	wxDECLARE_ABSTRACT_CLASS(ibValueMetaObjectRecordDataEnumRef);
protected:

	//ctor
	ibValueMetaObjectRecordDataEnumRef();
	virtual ~ibValueMetaObjectRecordDataEnumRef();

public:

	ibValueMetaObjectAttributePredefined* GetDataOrder() const { return m_propertyAttributeOrder->GetMetaObject(); }
	bool IsDataOrder(const ibMetaID& id) const { return id == (*m_propertyAttributeOrder)->GetMetaID(); }

	//events: 
	virtual bool OnCreateMetaObject(ibMetaData* metaData, int flags);
	virtual bool OnLoadMetaObject(ibMetaData* metaData);
	virtual bool OnSaveMetaObject(int flags);
	virtual bool OnDeleteMetaObject();

	//module manager is started or exit 
	virtual bool OnBeforeRunMetaObject(int flags);
	virtual bool OnAfterRunMetaObject(int flags);

	virtual bool OnBeforeCloseMetaObject();
	virtual bool OnAfterCloseMetaObject();

#pragma region __array_h__

	//enum
	std::vector<ibValueMetaObjectEnum*> GetEnumObjectArray(
		std::vector<ibValueMetaObjectEnum*> array = std::vector<ibValueMetaObjectEnum*>()) const {
		FillArrayObjectByFilter<ibValueMetaObjectEnum>(array, { g_metaEnumCLSID });
		return array;
	}

#pragma endregion
#pragma region __filter_h__

	//enum
	template <typename _T1>
	ibValueMetaObjectEnum* FindEnumObjectByFilter(const _T1& id) const {
		return FindObjectByFilter<ibValueMetaObjectEnum>(id, { g_metaEnumCLSID });
	}

#pragma endregion 

protected:

	//predefined array 
	virtual bool FillArrayObjectByPredefined(std::vector<ibValueMetaObjectAttributeBase*>& array) const {
		array = { m_propertyAttributeReference->GetMetaObject() };
		return true;
	}

	//searched array 
	virtual bool FillArrayObjectBySearched(std::vector<ibValueMetaObjectAttributeBase*>& array) const { return true; }

	//create and update table 
	virtual bool CreateAndUpdateTableDB(ibMetaDataConfiguration* srcMetaData, ibValueMetaObject* srcMetaObject, int flags);

	//load & save metaData from DB 
	virtual bool LoadData(ibReaderMemory& reader);
	virtual bool SaveData(ibWriterMemory& writer);

	//process default query
	int ProcessEnumeration(const wxString& tableName, const ibValueMetaObjectEnum* srcEnum, const ibValueMetaObjectEnum* dstEnum);

private:

	//default attributes 
	ibPropertyInnerAttribute<>* m_propertyAttributeOrder = ibPropertyObject::CreateProperty<ibPropertyInnerAttribute<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateNumber(wxT("Order"), _("Order"), wxEmptyString, 6, true));
};

//meta object with reference and deletion mark 
class BACKEND_API ibValueMetaObjectRecordDataMutableRef : public ibValueMetaObjectRecordDataRef {
	wxDECLARE_ABSTRACT_CLASS(ibValueMetaObjectRecordDataMutableRef);
protected:
	//ctor
	ibValueMetaObjectRecordDataMutableRef();
	virtual ~ibValueMetaObjectRecordDataMutableRef();
public:

#pragma region access_generic
	virtual bool AccessRight_Show() const { return AccessRight_Read(); }
#pragma endregion

	ibMetaDescription& GetGenerationDescription() const { return m_propertyGeneration->GetValueAsMetaDesc(); }

#pragma region access
	bool AccessRight_Read() const { return IsFullAccess() || AccessRight(m_roleRead); }
	bool AccessRight_Write() const { return IsFullAccess() || AccessRight(m_roleWrite); }
	bool AccessRight_Delete() const { return IsFullAccess() || AccessRight(m_roleDelete); }
#pragma endregion

	ibValueMetaObjectAttributePredefined* GetDataVersion() const { return m_propertyAttributeDataVersion->GetMetaObject(); }
	bool IsDataVersion(const ibMetaID& id) const { return id == (*m_propertyAttributeDataVersion)->GetMetaID(); }

	ibValueMetaObjectAttributePredefined* GetDataDeletionMark() const { return m_propertyAttributeDeletionMark->GetMetaObject(); }
	bool IsDataDeletionMark(const ibMetaID& id) const { return id == (*m_propertyAttributeDeletionMark)->GetMetaID(); }

	virtual bool HasQuickChoice() const { return false; }

	//events: 
	virtual bool OnCreateMetaObject(ibMetaData* metaData, int flags);
	virtual bool OnLoadMetaObject(ibMetaData* metaData);
	virtual bool OnSaveMetaObject(int flags);
	virtual bool OnDeleteMetaObject();

	//module manager is started or exit 
	virtual bool OnBeforeRunMetaObject(int flags);
	virtual bool OnAfterRunMetaObject(int flags);

	virtual bool OnBeforeCloseMetaObject();
	virtual bool OnAfterCloseMetaObject();

	//create associate value 
	ibValueRecordDataObjectRef* CreateObjectValue();
	ibValueRecordDataObjectRef* CreateObjectValue(const ibGuid& guid);
	ibValueRecordDataObjectRef* CreateObjectValue(ibValueRecordDataObjectRef* objSrc, bool generate = false);

	//copy associate value 	
	ibValueRecordDataObjectRef* CopyObjectValue(const ibGuid& guid);

	//create single object
	virtual ibValueRecordDataObject* CreateRecordDataObjectValue();

	//get command section 
	virtual ibInterfaceCommandSection GetCommandSection() const { return ibInterfaceCommandSection::ibInterfaceCommandSection_Combined; }

	// load & save config data 
	virtual bool LoadTableData(const ibReaderMemory& reader);
	virtual bool SaveTableData(ibWriterMemory& writer) const;

protected:

	//predefined array 
	virtual bool FillArrayObjectByPredefined(std::vector<ibValueMetaObjectAttributeBase*>& array) const {

		array = {
			m_propertyAttributeReference->GetMetaObject(),
			m_propertyAttributeDeletionMark->GetMetaObject()
		};

		return true;
	}

	//searched array 
	virtual bool FillArrayObjectBySearched(std::vector<ibValueMetaObjectAttributeBase*>& array) const { return true; }

	/**
	* Property events
	*/
	virtual void OnPropertyCreated(ibProperty* property);
	virtual void OnPropertyRefresh(class wxPropertyGridManager* pg, class wxPGProperty* pgProperty, ibProperty* property);
	virtual bool OnPropertyChanging(ibProperty* property, const wxVariant& newValue);
	virtual void OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue);

	//create and update table 
	virtual bool CreateAndUpdateTableDB(ibMetaDataConfiguration* srcMetaData, ibValueMetaObject* srcMetaObject, int flags);

	//load & save metaData from DB 
	virtual bool LoadData(ibReaderMemory& reader);
	virtual bool SaveData(ibWriterMemory& writer);

	//process default query
	int ProcessAttribute(const wxString& tableName, const ibValueMetaObjectAttributeBase* srcAttr, const ibValueMetaObjectAttributeBase* dstAttr);
	int ProcessTable(const wxString& tabularName, const ibValueMetaObjectTableData* srcTable, const ibValueMetaObjectTableData* dstTable);

	//create empty object
	virtual ibValueRecordDataObjectRef* CreateObjectRefValue(const ibGuid& objGuid = wxNullGuid) = 0; //create object and read by guid 

protected:

	ibPropertyGeneration* m_propertyGeneration = ibPropertyObject::CreateProperty<ibPropertyGeneration>(m_categoryData, wxT("ListGeneration"), _("List generation"));
	ibPropertyInnerAttribute<>* m_propertyAttributeDataVersion = ibPropertyObject::CreateProperty<ibPropertyInnerAttribute<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateString(wxT("DataVersion"), _("Data version"), wxEmptyString, 12, ibItemMode_Folder_Item));
	ibPropertyInnerAttribute<>* m_propertyAttributeDeletionMark = ibPropertyObject::CreateProperty<ibPropertyInnerAttribute<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateBoolean(wxT("DeletionMark"), _("Deletion mark"), wxEmptyString));

private:

#pragma region role
	ibRole* m_roleRead = ibValueMetaObject::CreateRole(wxT("Read"), _("Read"));
	ibRole* m_roleWrite = ibValueMetaObject::CreateRole(wxT("Wrire"), _("Write"));
	ibRole* m_roleDelete = ibValueMetaObject::CreateRole(wxT("Delete"), _("Delete"));
#pragma endregion
};

//meta object with reference and deletion mark and group/object type and predefined values 
class BACKEND_API ibValueMetaObjectRecordDataHierarchyMutableRef :
	public ibValueMetaObjectRecordDataMutableRef {
	wxDECLARE_ABSTRACT_CLASS(ibValueMetaObjectRecordDataHierarchyMutableRef);
public:

	class ibPredefinedValueObject : public wxRefCounter {
	public:

		ibPredefinedValueObject(bool valueIsFolder = false,
			const wxObjectDataPtr<ibPredefinedValueObject>& valueParent = wxObjectDataPtr<ibPredefinedValueObject>())
			:
			m_predefinedItemGuid(wxNewUniqueGuid),
			m_strPredefinedName(),
			m_strCode(), m_strDescription(),
			m_valueIsFolder(valueIsFolder),
			m_valueParent(nullptr)
		{
		}

		ibPredefinedValueObject(
			const ibGuid& predefinedGuid, const wxString& strPredefinedName,
			const wxString& strCode, const wxString& strDescription,
			bool valueIsFolder = false, const wxObjectDataPtr<ibPredefinedValueObject>& valueParent = wxObjectDataPtr<ibPredefinedValueObject>())
			:
			m_predefinedItemGuid(predefinedGuid),
			m_strPredefinedName(strPredefinedName),
			m_strCode(strCode), m_strDescription(strDescription),
			m_valueIsFolder(valueIsFolder),
			m_valueParent(valueParent)
		{
		}

		ibGuid GetPredefinedGuid() const { return m_predefinedItemGuid; }

		wxString GetPredefinedParentName() const {
			if (m_valueParent != nullptr)
				return m_valueParent->GetPredefinedName();
			return wxT("");
		}

		wxString GetPredefinedName() const { return m_strPredefinedName; }
		wxString GetPredefinedCode() const { return m_strCode; }
		wxString GetPredefinedDescription() const { return m_strDescription; }

		bool IsPredefinedFolder() const { return m_valueIsFolder; }
		wxObjectDataPtr<ibPredefinedValueObject> GetPredefinedParent() const { return m_valueParent; }

		friend class ibValueMetaObjectRecordDataHierarchyMutableRef;

	private:

		ibGuid m_predefinedItemGuid;

		wxString m_strPredefinedName;
		wxString m_strCode;
		wxString m_strDescription;

		bool m_valueIsFolder;

		wxObjectDataPtr<ibPredefinedValueObject> m_valueParent;
	};

	ibValueMetaObjectAttributePredefined* GetDataPredefinedName() const { return m_propertyAttributePredefined->GetMetaObject(); }
	virtual bool IsDataPredefinedName(const ibMetaID& id) const { return id == (*m_propertyAttributePredefined)->GetMetaID(); }

	ibValueMetaObjectAttributePredefined* GetDataCode() const { return m_propertyAttributeCode->GetMetaObject(); }
	virtual bool IsDataCode(const ibMetaID& id) const { return id == (*m_propertyAttributeCode)->GetMetaID(); }

	ibValueMetaObjectAttributePredefined* GetDataDescription() const { return m_propertyAttributeDescription->GetMetaObject(); }
	virtual bool IsDataDescription(const ibMetaID& id) const { return id == (*m_propertyAttributeDescription)->GetMetaID(); }

	ibValueMetaObjectAttributePredefined* GetDataParent() const { return m_propertyAttributeParent->GetMetaObject(); }
	virtual bool IsDataParent(const ibMetaID& id) const { return id == (*m_propertyAttributeParent)->GetMetaID(); }

	ibValueMetaObjectAttributePredefined* GetDataIsFolder() const { return m_propertyAttributeIsFolder->GetMetaObject(); }
	virtual bool IsDataFolder(const ibMetaID& id) const { return id == (*m_propertyAttributeIsFolder)->GetMetaID(); }

	virtual bool HasQuickChoice() const { return m_propertyQuickChoice->GetValueAsBoolean(); }

	ibValueMetaObjectRecordDataHierarchyMutableRef();
	virtual ~ibValueMetaObjectRecordDataHierarchyMutableRef();

	//process choice 
	virtual bool ProcessChoice(ibBackendControlFrame* ownerValue,
		const wxString& strFormName = wxEmptyString, ibSelectMode selMode = ibSelectMode::ibSelectMode_Items);

	virtual bool FilterChild(const ibClassID& clsid) const {
		if (clsid == g_metaAttributeCLSID ||
			clsid == g_metaTableCLSID)
			return true;
		return ibValueMetaObjectGenericData::FilterChild(clsid);
	}

	//events: 
	virtual bool OnCreateMetaObject(ibMetaData* metaData, int flags);
	virtual bool OnLoadMetaObject(ibMetaData* metaData);
	virtual bool OnSaveMetaObject(int flags);
	virtual bool OnDeleteMetaObject();

	//module manager is started or exit 
	virtual bool OnBeforeRunMetaObject(int flags);
	virtual bool OnAfterRunMetaObject(int flags);

	virtual bool OnBeforeCloseMetaObject();
	virtual bool OnAfterCloseMetaObject();

	//is predefined value? 
	bool HasPredefinedValue(const ibGuid& valueGuid) const { return FindPredefinedValue(valueGuid) != nullptr; }
	bool HasPredefinedValue(const wxString& strPredefinedName) const { return FindPredefinedValue(strPredefinedName) != nullptr; }

	//create associate value 	
	ibValueRecordDataObjectHierarchyRef* CreateObjectValue(ibObjectMode mode);
	ibValueRecordDataObjectHierarchyRef* CreateObjectValue(ibObjectMode mode, const ibGuid& guid);
	ibValueRecordDataObjectHierarchyRef* CreateObjectValue(ibObjectMode mode, ibValueRecordDataObjectRef* objSrc, bool generate = false);

	//copy associate value 	
	ibValueRecordDataObjectHierarchyRef* CopyObjectValue(ibObjectMode mode, const ibGuid& guid);

#pragma region _form_builder_h_
	//support form 
	virtual ibBackendValueForm* GetFolderForm(const wxString& strFormName = wxEmptyString, ibBackendControlFrame* ownerControl = nullptr, const ibUniqueKey& formGuid = wxNullGuid) = 0;
	virtual ibBackendValueForm* GetFolderSelectForm(const wxString& strFormName = wxEmptyString, ibBackendControlFrame* ownerControl = nullptr, const ibUniqueKey& formGuid = wxNullGuid) = 0;
#pragma endregion 

	//append predefined value
	void AppendPredefinedValue(const wxString& strPredefinedName,
		const wxString& strCode, const wxString& strDescription,
		bool valueIsFolder = false, const wxObjectDataPtr<ibPredefinedValueObject>& valueParent = wxObjectDataPtr<ibPredefinedValueObject>());

	void SetPredefinedValue(const ibGuid& predefinedGuid,
		const wxString& strPredefinedName,
		const wxString& strCode, const wxString& strDescription,
		bool valueIsFolder = false, const wxObjectDataPtr<ibPredefinedValueObject>& valueParent = wxObjectDataPtr<ibPredefinedValueObject>());

	void DeletePredefinedValue(const ibGuid& predefinedGuid);

	//find predefined value
	wxObjectDataPtr<ibPredefinedValueObject> FindPredefinedValue(const ibGuid& predefinedGuid) const {

		auto iterator = std::find_if(m_predefinedObjectVector.begin(), m_predefinedObjectVector.end(),
			[predefinedGuid](const auto& value) { return predefinedGuid == value->GetPredefinedGuid(); });

		if (iterator != m_predefinedObjectVector.end())
			return *iterator;

		return wxObjectDataPtr<ibPredefinedValueObject>();
	}

	wxObjectDataPtr<ibPredefinedValueObject> FindPredefinedValue(const wxString& predefinedName) const {

		auto iterator = std::find_if(m_predefinedObjectVector.begin(), m_predefinedObjectVector.end(),
			[predefinedName](const auto& value) { return predefinedName == value->GetPredefinedName(); });

		if (iterator != m_predefinedObjectVector.end())
			return *iterator;

		return wxObjectDataPtr<ibPredefinedValueObject>();
	}

	//predefined values 
	const std::vector<wxObjectDataPtr<ibPredefinedValueObject>>& GetPredefinedValueArray() const { return m_predefinedObjectVector; }

protected:

	/**
	* Property events
	*/
	virtual void OnPropertyRefresh(class wxPropertyGridManager* pg, class wxPGProperty* pgProperty, ibProperty* property);

	//create and update table 
	virtual bool CreateAndUpdateTableDB(ibMetaDataConfiguration* srcMetaData, ibValueMetaObject* srcMetaObject, int flags);

	//load & save metaData from DB 
	virtual bool LoadData(ibReaderMemory& reader);
	virtual bool SaveData(ibWriterMemory& writer);

	//create empty object
	virtual ibValueRecordDataObjectHierarchyRef* CreateObjectRefValue(ibObjectMode mode, const ibGuid& objGuid = wxNullGuid) = 0; //create object and read by guid 
	virtual ibValueRecordDataObjectRef* CreateObjectRefValue(const ibGuid& objGuid = wxNullGuid) final;

protected:

	//predefined array 
	virtual bool FillArrayObjectByPredefined(std::vector<ibValueMetaObjectAttributeBase*>& array) const {

		array = {
			m_propertyAttributePredefined->GetMetaObject(),
			m_propertyAttributeCode->GetMetaObject(),
			m_propertyAttributeDescription->GetMetaObject(),
			m_propertyAttributeParent->GetMetaObject(),
			m_propertyAttributeIsFolder->GetMetaObject(),
			m_propertyAttributeReference->GetMetaObject(),
			m_propertyAttributeDeletionMark->GetMetaObject(),
		};

		return true;
	}

	//searched array 
	virtual bool FillArrayObjectBySearched(std::vector<ibValueMetaObjectAttributeBase*>& array) const {

		array = {
			m_propertyAttributeCode->GetMetaObject(),
			m_propertyAttributeDescription->GetMetaObject(),
		};

		return true;
	}

	//process default query
	int ProcessPredefinedValue(const wxString& tableName, const wxObjectDataPtr<ibPredefinedValueObject>& srcPredefined, const wxObjectDataPtr<ibPredefinedValueObject>& dstPredefined);

	//create default attributes
	ibPropertyInnerAttribute<>* m_propertyAttributePredefined = ibPropertyObject::CreateProperty<ibPropertyInnerAttribute<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateString(wxT("PredefinedName"), _("Predefined name"), wxEmptyString, 150, ibItemMode::ibItemMode_Folder_Item));
	ibPropertyInnerAttribute<>* m_propertyAttributeCode = ibPropertyObject::CreateProperty<ibPropertyInnerAttribute<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateString(wxT("Code"), _("Code"), wxEmptyString, 8, true, ibItemMode::ibItemMode_Folder_Item));
	ibPropertyInnerAttribute<>* m_propertyAttributeDescription = ibPropertyObject::CreateProperty<ibPropertyInnerAttribute<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateString(wxT("Description"), _("Description"), wxEmptyString, 150, true, ibItemMode::ibItemMode_Folder_Item));
	ibPropertyInnerAttribute<>* m_propertyAttributeParent = ibPropertyObject::CreateProperty<ibPropertyInnerAttribute<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateEmptyType(wxT("Parent"), _("Parent"), wxEmptyString, ibItemMode::ibItemMode_Folder_Item, ibSelectMode::ibSelectMode_Folders));
	ibPropertyInnerAttribute<>* m_propertyAttributeIsFolder = ibPropertyObject::CreateProperty<ibPropertyInnerAttribute<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateBoolean(wxT("IsFolder"), _("Is folder"), wxEmptyString, ibItemMode::ibItemMode_Folder_Item));

	//predefinded vector
	std::vector<wxObjectDataPtr<ibPredefinedValueObject>> m_predefinedObjectVector;
};

//meta object with key   
class BACKEND_API ibValueMetaObjectRegisterData :
	public ibValueMetaObjectGenericData {
	wxDECLARE_ABSTRACT_CLASS(ibValueMetaObjectRegisterData);
protected:
	ibValueMetaObjectRegisterData();
	virtual ~ibValueMetaObjectRegisterData();
public:

#pragma region access_generic
	virtual bool AccessRight_Show() const { return AccessRight_Read(); }
#pragma endregion

#pragma region access
	bool AccessRight_Read() const { return IsFullAccess() || AccessRight(m_roleRead); }
	bool AccessRight_Write() const { return IsFullAccess() || AccessRight(m_roleWrite); }
	bool AccessRight_Delete() const { return IsFullAccess() || AccessRight(m_roleDelete); }
#pragma endregion

	ibValueMetaObjectAttributePredefined* GetRegisterActive() const { return m_propertyAttributeLineActive->GetMetaObject(); }
	bool IsRegisterActive(const ibMetaID& id) const { return id == (*m_propertyAttributeLineActive)->GetMetaID(); }
	ibValueMetaObjectAttributePredefined* GetRegisterPeriod() const { return m_propertyAttributePeriod->GetMetaObject(); }
	bool IsRegisterPeriod(const ibMetaID& id) const { return id == (*m_propertyAttributePeriod)->GetMetaID(); }
	ibValueMetaObjectAttributePredefined* GetRegisterRecorder() const { return m_propertyAttributeRecorder->GetMetaObject(); }
	bool IsRegisterRecorder(const ibMetaID& id) const { return id == (*m_propertyAttributeRecorder)->GetMetaID(); }
	ibValueMetaObjectAttributePredefined* GetRegisterLineNumber() const { return m_propertyAttributeLineNumber->GetMetaObject(); }
	bool IsRegisterLineNumber(const ibMetaID& id) const { return id == (*m_propertyAttributeLineNumber)->GetMetaID(); }

	///////////////////////////////////////////////////////////////////

#pragma region __generic_h__

	using ibValueMetaObjectCompositeData::GetGenericAttributeArrayObject;
	//attribute
	virtual std::vector<ibValueMetaObjectAttributeBase*> GetGenericAttributeArrayObject(
		std::vector<ibValueMetaObjectAttributeBase*>& array) const {
		FillArrayObjectByPredefined(array);
		FillArrayObjectByFilter<ibValueMetaObjectAttributeBase>(array, { g_metaDimensionCLSID, g_metaResourceCLSID, g_metaAttributeCLSID });
		return array;
	}

	//dimention
	virtual std::vector<ibValueMetaObjectAttributeBase*> GetGenericDimentionArrayObject(
		std::vector<ibValueMetaObjectAttributeBase*> array = std::vector<ibValueMetaObjectAttributeBase*>()) const {
		FillArrayObjectByDimention(array);
		return array;
	}

#pragma endregion
#pragma region __array_h__

	//any attribute
	std::vector<ibValueMetaObjectAttributeBase*> GetAnyAttributeArrayObject(
		std::vector<ibValueMetaObjectAttributeBase*> array = std::vector<ibValueMetaObjectAttributeBase*>()) const {
		FillArrayObjectByPredefined(array);
		FillArrayObjectByFilter<ibValueMetaObjectAttributeBase>(array, { g_metaDimensionCLSID, g_metaResourceCLSID, g_metaAttributeCLSID });
		return array;
	}

	//dimension
	std::vector<ibValueMetaObjectDimension*> GetDimentionArrayObject(
		std::vector<ibValueMetaObjectDimension*> array = std::vector<ibValueMetaObjectDimension*>()) const {
		FillArrayObjectByFilter<ibValueMetaObjectDimension>(array, { g_metaDimensionCLSID });
		return array;
	}

	//resource
	std::vector<ibValueMetaObjectResource*> GetResourceArrayObject(
		std::vector<ibValueMetaObjectResource*> array = std::vector<ibValueMetaObjectResource*>()) const {
		FillArrayObjectByFilter<ibValueMetaObjectResource>(array, { g_metaResourceCLSID });
		return array;
	}

	//attribute
	std::vector<ibValueMetaObjectAttributeBase*> GetAttributeArrayObject(
		std::vector<ibValueMetaObjectAttributeBase*> array = std::vector<ibValueMetaObjectAttributeBase*>()) const {
		FillArrayObjectByFilter<ibValueMetaObjectAttributeBase>(array, { g_metaAttributeCLSID });
		return array;
	}

#pragma endregion
#pragma region __filter_h__

	//any attribute 
	template <typename _T1>
	ibValueMetaObjectAttributeBase* FindAnyAttributeObjectByFilter(const _T1& id) const {
		return FindObjectByFilter<ibValueMetaObjectAttributeBase>(id, { g_metaDimensionCLSID, g_metaResourceCLSID, g_metaAttributeCLSID, g_metaPredefinedAttributeCLSID });
	}

	//dimension
	template <typename _T1>
	ibValueMetaObjectDimension* FindDimensionObjectByFilter(const _T1& id) const {
		return FindObjectByFilter<ibValueMetaObjectDimension>(id, { g_metaDimensionCLSID });
	}

	//resource
	template <typename _T1>
	ibValueMetaObjectResource* FindResourceObjectByFilter(const _T1& id) const {
		return FindObjectByFilter<ibValueMetaObjectResource>(id, { g_metaResourceCLSID });
	}

	//attribute
	template <typename _T1>
	ibValueMetaObjectAttributeBase* FindAttributeObjectByFilter(const _T1& id) const {
		return FindObjectByFilter<ibValueMetaObjectResource>(id, { g_metaAttributeCLSID });
	}

#pragma endregion 

	//has record manager 
	virtual bool HasRecordManager() const { return false; }

	//has recorder and period 
	virtual bool HasPeriod() const { return false; }
	virtual bool HasRecorder() const { return true; }

	ibValueRecordKeyObject* CreateRecordKeyObjectValue();

	ibValueRecordSetObject* CreateRecordSetObjectValue(bool needInitialize = true);
	ibValueRecordSetObject* CreateRecordSetObjectValue(const ibUniqueKeyPair& uniqueKey, bool needInitialize = true);
	ibValueRecordSetObject* CreateRecordSetObjectValue(ibValueRecordSetObject* source, bool needInitialize = true);

	ibValueRecordSetObject* CopyRecordSetObjectValue(const ibUniqueKeyPair& uniqueKey);

	ibValueRecordManagerObject* CreateRecordManagerObjectValue();
	ibValueRecordManagerObject* CreateRecordManagerObjectValue(const ibUniqueKeyPair& uniqueKey);
	ibValueRecordManagerObject* CreateRecordManagerObjectValue(ibValueRecordManagerObject* source);

	ibValueRecordManagerObject* CopyRecordManagerObjectValue(const ibUniqueKeyPair& uniqueKey);

	//get module object in compose object 
	virtual ibValueMetaObjectModule* GetModuleObject() const { return nullptr; }
	virtual ibValueMetaObjectCommonModule* GetModuleManager() const { return nullptr; }

	virtual bool FilterChild(const ibClassID& clsid) const {
		if (
			clsid == g_metaDimensionCLSID ||
			clsid == g_metaResourceCLSID ||
			clsid == g_metaAttributeCLSID
			)
			return true;
		return ibValueMetaObjectGenericData::FilterChild(clsid);
	}

	//meta events
	virtual bool OnCreateMetaObject(ibMetaData* metaData, int flags);
	virtual bool OnLoadMetaObject(ibMetaData* metaData);
	virtual bool OnSaveMetaObject(int flags);
	virtual bool OnDeleteMetaObject();

	//module manager is started or exit 
	virtual bool OnBeforeRunMetaObject(int flags);
	virtual bool OnAfterCloseMetaObject();

#pragma region _form_builder_h_
	//support form 
	virtual ibBackendValueForm* GetListForm(const wxString& strFormName = wxEmptyString, ibBackendControlFrame* ownerControl = nullptr, const ibUniqueKey& formGuid = wxNullGuid) = 0;
#pragma endregion 

	//special functions for DB 
	virtual wxString GetTableNameDB() const;

	// load & save config data 
	virtual bool LoadTableData(const ibReaderMemory& reader);
	virtual bool SaveTableData(ibWriterMemory& writer) const;

protected:

	//update current record
	bool UpdateCurrentRecords(const wxString& tableName, ibValueMetaObjectRegisterData* dst);

	//get default form 
	virtual ibBackendValueForm* GetFormByCommandType(ibInterfaceCommandType cmdType = ibInterfaceCommandType::ibInterfaceCommandType_Default) {

		//if (cmdType == ibInterfaceCommandType::ibInterfaceCommandType_Create)
		//	return GetObjectRecord();
		//else if (cmdType == ibInterfaceCommandType::ibInterfaceCommandType_List)
		//	return GetListForm();

		return GetListForm();
	}

	///////////////////////////////////////////////////////////////////

	//get default attributes
	virtual bool FillArrayObjectByPredefined(std::vector<ibValueMetaObjectAttributeBase*>& array) const {
		return false;
	}

	//get dimension keys 
	virtual bool FillArrayObjectByDimention(
		std::vector<ibValueMetaObjectAttributeBase*>& array) const {
		FillArrayObjectByFilter<ibValueMetaObjectAttributeBase>(array, { g_metaDimensionCLSID });
		return true;
	}

	///////////////////////////////////////////////////////////////////

	virtual ibValueRecordSetObject* CreateRecordSetObjectRegValue(const ibUniqueKeyPair& uniqueKey = wxNullUniquePairKey) = 0;
	virtual ibValueRecordManagerObject* CreateRecordManagerObjectRegValue(const ibUniqueKeyPair& uniqueKey = wxNullUniquePairKey) { return nullptr; }

	//create and update table 
	virtual bool CreateAndUpdateTableDB(ibMetaDataConfiguration* srcMetaData, ibValueMetaObject* srcMetaObject, int flags);

	//process default query
	int ProcessDimension(const wxString& tableName, const ibValueMetaObjectAttributeBase* srcAttr, const ibValueMetaObjectAttributeBase* dstAttr);
	int ProcessResource(const wxString& tableName, const ibValueMetaObjectAttributeBase* srcAttr, const ibValueMetaObjectAttributeBase* dstAttr);
	int ProcessAttribute(const wxString& tableName, const ibValueMetaObjectAttributeBase* srcAttr, const ibValueMetaObjectAttributeBase* dstAttr);

	//load & save metaData from DB 
	virtual bool LoadData(ibReaderMemory& reader);
	virtual bool SaveData(ibWriterMemory& writer);
	virtual bool DeleteData() { return true; }

protected:

	//create default attributes
	ibPropertyInnerAttribute<>* m_propertyAttributeLineActive = ibPropertyObject::CreateProperty<ibPropertyInnerAttribute<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateBoolean(wxT("Active"), _("Active"), wxEmptyString, false, true));
	ibPropertyInnerAttribute<>* m_propertyAttributePeriod = ibPropertyObject::CreateProperty<ibPropertyInnerAttribute<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateDate(wxT("Period"), _("Period"), wxEmptyString, ibDateFractions::ibDateFractions_DateTime, true));
	ibPropertyInnerAttribute<>* m_propertyAttributeRecorder = ibPropertyObject::CreateProperty<ibPropertyInnerAttribute<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateEmptyType(wxT("Recorder"), _("Recorder"), wxEmptyString));
	ibPropertyInnerAttribute<>* m_propertyAttributeLineNumber = ibPropertyObject::CreateProperty<ibPropertyInnerAttribute<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateNumber(wxT("LineNumber"), _("Line number"), wxEmptyString, 15, 0));

private:

#pragma region role
	ibRole* m_roleRead = ibValueMetaObject::CreateRole(wxT("Read"), _("Read"));
	ibRole* m_roleWrite = ibValueMetaObject::CreateRole(wxT("Write"), _("Write"));
	ibRole* m_roleDelete = ibValueMetaObject::CreateRole(wxT("Delete"), _("Delete"));
#pragma endregion
};

//********************************************************************************************
//*                                      Base data                                           *
//********************************************************************************************

#include "backend/srcExplorer.h"

class BACKEND_API ibSourceDataObject : public ibSourceObject {
public:

	virtual ~ibSourceDataObject() {}

	//override default type object 
	virtual bool IsNewObject() const { return true; }

	//standart override 
	virtual bool IsEmpty() const = 0;

	//standart override 
	virtual bool IsModified() const { return false; }
	virtual void Modify(bool mod) {}

	//save source modify  
	virtual bool SaveModify() { return true; }

	//check is changes data in db
	virtual bool ModifiesData() { return false; }

	//get unique identifier 
	virtual ibUniqueKey GetGuid() const = 0;

	//get metaData from object 
	virtual ibValueMetaObjectGenericData* GetSourceMetaObject() const = 0;

	//support source data 
	virtual ibSourceExplorer GetSourceExplorer() const = 0;
	virtual bool GetModel(ibValueModel*& tableValue, const ibMetaID& id) = 0;

	//support source set/get data 
	virtual bool SetValueByMetaID(const ibMetaID& id, const ibValue& varMetaVal) { return false; }
	virtual bool GetValueByMetaID(const ibMetaID& id, ibValue& pvarMetaVal) const { return false; }

	//counter
	virtual void SourceIncrRef() = 0;
	virtual void SourceDecrRef() = 0;
};

//********************************************************************************************
//*                                      Object                                              *
//********************************************************************************************

#pragma region __transaction_guard_h__

template <typename db_type = class BACKEND_API ibDatabaseLayer>
struct ibTransactionGuard
{
	ibTransactionGuard() : m_db(ibApplicationData::GetDatabaseLayer()), m_active_transaction(false) {}
	ibTransactionGuard(const std::shared_ptr<db_type>& db) : m_db(db), m_active_transaction(false) {}
	~ibTransactionGuard() { RollBackTransaction(); }

	/// Begin a transaction
	void BeginTransaction() {
		if (!m_active_transaction && m_db != nullptr)
			m_db->BeginTransaction();
		m_active_transaction = true;
	}

	/// Commit the current transaction
	void CommitTransaction() {
		if (m_active_transaction && m_db != nullptr)
			m_db->Commit();
		m_active_transaction = false;
	}

	/// Rollback the current transaction
	void RollBackTransaction() {
		if (m_active_transaction && m_db != nullptr)
			m_db->RollBack();
		m_active_transaction = false;
	}

private:
	bool m_active_transaction;
	std::shared_ptr<db_type> m_db;
};

#pragma endregion 

//manager with meta object  
#pragma region managers

class BACKEND_API ibValueManagerObject : public ibValue {
public:

	ibValueManagerObject() : ibValue(ibValueTypes::TYPE_VALUE, true) {}
	virtual ~ibValueManagerObject() {}

	virtual ibValueMetaObject* GetMetaObject() const = 0;
};

class BACKEND_API ibValueManagerDataObject : public ibValueManagerObject {
public:

	ibValueManagerDataObject() : ibValueManagerObject(), m_methodHelper(new ibValueMethodHelper) {}
	virtual ~ibValueManagerDataObject() { wxDELETE(m_methodHelper); }

	virtual ibValueMetaObjectCommonModule* GetModuleManager() const = 0;
	virtual ibValueMetaObjectGenericData* GetMetaObject() const = 0;

	virtual ibValueMethodHelper* GetPMethods() const { // get a reference to the class helper for parsing attribute and method names
		//PrepareNames(); 
		return m_methodHelper;
	}

	virtual void PrepareNames() const;                         // this method is automatically called to initialize attribute and method names.

	virtual bool CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray);//method call
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);//method call

	//Get ref class 
	virtual ibClassID GetClassType() const;

	virtual wxString GetClassName() const;
	virtual wxString GetString() const;

protected:
	//methods 
	ibValueMethodHelper* m_methodHelper;
};

class BACKEND_API ibValueManagerDataObjectPredefined : public ibValueManagerDataObject {

public:

	virtual ibValueMetaObjectRecordDataHierarchyMutableRef* GetMetaObject() const = 0;

	virtual void PrepareNames() const; // this method is automatically called to initialize attribute and method names.

	virtual bool SetPropVal(const long lPropNum, ibValue& varPropVal);        //setting attribute
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);                   //attribute value
};

#pragma endregion 

//object with metaobject 
#pragma region objects 
class BACKEND_API ibValueRecordDataObject : public ibValue, public ibActionDataObject,
	public ibSourceDataObject, public ibValueDataObject, public ibModuleDataObject {
	wxDECLARE_ABSTRACT_CLASS(ibValueRecordDataObject);
protected:
	enum helperAlias {
		eSystem,
		eProperty,
		eTable,
		eProcUnit
	};
	enum helperProp {
		eThisObject
	};
	enum helperFunc {
		eGetFormObject,
		enGetTemplate,
		eGetMetadata
	};
protected:
	//override copy constructor
	ibValueRecordDataObject(const ibGuid& objGuid, bool newObject);
	ibValueRecordDataObject(const ibValueRecordDataObject& source);

	//standart override 
	virtual ibValueMethodHelper* GetPMethods() const final { // get a reference to the class helper for parsing attribute and method names
		//PrepareNames(); 
		return m_methodHelper;
	}
public:
	virtual ~ibValueRecordDataObject();

	//support actionData
	virtual ibActionCollection GetActionCollection(const ibFormID& formType) override { return ibActionCollection(); }
	virtual void ExecuteAction(const ibActionID& lNumAction, ibBackendValueForm* srcForm) override {}

	virtual ibValueRecordDataObject* CopyObjectValue() = 0;

	//standart override
	virtual void PrepareNames() const override;

	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal) override;
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal) override;

	virtual bool CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray) override;
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray) override;

	//check is empty
	virtual bool IsEmpty() const override { return ibValue::IsEmpty(); }

	//get metaData from object 
	virtual ibValueMetaObjectGenericData* GetSourceMetaObject() const final { return GetMetaObject(); }

	//Get ref class 
	virtual ibClassID GetSourceClassType() const final { return GetClassType(); }

	//Get presentation 
	virtual wxString GetSourceCaption() const override {
		return GetMetaObject() ? stringUtils::GenerateSynonym(GetMetaObject()->GetClassName()) + wxT(": ") + GetMetaObject()->GetSynonym() : GetString();
	}

	//support source data
	virtual ibSourceExplorer GetSourceExplorer() const override;
	virtual bool GetModel(ibValueModel*& tableValue, const ibMetaID& id) override;

	//support source set/get data
	virtual bool SetValueByMetaID(const ibMetaID& id, const ibValue& varMetaVal) override;
	virtual bool GetValueByMetaID(const ibMetaID& id, ibValue& pvarMetaVal) const override;

	ibValue GetValueByMetaID(const ibMetaID& id) const {
		ibValue retValue;
		if (GetValueByMetaID(id, retValue))
			return retValue;
		return ibValue();
	}

	//support tabular section
	virtual ibValueModelTableBase* GetTableByMetaID(const ibMetaID& id) const;

	//counter
	virtual void SourceIncrRef() override { ibValue::IncrRef(); }
	virtual void SourceDecrRef() override { ibValue::DecrRef(); }

	//get metaData from object
	virtual ibValueMetaObjectRecordData* GetMetaObject() const override = 0;

	//get unique identifier
	virtual ibUniqueKey GetGuid() const override = 0;

	//get frame
	virtual ibBackendValueForm* GetForm() const;

#pragma region _form_builder_h_
	//support show 
	virtual void ShowFormValue(const wxString& strFormName = wxEmptyString, ibBackendControlFrame* ownerControl = nullptr) = 0;
	virtual ibBackendValueForm* GetFormValue(const wxString& strFormName = wxEmptyString, ibBackendControlFrame* ownerControl = nullptr) = 0;
#pragma endregion 

	//default showing
	virtual void ShowValue() override { ShowFormValue(); }

	//save modify 
	virtual bool SaveModify() override { return true; }

	//Get ref class
	virtual ibClassID GetClassType() const override;

	virtual wxString GetClassName() const override;
	virtual wxString GetString() const override;

	//Working with iterators
	virtual bool HasIterator() const override { return true; }
	virtual ibValue GetIteratorAt(unsigned int lPropNum) override { ibValue retValue; GetPropVal(lPropNum, retValue); return retValue; }
	virtual unsigned int GetIteratorCount() const override { return m_methodHelper != nullptr ? m_methodHelper->GetNProps() : 0; }

protected:
	virtual void PrepareEmptyObject();
protected:
	friend class ibMetaData;
	friend class ibValueMetaObjectRecordData;
protected:
	ibValueMethodHelper* m_methodHelper;
};

//Object with file
class BACKEND_API ibValueRecordDataObjectExt : public ibValueRecordDataObject {
	wxDECLARE_ABSTRACT_CLASS(ibValueRecordDataObjectExt);
protected:
	//override copy constructor
	ibValueRecordDataObjectExt(ibValueMetaObjectRecordDataExt* metaObject);
	ibValueRecordDataObjectExt(const ibValueRecordDataObjectExt& source);
public:
	virtual ~ibValueRecordDataObjectExt();

	bool InitializeObject();
	bool InitializeObject(ibValueRecordDataObjectExt* source);

	//get unique identifier 
	virtual ibUniqueKey GetGuid() const { return m_objGuid; }

	//check is empty
	virtual bool IsEmpty() const { return false; }

	//copy new object
	virtual ibValueRecordDataObjectExt* CopyObjectValue();

	//get metaData from object 
	virtual ibValueMetaObjectRecordDataExt* GetMetaObject() const { return m_metaObject; }

protected:
	ibValueMetaObjectRecordDataExt* m_metaObject;
};

//Object with reference type 
class BACKEND_API ibValueRecordDataObjectRef : public ibValueRecordDataObject {
	wxDECLARE_ABSTRACT_CLASS(ibValueRecordDataObjectRef);
protected:

	// code generator
	ibValue GenerateNextIdentifier(
		ibValueMetaObjectAttributeBase* attribute, const wxString& strPrefix);

protected:
	ibValueRecordDataObjectRef(ibValueMetaObjectRecordDataMutableRef* metaObject, const ibGuid& objGuid);
	ibValueRecordDataObjectRef(const ibValueRecordDataObjectRef& src);
public:

	virtual ~ibValueRecordDataObjectRef();

	bool InitializeObject(const ibGuid& copyGuid = wxNullGuid);
	bool InitializeObject(ibValueRecordDataObjectRef* source, bool generate = false);

	virtual bool WriteObject() = 0;
	virtual bool DeleteObject() = 0;

	//Get ref class 
	virtual ibClassID GetClassType() const;

	virtual wxString GetClassName() const;
	virtual wxString GetString() const;

	//is new object?
	virtual bool IsNewObject() const { return m_newObject; }

	//check is empty
	virtual bool IsEmpty() const {
		return !m_objGuid.isValid();
	}


	//is modified 
	virtual bool IsModified() const { return m_objModified; }

	//set modify 
	virtual void Modify(bool mod);

	//Get presentation 
	virtual wxString GetSourceCaption() const {
		return m_metaObject->GetSynonym() + wxT(": ") + (IsNewObject() ? _("Creating") : GetString());
	}

	//support source data 
	virtual ibSourceExplorer GetSourceExplorer() const;
	virtual bool GetModel(ibValueModel*& tableValue, const ibMetaID& id);

	//get metaData from object 
	virtual ibValueMetaObjectRecordDataMutableRef* GetMetaObject() const { return m_metaObject; }

	//check is changes data in db
	virtual bool ModifiesData() { return true; }

	//default methods
	virtual bool Generate();

	//filling object 
	virtual bool Filling(ibValue cValue = ibValue()) const;

	//support source set/get data 
	virtual bool SetValueByMetaID(const ibMetaID& id, const ibValue& varMetaVal);
	virtual bool GetValueByMetaID(const ibMetaID& id, ibValue& pvarMetaVal) const;

	ibValue GetValueByMetaID(const ibMetaID& id) const {
		ibValue retValue;
		if (GetValueByMetaID(id, retValue))
			return retValue;
		return ibValue();
	}

	//get unique identifier 
	virtual ibUniqueKey GetGuid() const { return m_objGuid; }

	//copy new object
	virtual ibValueRecordDataObjectRef* CopyObjectValue();

	//get reference
	virtual class ibValueReferenceDataObject* GetReference() const;

public:
	virtual void SetDeletionMark(bool deletionMark = true);
protected:

	virtual bool ReadData();
	virtual bool ReadData(const ibGuid& srcGuid);
	virtual bool SaveData();
	virtual bool DeleteData();

	//code/number generator 
	virtual bool IsSetUniqueIdentifier() const;

	virtual bool GenerateUniqueIdentifier(const wxString& strPrefix);
	virtual bool ResetUniqueIdentifier();

protected:
	virtual void PrepareEmptyObject();
	virtual void PrepareEmptyObject(const ibValueRecordDataObjectRef* source);
protected:

	friend class ibValueTabularSectionDataObjectRef;

	bool m_objModified;
	ibValueMetaObjectRecordDataMutableRef* m_metaObject;
	ibReference* m_reference_impl;
};

//Object with reference type and group/object type 
class BACKEND_API ibValueRecordDataObjectHierarchyRef : public ibValueRecordDataObjectRef {
	wxDECLARE_ABSTRACT_CLASS(ibValueRecordDataObjectHierarchyRef);
protected:
	ibValueRecordDataObjectHierarchyRef(ibValueMetaObjectRecordDataHierarchyMutableRef* metaObject, const ibGuid& objGuid, ibObjectMode objMode = ibObjectMode::OBJECT_ITEM);
	ibValueRecordDataObjectHierarchyRef(const ibValueRecordDataObjectHierarchyRef& src);
public:
	virtual ~ibValueRecordDataObjectHierarchyRef();

	//support source data 
	virtual ibSourceExplorer GetSourceExplorer() const;
	virtual bool GetModel(ibValueModel*& tableValue, const ibMetaID& id);

	//Get presentation 
	virtual wxString GetSourceCaption() const {
		return m_metaObject->GetSynonym() + wxT(": ") + (IsNewObject() ? _("Creating") : GetString());
	}

	//get metaData from object 
	virtual ibValueMetaObjectRecordDataHierarchyMutableRef* GetMetaObject() const {
		return static_cast<ibValueMetaObjectRecordDataHierarchyMutableRef*>(m_metaObject);
	}

	//copy new object
	virtual ibValueRecordDataObjectRef* CopyObjectValue();

	//support source set/get data 
	virtual bool SetValueByMetaID(const ibMetaID& id, const ibValue& varMetaVal);
	virtual bool GetValueByMetaID(const ibMetaID& id, ibValue& pvarMetaVal) const;

	ibValue GetValueByMetaID(const ibMetaID& id) const {
		ibValue retValue;
		if (GetValueByMetaID(id, retValue))
			return retValue;
		return ibValue();
	}

protected:
	virtual bool ReadData();
	virtual bool ReadData(const ibGuid& srcGuid);
protected:
	virtual void PrepareEmptyObject();
	virtual void PrepareEmptyObject(const ibValueRecordDataObjectRef* source);
protected:
	//folder or object catalog
	ibObjectMode m_objMode;
};
#pragma endregion

//object with register type 
#pragma region registers 
class BACKEND_API ibValueRecordKeyObject : public ibValue {
	wxDECLARE_ABSTRACT_CLASS(ibValueRecordKeyObject);
public:
	ibValueRecordKeyObject(ibValueMetaObjectRegisterData* metaObject);
	virtual ~ibValueRecordKeyObject();

	//standart override
	virtual ibValueMethodHelper* GetPMethods() const override { // get a reference to the class helper for parsing attribute and method names
		//PrepareNames();
		return m_methodHelper;
	}

	virtual void PrepareNames() const override;
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray) override;

	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal) override;
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal) override;

	//check is empty
	virtual bool IsEmpty() const override;

	//Get unique key
	ibUniqueKeyPair GetUniqueKey() { return ibUniqueKeyPair(m_metaObject, m_keyValues); }

	//get metaData from object
	virtual ibValueMetaObjectRegisterData* GetMetaObject() const { return m_metaObject; };

	//Get ref class
	virtual ibClassID GetClassType() const override;

	virtual wxString GetClassName() const override;
	virtual wxString GetString() const override;

	//Working with iterators
	virtual bool HasIterator() const override { return true; }
	virtual ibValue GetIteratorAt(unsigned int lPropNum) override { ibValue retValue; GetPropVal(lPropNum, retValue); return retValue; }
	virtual unsigned int GetIteratorCount() const override { return m_methodHelper != nullptr ? m_methodHelper->GetNProps() : 0; }

protected:
	ibValueMetaObjectRegisterData* m_metaObject;
	ibMetaValueArray m_keyValues;
	ibValueMethodHelper* m_methodHelper;
};

class BACKEND_API ibValueRecordSetObject : public ibValueModelTableBase, public ibModuleDataObject {
	wxDECLARE_ABSTRACT_CLASS(ibValueRecordSetObject);
public:

	virtual ibValueModelColumnCollection* GetColumnCollection() const override { return m_recordColumnCollection; }
	virtual ibValueModelReturnLine* GetRowAt(const ibDataViewItem& line) override {
		if (!line.IsOk())
			return nullptr;
		return ibValue::CreateAndPrepareValueRef<ibValueRecordSetObjectRegisterReturnLine>(this, line);
	}

	class ibValueRecordSetObjectRegisterColumnCollection : public ibValueModelTableBase::ibValueModelColumnCollection {
		wxDECLARE_DYNAMIC_CLASS(ibValueRecordSetObjectRegisterColumnCollection);
	public:

		class ibValueRecordSetRegisterColumnInfo : public ibValueModelTableBase::ibValueModelColumnCollection::ibValueModelColumnInfo {
			wxDECLARE_DYNAMIC_CLASS(ibValueRecordSetRegisterColumnInfo);
		public:

			ibValueRecordSetRegisterColumnInfo();
			ibValueRecordSetRegisterColumnInfo(ibValueMetaObjectAttributeBase* attribute);
			virtual ~ibValueRecordSetRegisterColumnInfo();

			virtual unsigned int GetColumnID() const { return m_metaAttribute->GetMetaID(); }
			virtual wxString GetColumnName() const { return m_metaAttribute->GetName(); }
			virtual wxString GetColumnCaption() const { return m_metaAttribute->GetSynonym(); }
			virtual const ibTypeDescription GetColumnType() const { return m_metaAttribute->GetTypeDesc(); }

			friend ibValueRecordSetObjectRegisterColumnCollection;

		private:
			ibValueMetaObjectAttributeBase* m_metaAttribute;
		};

	public:

		ibValueRecordSetObjectRegisterColumnCollection();
		ibValueRecordSetObjectRegisterColumnCollection(ibValueRecordSetObject* ownerTable);
		virtual ~ibValueRecordSetObjectRegisterColumnCollection();

		virtual const ibTypeDescription GetColumnType(unsigned int col) const {
			ibValueRecordSetRegisterColumnInfo* columnInfo = m_listColumnInfo.at(col);
			wxASSERT(columnInfo);
			return columnInfo->GetColumnType();
		}

		virtual ibValueModelColumnInfo* GetColumnInfo(unsigned int idx) const override {
			if (m_listColumnInfo.size() < idx)
				return nullptr;
			auto it = m_listColumnInfo.begin();
			std::advance(it, idx);
			return it->second;
		}

		virtual unsigned int GetColumnCount() const override {
			ibValueMetaObjectGenericData* metaTable = m_ownerTable->GetMetaObject();
			wxASSERT(metaTable);
			std::vector<ibValueMetaObjectAttributeBase*> genArr;
			const auto obj = metaTable->GetGenericAttributeArrayObject(genArr);
			return obj.size();
		}

		//array support
		virtual bool SetAt(const ibValue& varKeyValue, const ibValue& varValue) override;
		virtual bool GetAt(const ibValue& varKeyValue, ibValue& pvarValue) override;

		friend class ibValueRecordSetObject;

	protected:
		ibValueRecordSetObject* m_ownerTable;
		std::map<ibMetaID, ibValuePtr<ibValueRecordSetRegisterColumnInfo>> m_listColumnInfo;
		ibValueMethodHelper* m_methodHelper;
	};

	class ibValueRecordSetObjectRegisterReturnLine : public ibValueModelReturnLine {
		wxDECLARE_DYNAMIC_CLASS(ibValueRecordSetObjectRegisterReturnLine);
	public:

		ibValueRecordSetObjectRegisterReturnLine(ibValueRecordSetObject* ownerTable = nullptr,
			const ibDataViewItem& line = ibDataViewItem(nullptr));
		virtual ~ibValueRecordSetObjectRegisterReturnLine();

		virtual ibValueModelTableBase* GetOwnerModel() const { return m_ownerTable; }

		virtual ibValueMethodHelper* GetPMethods() const { // get a reference to the class helper for parsing attribute and method names
			//PrepareNames(); 
			return m_methodHelper;
		}

		virtual void PrepareNames() const;

		virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal); //setting attribute
		virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal); //attribute value

		//Get ref class 
		virtual ibClassID GetClassType() const;

		virtual wxString GetClassName() const;
		virtual wxString GetString() const;

		friend class ibValueRecordSetObject;
	private:
		ibValueRecordSetObject* m_ownerTable;
		ibValueMethodHelper* m_methodHelper;
	};

	class ibValueRecordSetObjectRegisterKeyValue : public ibValue {
		wxDECLARE_DYNAMIC_CLASS(ibValueRecordSetObjectRegisterKeyValue);
	public:
		class ibValueRecordSetObjectRegisterKeyDescriptionValue : public ibValue {
			wxDECLARE_DYNAMIC_CLASS(ibValueRecordSetObjectRegisterKeyDescriptionValue);
		public:

			ibValueRecordSetObjectRegisterKeyDescriptionValue(ibValueRecordSetObject* recordSet = nullptr, const ibMetaID& id = wxNOT_FOUND);
			virtual ~ibValueRecordSetObjectRegisterKeyDescriptionValue();

			virtual bool IsEmpty() const { return false; }

			//****************************************************************************
			//*                              Support methods                             *
			//****************************************************************************

			virtual ibValueMethodHelper* GetPMethods() const { // get a reference to the class helper for parsing attribute and method names
				//PrepareNames(); 
				return m_methodHelper;
			}

			virtual void PrepareNames() const;                             // this method is automatically called to initialize attribute and method names
			virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);       // method call

			virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);//setting attribute
			virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);//attribute value

		protected:
			ibMetaID m_metaId;
			ibValueMethodHelper* m_methodHelper;
			ibValueRecordSetObject* m_recordSet;
		};
	public:

		ibValueRecordSetObjectRegisterKeyValue(ibValueRecordSetObject* recordSet = nullptr);
		virtual ~ibValueRecordSetObjectRegisterKeyValue();

		virtual bool IsEmpty() const { return false; }

		//****************************************************************************
		//*                              Support methods                             *
		//****************************************************************************

		virtual ibValueMethodHelper* GetPMethods() const { // get a reference to the class helper for parsing attribute and method names
			//PrepareNames(); 
			return m_methodHelper;
		}

		virtual void PrepareNames() const;                             // this method is automatically called to initialize attribute and method names
		virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);       // method call

		virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);//setting attribute
		virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);//attribute value

	protected:
		ibValueRecordSetObject* m_recordSet;
		ibValueMethodHelper* m_methodHelper;
	};

protected:
	ibValueRecordSetObject(ibValueMetaObjectRegisterData* metaObject, const ibUniqueKeyPair& uniqueKey);
	ibValueRecordSetObject(const ibValueRecordSetObject& source);
public:
	virtual ~ibValueRecordSetObject();

	void CreateEmptyKey();
	bool InitializeObject(const ibValueRecordSetObject* source = nullptr, bool newRecord = false);

	bool FindKeyValue(const ibMetaID& id) const { return m_keyValues.find(id) != m_keyValues.end(); }
	template <typename value>
	void SetKeyValue(const ibMetaID& id, value&& cValue) {
		const ibValueMetaObjectAttributeBase* attribute = m_metaObject->FindAnyAttributeObjectByFilter(id);
		wxASSERT(attribute);
		m_keyValues.insert_or_assign(
			id, attribute != nullptr ? attribute->AdjustValue(cValue) : cValue
		);
	}

	const ibValue& GetKeyValue(const ibMetaID& id) const { return m_keyValues.at(id); }
	void EraseKeyValue(const ibMetaID& id) { m_keyValues.erase(id); }

	//copy new object
	virtual ibValueRecordSetObject* CopyRegisterValue();

	bool Selected() const { return m_selected; }
	void Read() { ReadData(); }

	//standart override
	virtual ibValueMethodHelper* GetPMethods() const override { // get a reference to the class helper for parsing attribute and method names
		//PrepareNames();
		return m_methodHelper;
	}

	//counter
	virtual void SourceIncrRef() { ibValue::IncrRef(); }
	virtual void SourceDecrRef() { ibValue::DecrRef(); }

	//check is empty object?
	virtual bool IsEmpty() const override { return !m_selected; }

	//set modify
	virtual void Modify(bool mod) { m_objModified = mod; }

	//is modified
	virtual bool IsModified() const { return m_objModified; }

	//check is changes data in db
	virtual bool ModifiesData() { return true; }

	//get metaData from object
	virtual ibValueMetaObjectRegisterData* GetMetaObject() const { return m_metaObject; }

#pragma region _tabular_data_
	//get metaData from object
	virtual ibValueMetaObjectCompositeData* GetSourceMetaObject() const { return GetMetaObject(); }

	//Get ref class
	virtual ibClassID GetSourceClassType() const { return GetClassType(); }
#pragma endregion

	virtual bool AutoCreateColumn() const { return false; }
	virtual bool EditableLine(const ibDataViewItem& item, unsigned int col) const {
		return false;
	}

	virtual void ActivateItem(ibBackendValueForm* formOwner,
		const ibDataViewItem& item, unsigned int col) {
		ibValueModelTableBase::RowValueStartEdit(item, col);
	}

	virtual void AddValue(unsigned int before = 0) {}
	virtual void CopyValue() {}
	virtual void EditValue() {}
	virtual void DeleteValue() {}

	// implementation of base class virtuals to define model
	virtual void GetValueByRow(wxVariant& variant,
		const ibDataViewItem& row, unsigned int col) const override;
	virtual bool SetValueByRow(const wxVariant& variant,
		const ibDataViewItem& row, unsigned int col) override;

	//support def. methods (in runtime)
	virtual long AppendRow(unsigned int before = 0);

	virtual bool LoadDataFromTable(ibValueModelTableBase* srcTable);
	virtual ibValueModelTableBase* SaveDataToTable() const;

	//default methods
	virtual bool WriteRecordSet(bool replace = true, bool clearTable = true) = 0;
	virtual bool DeleteRecordSet() = 0;

	//array
	virtual bool GetAt(const ibValue& varKeyValue, ibValue& pvarValue) override;

	//Get ref class
	virtual ibClassID GetClassType() const override;

	virtual wxString GetClassName() const override;
	virtual wxString GetString() const override;

	//Working with iterators
	virtual bool HasIterator() const override { return true; }

	virtual ibValue GetIteratorEmpty() override {
		return ibValue::CreateAndPrepareValueRef<ibValueRecordSetObjectRegisterReturnLine>(this, ibDataViewItem(nullptr));
	}

	virtual ibValue GetIteratorAt(unsigned int idx) override {
		if (idx > (unsigned int)GetRowCount())
			return wxEmptyValue;
		return ibValue::CreateAndPrepareValueRef<ibValueRecordSetObjectRegisterReturnLine>(this, GetItem(idx));
	}

	virtual unsigned int GetIteratorCount() const override { return GetRowCount(); }

protected:

	virtual bool ExistData();
	virtual bool ExistData(ibNumber& lastNum); //for records
	virtual bool ReadData();
	virtual bool ReadData(const ibUniqueKeyPair& key);
	virtual bool SaveData(bool replace = true, bool clearTable = true);
	virtual bool DeleteData();

	////////////////////////////////////////

	virtual bool SaveVirtualTable() { return true; }
	virtual bool DeleteVirtualTable() { return true; }

protected:

	//set meta/get meta
	virtual bool SetValueByMetaID(const ibDataViewItem& item, const ibMetaID& id, const ibValue& varMetaVal) override;
	virtual bool GetValueByMetaID(const ibDataViewItem& item, const ibMetaID& id, ibValue& pvarMetaVal) const override;

protected:

	friend class ibValueRecordManagerObject;

	bool m_objModified;
	bool m_selected;

	ibMetaValueArray m_keyValues;

	ibValueMetaObjectRegisterData* m_metaObject;

	ibValuePtr<ibValueRecordSetObjectRegisterColumnCollection> m_recordColumnCollection;
	ibValuePtr<ibValueRecordSetObjectRegisterKeyValue> m_recordSetKeyValue;

	ibValueMethodHelper* m_methodHelper;
};

class BACKEND_API ibValueRecordManagerObject : public ibValue,
	public ibSourceDataObject, public ibActionDataObject {
	wxDECLARE_ABSTRACT_CLASS(ibValueRecordManagerObject);
protected:
	ibValueRecordManagerObject(ibValueMetaObjectRegisterData* metaObject, const ibUniqueKeyPair& uniqueKey);
	ibValueRecordManagerObject(const ibValueRecordManagerObject& source);
public:

	ibValueRecordSetObject* GetRecordSet() const { return m_recordSet; }

	virtual ~ibValueRecordManagerObject();

	virtual void CreateEmptyKey();

	bool InitializeObject(const ibValueRecordManagerObject* source = nullptr, bool newRecord = false);
	bool InitializeObject(const ibUniqueKeyPair& key);

	virtual bool IsNewObject() const override { return !m_recordSet->m_selected; }

	//get metaData from object
	virtual ibValueMetaObjectGenericData* GetSourceMetaObject() const final { return GetMetaObject(); }

	//Get ref class
	virtual ibClassID GetSourceClassType() const final { return GetClassType(); }

	//Get presentation
	virtual wxString GetSourceCaption() const override {
		return m_metaObject->GetSynonym() + wxT(": ") + (IsNewObject() ? _("Creating") : m_metaObject->GetSynonym());
	}

	//copy new object
	virtual ibValueRecordManagerObject* CopyRegisterValue();

	//standart override
	virtual ibValueMethodHelper* GetPMethods() const override { // get a reference to the class helper for parsing attribute and method names
		//PrepareNames();
		return m_methodHelper;
	}

	//counter
	virtual void SourceIncrRef() override { ibValue::IncrRef(); }
	virtual void SourceDecrRef() override { ibValue::DecrRef(); }

	//get unique identifier
	virtual ibUniqueKey GetGuid() const override { return m_objGuid; }

	//save modify
	virtual bool SaveModify() override { return WriteRegister(); }

	//check is changes data in db
	virtual bool ModifiesData() override { return true; }

	//default methods
	virtual bool WriteRegister(bool replace = true) = 0;
	virtual bool DeleteRegister() = 0;

	//check is empty
	virtual bool IsEmpty() const override;

	//set modify
	virtual void Modify(bool mod) override;

	//is modified
	virtual bool IsModified() const override;

	//support source set/get data
	virtual bool SetValueByMetaID(const ibMetaID& id, const ibValue& varMetaVal) override;
	virtual bool GetValueByMetaID(const ibMetaID& id, ibValue& pvarMetaVal) const override;

	ibValue GetValueByMetaID(const ibMetaID& id) const {
		ibValue retValue;
		if (GetValueByMetaID(id, retValue))
			return retValue;
		return ibValue();
	}

	//support source data
	virtual ibSourceExplorer GetSourceExplorer() const override;
	virtual bool GetModel(ibValueModel*& tableValue, const ibMetaID& id) override;

	//get metaData from object
	virtual ibValueMetaObjectRegisterData* GetMetaObject() const {
		return m_metaObject;
	};

	//get frame
	virtual ibBackendValueForm* GetForm() const;

#pragma region _form_builder_h_
	//support show 
	virtual void ShowFormValue(const wxString& strFormName = wxEmptyString, ibBackendControlFrame* ownerControl = nullptr) = 0;
	virtual ibBackendValueForm* GetFormValue(const wxString& strFormName = wxEmptyString, ibBackendControlFrame* ownerControl = nullptr) = 0;
#pragma endregion 

	//default showing
	virtual void ShowValue() override { ShowFormValue(); }

	//Get ref class
	virtual ibClassID GetClassType() const override;

	virtual wxString GetClassName() const override;
	virtual wxString GetString() const override;

	//Working with iterators
	virtual bool HasIterator() const override { return true; }
	virtual ibValue GetIteratorAt(unsigned int lPropNum) override { ibValue retValue; GetPropVal(lPropNum, retValue); return retValue; }
	virtual unsigned int GetIteratorCount() const override { return m_methodHelper != nullptr ? m_methodHelper->GetNProps() : 0; }

protected:

	virtual void PrepareEmptyObject(const ibValueRecordManagerObject* source);

protected:

	virtual bool ExistData();
	virtual bool ReadData(const ibUniqueKeyPair& key);
	virtual bool SaveData(bool replace = true);
	virtual bool DeleteData();

protected:

	ibUniqueKeyPair m_objGuid;

	ibValueMetaObjectRegisterData* m_metaObject;

	ibValuePtr<ibValueRecordSetObject> m_recordSet;
	ibValuePtr<ibValueModelTableBase::ibValueModelReturnLine> m_recordLine;

	ibValueMethodHelper* m_methodHelper;
};
#pragma endregion

#endif 