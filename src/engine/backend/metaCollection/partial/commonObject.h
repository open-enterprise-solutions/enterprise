#ifndef __COMMON_OBJECT_H__
#define __COMMON_OBJECT_H__

#include "reference/reference.h"
#include "backend/uniqueKey.h"
#include "backend/lock/lockHandle.h"
#include "backend/lock/lockTypes.h"
// documentEnum.h carries ibDocumentWriteMode / ibDocumentPostingMode —
// pulled here so ibValueRecordDataObjectRecorderRef::WriteObject can
// take typed enum args. Wrappers (ibValueEnumDocument*) stay private
// to documentEnum.h; only the plain enum values bleed up.
#include "backend/metaCollection/partial/documentEnum.h"
// ibConnectionScope is used by-reference in the Phase A Begin*/Commit*
// scaffold helper signatures on ibValueRecordDataObjectRef +
// ibValueRecordSetObject. Pulling the real header here avoids a
// global forward-decl in this header. ibBackendValueForm is already
// visible transitively via metaFormObject.h below.
#include "backend/databaseLayer/connectionScope.h"

#include "backend/query/queryableFactory.h"   // ibMetaSourceDescriptor (the L4 source descriptor field)

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

// L3 data-navigation interface — ibValueMetaObjectRecordDataRef and
// ibValueMetaObjectRegisterData implement it so the query builder is family-blind.
#include "backend/query/queryable.h"

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
	public:
	friend class ibMetaData;
public:

#pragma region access_generic
	virtual bool AccessRight_Show() const { return true; }
#pragma endregion

	virtual ibClassID ResolveChild(const ibClassID& clsid) const {
		if (clsid == g_metaFormCLSID ||
			clsid == g_metaTemplateCLSID)
			return clsid;
		return 0;
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
		ibBackendControlFrame* ownerControl = nullptr, const ibUniqueKey& formGuid = wxNullGuid) const;
#pragma endregion

#pragma region _form_creator_h_
	ibBackendValueForm* CreateAndBuildForm(const wxString& strFormName, const ibFormID& form_id = defaultFormType,
		ibBackendControlFrame* ownerControl = nullptr,
		ibSourceDataObject* srcObject = nullptr,
		const ibUniqueKey& formGuid = wxNullGuid
	) const;
#pragma endregion

#pragma region _template_builder_h_

	class ibValueSpreadsheetDocument* GetTemplate(const wxString& strFormName) const;

#pragma endregion

	virtual ibValueManagerDataObject* CreateManagerDataObjectValue() const = 0;

protected:

	//create object data with meta form
	virtual ibSourceDataObject* CreateSourceObject(const ibValueMetaObjectFormBase* metaObject) const { return nullptr; }
};

class BACKEND_API ibValueMetaObjectRecordData
	: public ibValueMetaObjectGenericData {
	public:


public:

	virtual ibClassID ResolveChild(const ibClassID& clsid) const {
		if (clsid == g_metaAttributeCLSID)
			return clsid;
		// RAM owner (processor / report): either tabular-section variant maps to MD_TBL.
		if (clsid == g_metaTableCLSID || clsid == g_metaTableRefCLSID)
			return g_metaTableCLSID;
		return ibValueMetaObjectGenericData::ResolveChild(clsid);
	}

	//get data selector 
	virtual ibSelectorDataType GetFilterDataType() const {
		return ibSelectorDataType::ibSelectorDataType_any;
	}

	//get module object in compose object 
	virtual const ibValueMetaObjectModule* GetObjectModule() const { return nullptr; }
	virtual const ibValueMetaObjectCommonModule* GetManagerModule() const { return nullptr; }

	//meta events
	virtual bool OnLoadMetaObject(ibMetaData* metaData);
	virtual bool OnSaveMetaObject(int flags);
	virtual bool OnDeleteMetaObject();

	//module manager is started or exit 
	virtual bool OnBeforeRunMetaObject(int flags);
	virtual bool OnAfterCloseMetaObject();

#pragma region __generic_h__
	
	//attribute
	std::vector<ibValueMetaObjectAttributeBase*> GetGenericAttributeArrayObject() const {
		std::vector<ibValueMetaObjectAttributeBase*> array;
		return GetGenericAttributeArrayObject(array);
	}

	virtual std::vector<ibValueMetaObjectAttributeBase*> GetGenericAttributeArrayObject(
		std::vector<ibValueMetaObjectAttributeBase*>& array) const {
		FillArrayObjectByPredefinedAttribute(array);
		FillArrayObjectByFilter<ibValueMetaObjectAttributeBase>(array, { g_metaAttributeCLSID });
		return array;
	}

	//table 
	std::vector<ibValueMetaObjectTableData*> GetGenericTableArrayObject() const {
		std::vector<ibValueMetaObjectTableData*> array;
		return GetGenericTableArrayObject(array);
	}

	virtual std::vector<ibValueMetaObjectTableData*> GetGenericTableArrayObject(
		std::vector<ibValueMetaObjectTableData*>& array) const {
		FillArrayObjectByPredefinedTable(array);
		// both RAM (MD_TBL) and DB-backed reference (MD_TBLR) tabular sections — this is the runtime
		// model-build path (records create a tabular data object per entry), so Ref tables must be here.
		FillArrayObjectByFilter<ibValueMetaObjectTableData>(array, { g_metaTableCLSID, g_metaTableRefCLSID });
		return array;
	}

	// True for an attribute that holds the object's own reference (read-only in the
	// value surface). Only reference metaobjects have one; default false lets the
	// shared record data filler stay type-agnostic (ibValueRecordDataObjectRef
	// overrides with the real test).
	virtual bool IsDataReference(const ibMetaID& /*id*/) const { return false; }

#pragma endregion

#pragma region __array_h__

	//any attribute
	std::vector<ibValueMetaObjectAttributeBase*> GetAnyAttributeArrayObject(
		std::vector<ibValueMetaObjectAttributeBase*> array = std::vector<ibValueMetaObjectAttributeBase*>()) const {
		FillArrayObjectByPredefinedAttribute(array);
		FillArrayObjectByFilter<ibValueMetaObjectAttributeBase>(array, { g_metaAttributeCLSID });
		return array;
	}

	//attribute
	std::vector<ibValueMetaObjectAttributeBase*> GetAttributeArrayObject(
		std::vector<ibValueMetaObjectAttributeBase*> array = std::vector<ibValueMetaObjectAttributeBase*>()) const {
		FillArrayObjectByFilter<ibValueMetaObjectAttributeBase>(array, { g_metaAttributeCLSID });
		return array;
	}

	//table — both the RAM (MD_TBL) and DB-backed reference (MD_TBLR) variants, collected through the base pointer
	std::vector<ibValueMetaObjectTableData*> GetTableArrayObject(
		std::vector<ibValueMetaObjectTableData*> array = std::vector<ibValueMetaObjectTableData*>()) const {
		FillArrayObjectByFilter<ibValueMetaObjectTableData>(array, { g_metaTableCLSID, g_metaTableRefCLSID });
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

	//table — RAM (MD_TBL) + DB-backed reference (MD_TBLR)
	template <typename _T1>
	ibValueMetaObjectTableData* FindTableObjectByFilter(const _T1& id) const {
		return FindObjectByFilter<ibValueMetaObjectTableData>(id, { g_metaTableCLSID, g_metaTableRefCLSID });
	}

#pragma endregion 

	//create single object
	virtual ibValueRecordDataObject* CreateRecordDataObjectValue() const = 0;

#pragma region _form_builder_h_
	//support form 
	virtual ibBackendValueForm* GetObjectForm(const wxString& strFormName = wxEmptyString, ibBackendControlFrame* ownerControl = nullptr, const ibUniqueKey& formGuid = wxNullGuid) const = 0;
#pragma endregion 

protected:

	virtual bool FillArrayObjectByPredefinedTable(
		std::vector<ibValueMetaObjectTableData*>& array) const {
		return false;
	}

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
	public:

#pragma region access_generic
	virtual bool AccessRight_Show() const { return AccessRight_Use(); }
#pragma endregion
#pragma region access
	bool AccessRight_Use() const { return IsFullAccess() || AccessRight(m_roleUse); }
#pragma endregion

	//ctor
	ibValueMetaObjectRecordDataExt();

	//пїЅreate from file?
	virtual bool IsExternalCreate() const { return false; }

	//module manager is started or exit
	virtual bool OnBeforeRunMetaObject(int flags);
	virtual bool OnAfterCloseMetaObject();

	//create associate value
	ibValueRecordDataObjectExt* CreateObjectValue() const;
	ibValueRecordDataObjectExt* CreateObjectValue(ibValueRecordDataObjectExt* objSrc) const;

	//create single object
	virtual ibValueRecordDataObject* CreateRecordDataObjectValue() const;

	//get command section
	virtual ibInterfaceCommandSection GetCommandSection() const { return ibInterfaceCommandSection::ibInterfaceCommandSection_Service; }

protected:

	//create empty object
	virtual ibValueRecordDataObjectExt* CreateObjectExtValue() const = 0;  //create object

private:
#pragma region role
	ibRole* m_roleUse = ibValueMetaObject::CreateRole(wxT("Use"), _("Use"));
#pragma endregion
};

// ibRecordQueryable — the L3 queryable for the catalog / document / charts / enums
// family. The metaobject no longer IS a queryable; it VENDS this adapter (a stable
// member), which forwards navigation to the metaobject's own query methods — so the
// concrete leaf (catalog / document / …) behaviour comes through virtual dispatch.
class ibValueMetaObjectRecordDataRef;
// A record source — a DB-family L3 queryable. It names NO attribute / L1: it returns its
// own COLUMNS (which happen to be metaobject attributes), and the DB provider does the
// physical materialisation by static_cast'ing those columns back to the attribute.
class BACKEND_API ibRecordQueryable : public ibBackendQueryable {
public:
	explicit ibRecordQueryable(const ibValueMetaObjectRecordDataRef* meta) : m_meta(meta) {}
	virtual const ibBackendQueryColumn* ResolveColumnByName(const wxString& name) const override;   // attribute-by-name AS a column
	virtual std::vector<const ibBackendQueryColumn*> GetColumns() const override;   // all attributes (SELECT *)
	virtual wxString GetQueryTableName() const override;
	virtual wxString GetQueryName() const override;   // the metaobject's user-facing name (change ledger)
	virtual ibMetaID GetQueryTableId() const override;
	virtual const ibMetaData* GetMetaData() const override;                      // metadata context for column-based value reads
	virtual std::vector<ibQuerySortItem> GetIdentitySort() const override;       // { uuid } — real column
	virtual std::vector<const ibBackendQueryColumn*> GetPrimaryKeyColumns() const override;   // { data-reference } — key authority
	virtual const ibBackendQueryable* ResolveReferenceTarget(const ibBackendQueryColumn* refColumn) const override;
	virtual std::vector<const ibBackendQueryable*> ResolveReferenceTargets(const ibBackendQueryColumn* refColumn) const override;   // composite: one per type
	virtual const ibBackendQueryColumn* GetParentColumn() const override;   // the parent attribute (hierarchy key)
private:
	const ibValueMetaObjectRecordDataRef* m_meta;
};

//meta object with reference
class BACKEND_API ibValueMetaObjectRecordDataRef : public ibValueMetaObjectRecordData, public ibBackendQueryableHolder {
	public:

protected:
	//ctor
	ibValueMetaObjectRecordDataRef();
	virtual ~ibValueMetaObjectRecordDataRef();
public:

	// The metaobject VENDS its queryable; it does NOT navigate itself. ibRecordQueryable
	// (a friend) owns the navigation, built from this metaobject's primitives
	// (FindObjectByFilter / GetPhysicalTableName / IsDataReference / guidName). L4 and the door
	// read through GetQueryable().
	virtual const ibBackendQueryable* GetQueryable() const override { return m_queryable.GetQueryable(); }
	friend class ibRecordQueryable;

	virtual ibValueMetaObjectAttributePredefined* GetDataReference() const { return m_propertyAttributeReference->GetMetaObject(); }
	virtual bool IsDataReference(const ibMetaID& id) const override { return id == (*m_propertyAttributeReference)->GetMetaID(); }

	virtual bool HasQuickChoice() const {
		return m_propertyQuickChoice->GetValueAsBoolean();
	}

	//get data selector
	virtual ibSelectorDataType GetFilterDataType() const { return ibSelectorDataType::ibSelectorDataType_reference; }

	//reference owners persist their tabular sections to the DB — they take the MD_TBLR variant
	//(processors/reports keep the RAM MD_TBL via the base RecordData::ResolveChild).
	virtual ibClassID ResolveChild(const ibClassID& clsid) const {
		if (clsid == g_metaAttributeCLSID)
			return clsid;
		// reference owner (catalog / document): either tabular-section variant maps to MD_TBLR.
		if (clsid == g_metaTableCLSID || clsid == g_metaTableRefCLSID)
			return g_metaTableRefCLSID;
		return ibValueMetaObjectGenericData::ResolveChild(clsid);
	}

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
	virtual ibValueRecordDataObject* CreateRecordDataObjectValue() const {
		wxASSERT_MSG(false, "ibValueMetaObjectRecordDataRef::CreateRecordDataObjectValue");
		return nullptr;
	}

	//process choice
	virtual bool ProcessChoice(ibBackendControlFrame* ownerValue,
		const wxString& strFormName = wxEmptyString, ibSelectMode selMode = ibSelectMode::ibSelectMode_Items) const;

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
	virtual class ibValueReferenceDataObject* FindObjectValue(const ibGuid& guid) const; //find by guid and ret reference

#pragma region _form_builder_h_
	//support form 
	virtual ibBackendValueForm* GetListForm(const wxString& strFormName = wxEmptyString, ibBackendControlFrame* ownerControl = nullptr, const ibUniqueKey& formGuid = wxNullGuid) const = 0;
	virtual ibBackendValueForm* GetSelectForm(const wxString& strFormName = wxEmptyString, ibBackendControlFrame* ownerControl = nullptr, const ibUniqueKey& formGuid = wxNullGuid) const = 0;
#pragma endregion 

	//descriptions...
	virtual wxString GetDataPresentation(const ibValueDataObject* objValue) const = 0;

	//special functions for DB
	virtual wxString GetPhysicalTableName() const;

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
	ibPropertyContainer<>* m_propertyAttributeReference = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateSpecialType(wxT("Reference"), _("Reference"), wxEmptyString, ibValue::GetIDByVT(ibValueTypes::TYPE_EMPTY)));

	// the L4 source descriptor — CONTAINS the vended queryable (stable for this metaobject's
	// life) and is registered with the factory on run / close. GetQueryable() forwards to it.
	ibMetaSourceDescriptor<ibRecordQueryable, ibValueMetaObjectRecordDataRef> m_queryable{ this };

protected:

	// "Reference" is the predefined column declared at THIS level, so its push must
	// live here. The whole reference subtree routes through
	// ibValueMetaObjectRecordDataRef::FillArrayObjectByPredefinedAttribute —
	// EnumRef overrides it, MutableRef (→ catalog / document / charts) calls it as
	// parent. If only a leaf pushed Reference, MutableRef's parent call would
	// resolve to a base without it and the whole subtree would silently drop
	// "Reference" (the regression this fixes). See the additive-contract note below.
	virtual bool FillArrayObjectByPredefinedAttribute(std::vector<ibValueMetaObjectAttributeBase*>& array) const {
		array.push_back(m_propertyAttributeReference->GetMetaObject());
		return true;
	}
};

//meta object with reference - for enumeration
class BACKEND_API ibValueMetaObjectRecordDataEnumRef : public ibValueMetaObjectRecordDataRef {
	public:
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

	// Predefined-attribute contract — ADDITIVE. Every override must
	// call its parent's FillArrayObjectByPredefinedAttribute first
	// (or push_back base entries explicitly) so the subclass list
	// always carries the inherited predefined columns. Forgetting a
	// base entry used to silently drop the attribute from runtime
	// (DataVersion bug pre-Phase-A) — the additive contract makes
	// that mistake structurally impossible.
	virtual bool FillArrayObjectByPredefinedAttribute(std::vector<ibValueMetaObjectAttributeBase*>& array) const {
		array.push_back(m_propertyAttributeReference->GetMetaObject());
		return true;
	}

	//searched array 
	virtual bool FillArrayObjectBySearched(std::vector<ibValueMetaObjectAttributeBase*>& array) const { return true; }

	// Declare the enum table (uuid key) + its values as SEED rows (diffed by uuid: new -> insert, gone -> delete).
	virtual void ContributeTables(ibSchemaSnapshot& out) const override;

	//load & save metaData from DB
	virtual bool LoadData(ibReaderMemory& reader);
	virtual bool SaveData(ibWriterMemory& writer);

private:

	//default attributes 
	ibPropertyContainer<>* m_propertyAttributeOrder = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateNumber(wxT("Order"), _("Order"), wxEmptyString, 6, true));
};

// Helper macro — emits the Read/Write/Delete role triplet + their
// AccessRight_* helpers. Used by both ibValueMetaObjectRecordDataMutableRef
// and ibValueMetaObjectRegisterData (independent branches of the tree
// share the same RWD policy). The macro is parameterised on the
// stored role-name for "Write" because MutableRef historically stored
// it as "Wrire" (legacy typo) while RegisterData stored "Write" —
// fixing the typo requires a metadata migration tracked separately.
#define IB_DECLARE_RWD_ROLE_TRIPLET(writeStorageName) \
private: \
	ibRole* m_roleRead   = ibValueMetaObject::CreateRole(wxT("Read"),   _("Read")); \
	ibRole* m_roleWrite  = ibValueMetaObject::CreateRole(wxT(writeStorageName), _("Write")); \
	ibRole* m_roleDelete = ibValueMetaObject::CreateRole(wxT("Delete"), _("Delete")); \
public: \
	bool AccessRight_Read()   const { return IsFullAccess() || AccessRight(m_roleRead);   } \
	bool AccessRight_Write()  const { return IsFullAccess() || AccessRight(m_roleWrite);  } \
	bool AccessRight_Delete() const { return IsFullAccess() || AccessRight(m_roleDelete); }

//meta object with reference and deletion mark
class BACKEND_API ibValueMetaObjectRecordDataMutableRef : public ibValueMetaObjectRecordDataRef {
	public:
protected:
	//ctor
	ibValueMetaObjectRecordDataMutableRef();
	virtual ~ibValueMetaObjectRecordDataMutableRef();
public:

#pragma region access_generic
	virtual bool AccessRight_Show() const { return AccessRight_Read(); }
#pragma endregion

	IB_DECLARE_RWD_ROLE_TRIPLET("Wrire")   // legacy storage typo — kept until migration arc

	ibMetaDescription& GetGenerationDescription() const { return m_propertyGeneration->GetValueAsMetaDesc(); }

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
	ibValueRecordDataObjectRef* CreateObjectValue() const;
	ibValueRecordDataObjectRef* CreateObjectValue(const ibGuid& guid) const;
	ibValueRecordDataObjectRef* CreateObjectValue(ibValueRecordDataObjectRef* objSrc, bool generate = false) const;

	//copy associate value
	ibValueRecordDataObjectRef* CopyObjectValue(const ibGuid& guid) const;

	//create single object
	virtual ibValueRecordDataObject* CreateRecordDataObjectValue() const;

	//get command section
	virtual ibInterfaceCommandSection GetCommandSection() const { return ibInterfaceCommandSection::ibInterfaceCommandSection_Combined; }

	// (no table-data dump / restore here — the orchestrator moves rows off the config's ContributeTables
	//  snapshot through the L3-3 mover; this object only DECLARES its tables via ContributeTables.)

protected:

	// Predefined-attribute contract is ADDITIVE (see base class doc).
	// MutableRef adds DeletionMark + DataVersion on top of Ref's
	// Reference column. Hierarchy + each leaf extend further; chain
	// of base calls guarantees no slot ever disappears silently.
	virtual bool FillArrayObjectByPredefinedAttribute(std::vector<ibValueMetaObjectAttributeBase*>& array) const override {
		ibValueMetaObjectRecordDataRef::FillArrayObjectByPredefinedAttribute(array);
		array.push_back(m_propertyAttributeDeletionMark->GetMetaObject());
		array.push_back(m_propertyAttributeDataVersion->GetMetaObject());
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

	// Declare the record's main table + its tabular-section tables into the structure snapshot.
	virtual void ContributeTables(ibSchemaSnapshot& out) const override;

	//load & save metaData from DB
	virtual bool LoadData(ibReaderMemory& reader);
	virtual bool SaveData(ibWriterMemory& writer);

	//create empty object
	virtual ibValueRecordDataObjectRef* CreateObjectRefValue(const ibGuid& objGuid = wxNullGuid) const = 0; //create object and read by guid

protected:

	ibPropertyGeneration* m_propertyGeneration = ibPropertyObject::CreateProperty<ibPropertyGeneration>(m_categoryData, wxT("ListGeneration"), _("List generation"));
	ibPropertyContainer<>* m_propertyAttributeDataVersion = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateString(wxT("DataVersion"), _("Data version"), wxEmptyString, 12, ibItemMode_Folder_Item));
	ibPropertyContainer<>* m_propertyAttributeDeletionMark = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateBoolean(wxT("DeletionMark"), _("Deletion mark"), wxEmptyString));

	// Read/Write/Delete role triplet emitted by IB_DECLARE_RWD_ROLE_TRIPLET
	// above (in the public access region).
};

//meta object with reference and deletion mark and group/object type and predefined values
class BACKEND_API ibValueMetaObjectRecordDataHierarchyMutableRef :
	public ibValueMetaObjectRecordDataMutableRef {
	public:

	class ibPredefinedValueObject : public ibDataViewObject {
	public:

		// Folder/leaf flag is stored on the predefined value itself
		// — the designer's predefined-list dialog renders these as
		// a tree, so the data-view item asks the row "are you a
		// container?" directly.  Keeps the dialog free of model-side
		// IsContainer overrides.
		virtual bool IsContainer() const override { return m_valueIsFolder; }

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
		const wxString& strFormName = wxEmptyString, ibSelectMode selMode = ibSelectMode::ibSelectMode_Items) const;

	virtual ibClassID ResolveChild(const ibClassID& clsid) const {
		if (clsid == g_metaAttributeCLSID)
			return clsid;
		// reference owner (catalog / document): either tabular-section variant maps to MD_TBLR.
		if (clsid == g_metaTableCLSID || clsid == g_metaTableRefCLSID)
			return g_metaTableRefCLSID;
		return ibValueMetaObjectGenericData::ResolveChild(clsid);
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
	ibValueRecordDataObjectHierarchyRef* CreateObjectValue(ibObjectMode mode) const;
	ibValueRecordDataObjectHierarchyRef* CreateObjectValue(ibObjectMode mode, const ibGuid& guid) const;
	ibValueRecordDataObjectHierarchyRef* CreateObjectValue(ibObjectMode mode, ibValueRecordDataObjectRef* objSrc, bool generate = false) const;

	//copy associate value
	ibValueRecordDataObjectHierarchyRef* CopyObjectValue(ibObjectMode mode, const ibGuid& guid) const;

#pragma region _form_builder_h_
	//support form 
	virtual ibBackendValueForm* GetFolderForm(const wxString& strFormName = wxEmptyString, ibBackendControlFrame* ownerControl = nullptr, const ibUniqueKey& formGuid = wxNullGuid) const = 0;
	virtual ibBackendValueForm* GetFolderSelectForm(const wxString& strFormName = wxEmptyString, ibBackendControlFrame* ownerControl = nullptr, const ibUniqueKey& formGuid = wxNullGuid) const = 0;
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

	// Declare the main table (via the base) + the predefined values as its SEED rows (cells keyed by
	// column id; the builder diffs by uuid + cells).
	virtual void ContributeTables(ibSchemaSnapshot& out) const override;

	//load & save metaData from DB 
	virtual bool LoadData(ibReaderMemory& reader);
	virtual bool SaveData(ibWriterMemory& writer);

	//create empty object
	virtual ibValueRecordDataObjectHierarchyRef* CreateObjectRefValue(ibObjectMode mode, const ibGuid& objGuid = wxNullGuid) const = 0; //create object and read by guid
	virtual ibValueRecordDataObjectRef* CreateObjectRefValue(const ibGuid& objGuid = wxNullGuid) const final;

protected:

	// Additive predefined-attribute contract (see base). Hierarchy
	// adds Predefined / Code / Description / Parent / IsFolder on top
	// of MutableRef's Reference + DeletionMark + DataVersion.
	virtual bool FillArrayObjectByPredefinedAttribute(std::vector<ibValueMetaObjectAttributeBase*>& array) const override {
		ibValueMetaObjectRecordDataMutableRef::FillArrayObjectByPredefinedAttribute(array);
		array.push_back(m_propertyAttributePredefined->GetMetaObject());
		array.push_back(m_propertyAttributeCode->GetMetaObject());
		array.push_back(m_propertyAttributeDescription->GetMetaObject());
		array.push_back(m_propertyAttributeParent->GetMetaObject());
		array.push_back(m_propertyAttributeIsFolder->GetMetaObject());
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

	//create default attributes
	ibPropertyContainer<>* m_propertyAttributePredefined = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateString(wxT("PredefinedName"), _("Predefined name"), wxEmptyString, 150, ibItemMode::ibItemMode_Folder_Item));
	ibPropertyContainer<>* m_propertyAttributeCode = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateString(wxT("Code"), _("Code"), wxEmptyString, 8, true, ibItemMode::ibItemMode_Folder_Item));
	ibPropertyContainer<>* m_propertyAttributeDescription = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateString(wxT("Description"), _("Description"), wxEmptyString, 150, true, ibItemMode::ibItemMode_Folder_Item));
	ibPropertyContainer<>* m_propertyAttributeParent = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateEmptyType(wxT("Parent"), _("Parent"), wxEmptyString, ibItemMode::ibItemMode_Folder_Item, ibSelectMode::ibSelectMode_Folders));
	ibPropertyContainer<>* m_propertyAttributeIsFolder = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateBoolean(wxT("IsFolder"), _("Is folder"), wxEmptyString, ibItemMode::ibItemMode_Folder_Item));

	//predefinded vector
	std::vector<wxObjectDataPtr<ibPredefinedValueObject>> m_predefinedObjectVector;
};

// ibRegisterDataQueryable — the L3 queryable for the register family (its main
// "records" relation). The register no longer IS a queryable; it VENDS this adapter,
// which forwards navigation to the register's own query methods. (Slices are separate
// transient queryables; this is the persistent main one.)
class BACKEND_API ibRegisterDataQueryable : public ibBackendQueryable {
public:
	explicit ibRegisterDataQueryable(const ibValueMetaObjectRegisterData* meta) : m_meta(meta) {}
	virtual const ibBackendQueryColumn* ResolveColumnByName(const wxString& name) const override;   // attribute-by-name AS a column
	virtual std::vector<const ibBackendQueryColumn*> GetColumns() const override;   // identity (recorder+line / period) ++ dims ++ generic attrs
	virtual wxString GetQueryTableName() const override;
	virtual wxString GetQueryName() const override;   // the metaobject's user-facing name (change ledger)
	virtual ibMetaID GetQueryTableId() const override;
	virtual const ibMetaData* GetMetaData() const override;                      // metadata context for column-based value reads
	virtual std::vector<ibQuerySortItem> GetIdentitySort() const override;
	virtual std::vector<const ibBackendQueryColumn*> GetPrimaryKeyColumns() const override;   // recorder+line+period / period+dims
private:
	const ibValueMetaObjectRegisterData* m_meta;
};

//meta object with key
class BACKEND_API ibValueMetaObjectRegisterData :
	public ibValueMetaObjectGenericData, public ibBackendQueryableHolder {
	public:
protected:
	ibValueMetaObjectRegisterData();
	virtual ~ibValueMetaObjectRegisterData();
public:

	// The metaobject VENDS its main queryable; it does NOT navigate itself.
	// ibRegisterDataQueryable (a friend) owns the navigation, from this register's
	// primitives (FindAnyAttributeObjectByFilter / GetPhysicalTableName / HasRecorder /
	// HasPeriod / GetRegister* / GetGenericDimensionArrayObject). Slices are separate.
	virtual const ibBackendQueryable* GetQueryable() const override { return m_queryable.GetQueryable(); }
	friend class ibRegisterDataQueryable;

	// L4 query-source registration — the register IS a queryable holder; it passes `this`.
	virtual bool OnAfterRunMetaObject(int flags) override;
	virtual bool OnBeforeCloseMetaObject() override;

#pragma region access_generic
	virtual bool AccessRight_Show() const { return AccessRight_Read(); }
#pragma endregion

	IB_DECLARE_RWD_ROLE_TRIPLET("Write")

	ibValueMetaObjectAttributePredefined* GetRegisterActive() const { return m_propertyAttributeLineActive->GetMetaObject(); }
	bool IsRegisterActive(const ibMetaID& id) const { return id == (*m_propertyAttributeLineActive)->GetMetaID(); }
	ibValueMetaObjectAttributePredefined* GetRegisterPeriod() const { return m_propertyAttributePeriod->GetMetaObject(); }
	bool IsRegisterPeriod(const ibMetaID& id) const { return id == (*m_propertyAttributePeriod)->GetMetaID(); }
	ibValueMetaObjectAttributePredefined* GetRegisterRecorder() const { return m_propertyAttributeRecorder->GetMetaObject(); }
	bool IsRegisterRecorder(const ibMetaID& id) const { return id == (*m_propertyAttributeRecorder)->GetMetaID(); }
	ibValueMetaObjectAttributePredefined* GetRegisterLineNumber() const { return m_propertyAttributeLineNumber->GetMetaObject(); }
	bool IsRegisterLineNumber(const ibMetaID& id) const { return id == (*m_propertyAttributeLineNumber)->GetMetaID(); }

	// (The register's uniqueness key — recorder+line+period / period+dimensions — is vended by
	// its queryable, ibRegisterDataQueryable::GetPrimaryKeyColumns, not a per-attribute flag.)

	///////////////////////////////////////////////////////////////////

#pragma region __generic_h__

	using ibValueMetaObjectCompositeData::GetGenericAttributeArrayObject;
	//attribute
	virtual std::vector<ibValueMetaObjectAttributeBase*> GetGenericAttributeArrayObject(
		std::vector<ibValueMetaObjectAttributeBase*>& array) const {
		FillArrayObjectByPredefinedAttribute(array);
		FillArrayObjectByFilter<ibValueMetaObjectAttributeBase>(array, { g_metaDimensionCLSID, g_metaResourceCLSID, g_metaAttributeCLSID });
		return array;
	}

	//Dimension
	virtual std::vector<ibValueMetaObjectAttributeBase*> GetGenericDimensionArrayObject(
		std::vector<ibValueMetaObjectAttributeBase*> array = std::vector<ibValueMetaObjectAttributeBase*>()) const {
		FillArrayObjectByDimension(array);
		return array;
	}

#pragma endregion
#pragma region __array_h__

	//any attribute
	std::vector<ibValueMetaObjectAttributeBase*> GetAnyAttributeArrayObject(
		std::vector<ibValueMetaObjectAttributeBase*> array = std::vector<ibValueMetaObjectAttributeBase*>()) const {
		FillArrayObjectByPredefinedAttribute(array);
		FillArrayObjectByFilter<ibValueMetaObjectAttributeBase>(array, { g_metaDimensionCLSID, g_metaResourceCLSID, g_metaAttributeCLSID });
		return array;
	}

	//dimension
	std::vector<ibValueMetaObjectDimension*> GetDimensionArrayObject(
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

	// Build a composite-key Pair seeded with this register's dimension
	// list.  ibUniqueKeyPair itself is metadata-agnostic (just carries
	// the metaID → ibValue map); these helpers are the canonical "make
	// me a Pair from this register" entry points.
	ibUniqueKeyPair CreateUniqueKeyPair() const {
		ibRowMetaValues values;
		for (const auto* attr : GetGenericDimensionArrayObject())
			values.insert_or_assign(attr->GetMetaID(), attr->CreateValue());
		return ibUniqueKeyPair(values);
	}

	// Same, but the caller already has dimension values to copy in
	// (filtered to dimensions actually owned by this register).
	ibUniqueKeyPair CreateUniqueKeyPair(const ibRowMetaValues& keyValues) const {
		ibRowMetaValues values;
		for (const auto* attr : GetGenericDimensionArrayObject()) {
			const auto it = keyValues.find(attr->GetMetaID());
			if (it != keyValues.end())
				values.insert_or_assign(attr->GetMetaID(), it->second);
		}
		return ibUniqueKeyPair(values);
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

	ibValueRecordKeyObject* CreateRecordKeyObjectValue() const;

	ibValueRecordSetObject* CreateRecordSetObjectValue(bool needInitialize = true) const;
	ibValueRecordSetObject* CreateRecordSetObjectValue(const ibUniqueKeyPair& uniqueKey, bool needInitialize = true) const;
	ibValueRecordSetObject* CreateRecordSetObjectValue(ibValueRecordSetObject* source, bool needInitialize = true) const;

	ibValueRecordSetObject* CopyRecordSetObjectValue(const ibUniqueKeyPair& uniqueKey);

	ibValueRecordManagerObject* CreateRecordManagerObjectValue() const;
	ibValueRecordManagerObject* CreateRecordManagerObjectValue(const ibUniqueKeyPair& uniqueKey) const;
	ibValueRecordManagerObject* CreateRecordManagerObjectValue(ibValueRecordManagerObject* source) const;

	ibValueRecordManagerObject* CopyRecordManagerObjectValue(const ibUniqueKeyPair& uniqueKey) const;

	//get module object in compose object 
	virtual const ibValueMetaObjectModule* GetObjectModule() const { return nullptr; }
	virtual const ibValueMetaObjectCommonModule* GetManagerModule() const { return nullptr; }

	virtual ibClassID ResolveChild(const ibClassID& clsid) const {
		if (clsid == g_metaDimensionCLSID ||
			clsid == g_metaResourceCLSID ||
			clsid == g_metaAttributeCLSID)
			return clsid;
		return ibValueMetaObjectGenericData::ResolveChild(clsid);
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
	virtual ibBackendValueForm* GetListForm(const wxString& strFormName = wxEmptyString, ibBackendControlFrame* ownerControl = nullptr, const ibUniqueKey& formGuid = wxNullGuid) const = 0;
#pragma endregion 

	//special functions for DB
	virtual wxString GetPhysicalTableName() const;

	// (no table-data dump / restore here — the orchestrator moves rows off the config's ContributeTables
	//  snapshot through the L3-3 mover; this object only DECLARES its table via ContributeTables.)

protected:

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
	virtual bool FillArrayObjectByPredefinedAttribute(std::vector<ibValueMetaObjectAttributeBase*>& array) const {
		return false;
	}

	//get dimension keys 
	virtual bool FillArrayObjectByDimension(
		std::vector<ibValueMetaObjectAttributeBase*>& array) const {
		FillArrayObjectByFilter<ibValueMetaObjectAttributeBase>(array, { g_metaDimensionCLSID });
		return true;
	}

	///////////////////////////////////////////////////////////////////

	virtual ibValueRecordSetObject* CreateRecordSetObjectRegValue(const ibUniqueKeyPair& uniqueKey = wxNullUniquePairKey) const = 0;
	virtual ibValueRecordManagerObject* CreateRecordManagerObjectRegValue(const ibUniqueKeyPair& uniqueKey = wxNullUniquePairKey) const { return nullptr; }

	// Declare the register table (rowData blob + dimension/resource columns + the lookup index).
	virtual void ContributeTables(ibSchemaSnapshot& out) const override;

	//load & save metaData from DB
	virtual bool LoadData(ibReaderMemory& reader);
	virtual bool SaveData(ibWriterMemory& writer);
	virtual bool DeleteData() { return true; }

protected:

	//create default attributes
	ibPropertyContainer<>* m_propertyAttributeLineActive = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateBoolean(wxT("Active"), _("Active"), wxEmptyString, false, true));
	ibPropertyContainer<>* m_propertyAttributePeriod = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateDate(wxT("Period"), _("Period"), wxEmptyString, ibDateFractions::ibDateFractions_DateTime, true));
	ibPropertyContainer<>* m_propertyAttributeRecorder = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateEmptyType(wxT("Recorder"), _("Recorder"), wxEmptyString));
	ibPropertyContainer<>* m_propertyAttributeLineNumber = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateNumber(wxT("LineNumber"), _("Line number"), wxEmptyString, 15, 0));

	// the BASE L4 source descriptor — CONTAINS the register's main (records) queryable and is
	// registered with the factory on run / close; GetQueryable() forwards to it. The register
	// ADDITIONALLY owns CUSTOM virtual-table descriptors (balances / turnovers / balances-and-
	// turnovers / slices) registered alongside it on load — pending the companion queryables
	// (docs §22.4, the totals-table arc).
	ibMetaSourceDescriptor<ibRegisterDataQueryable, ibValueMetaObjectRegisterData> m_queryable{ this };

	// Read/Write/Delete role triplet emitted by IB_DECLARE_RWD_ROLE_TRIPLET
	// above (in the public access region).
};

//********************************************************************************************
//*                                      Base data                                           *
//********************************************************************************************

#include "backend/srcExplorer.h"

class BACKEND_API ibSourceDataObject : public ibSourceObject {
public:

	virtual ~ibSourceDataObject() {}

	// Long-held sys_lock acquire/release for form-open hold pattern.
	// Default = no-op (sources without a meaningful lock identity
	// — DataProcessor, Report — just silently succeed). Concrete
	// data-bound sources override:
	//   ibValueRecordDataObjectRef → keys by ref guid
	//   (future) ibValueRecordSetObject → keys by m_keyValues
	//   (future) ibValueConstantDataObject → keys by const name
	//
	// Storage lives on the base (m_formLockHandle below) — overrides
	// reuse the same field for RAII release on source dtor.
	//
	// See docs/record-locks.md "Planned upgrade path" / Phase B.3.
	virtual bool TryAcquireFormLock(ibLockMode /*mode*/ = ibLockMode::Exclusive) { return true; }
	virtual void ReleaseFormLock() { m_formLockHandle.Release(); }

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
	virtual const ibValueMetaObjectGenericData* GetSourceMetaObject() const = 0;

	//support source data 
	virtual ibSourceExplorer GetSourceExplorer() const = 0;
	virtual bool GetModel(ibValueModel*& tableValue, const ibMetaID& id) = 0;

	//support source set/get data
	virtual bool SetValueByMetaID(const ibMetaID& id, const ibValue& varMetaVal) { return false; }
	virtual bool GetValueByMetaID(const ibMetaID& id, ibValue& pvarMetaVal) const { return false; }

	// Path access: the same source fed an ordered metaId path instead of a single id.
	// The first id is read off the source itself (GetValueByMetaID); each further id
	// steps into the previous reference value. A one-id path is just GetValueByMetaID.
	// The path is a plain lightweight vector — no property-side description type leaks
	// into the source. The walker lives on the source so every kind (RAM / list /
	// object / record set / manager) inherits one traversal — read-only navigation.
	bool GetValueByPath(const std::vector<ibMetaID>& path, ibValue& pvarMetaVal) const;
	ibValue GetValueByPath(const std::vector<ibMetaID>& path) const {
		ibValue retValue;
		if (GetValueByPath(path, retValue))
			return retValue;
		return ibValue();
	}

	//counter
	virtual void SourceIncrRef() = 0;
	virtual void SourceDecrRef() = 0;

protected:
	// Storage for TryAcquireFormLock / ReleaseFormLock. RAII-released
	// on source destruction (form holds source via refcount; source
	// dies when form closes → handle dtor DELETEs sys_lock row).
	// Empty by default; populated by concrete-source overrides of
	// TryAcquireFormLock.
	ibLockHandle m_formLockHandle;
};

//********************************************************************************************
//*                                      Object                                              *
//********************************************************************************************

// The legacy `ibTransactionGuard` template lived here; replaced by
// ibConnectionScope's merged Safe{Begin,Commit,RollBack}Transaction
// methods (databaseLayer/connectionScope.h). Mutation entry points
// (Write/Delete on every object type, plus register RecordSet /
// Manager writes) were migrated wholesale; the struct had no remaining
// call sites and was removed to keep the transaction API surface on
// one path.

//manager with meta object
#pragma region managers

class BACKEND_API ibValueManagerObject : public ibValueDynamicMembers {
	public:

	ibValueManagerObject() : ibValueDynamicMembers(ibValueTypes::TYPE_VALUE, true) {}
	virtual ~ibValueManagerObject() {}

	virtual const ibValueMetaObject* GetMetaObject() const = 0;
};

class BACKEND_API ibValueManagerDataObject : public ibValueManagerObject {
	public:

	// Helper + NVI DoGetPMethods come from ibValueDynamicMembers. The surface is
	// composed from member fillers bound along the ctor chain: this base contributes
	// the manager module's methods (FillMembers / CopyMethod); subclasses add their own.
	ibValueManagerDataObject() : ibValueManagerObject() { m_members.Bind(this, &ibValueManagerDataObject::FillMembers); }
	virtual ~ibValueManagerDataObject() {}

	virtual const ibValueMetaObjectCommonModule* GetManagerModule() const = 0;
	virtual const ibValueMetaObjectGenericData*  GetMetaObject()    const = 0;

	void FillMembers(ibMemberTable& helper) const;       // manager-module methods (was PrepareNames)

	virtual bool CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray);//method call
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);//method call

	//Get ref class
	virtual ibClassID GetClassType() const;

	virtual wxString GetClassName() const;
	virtual wxString GetString() const;
};

class BACKEND_API ibValueManagerDataObjectPredefined : public ibValueManagerDataObject {
	public:

	ibValueManagerDataObjectPredefined() { m_members.Bind(this, &ibValueManagerDataObjectPredefined::FillPredefined); }

	virtual const ibValueMetaObjectRecordDataHierarchyMutableRef* GetMetaObject() const = 0;

	void FillPredefined(ibMemberTable& helper) const;    // predefined-value props (composes onto FillMembers)

	virtual bool SetPropVal(const long lPropNum, ibValue& varPropVal);        //setting attribute
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);                   //attribute value
};

#pragma endregion 

//object with metaobject 
#pragma region objects 
class BACKEND_API ibValueRecordDataObject : public ibValueDynamicMembers, public ibActionDataObject,
	public ibSourceDataObject, public ibValueDataObject, public ibRuntimeModuleDataObject {
	public:
protected:
	enum helperAlias {
		eSystem,
		eProperty,
		eTable,
		eProcUnit = g_aliasExport   // module exports go through the descriptor autobind
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

	// Helper lives in ibValueDynamicMembers (by-value m_members); the NVI
	// DoGetPMethods/GetPMethods + lazy Build() come from the base. The name
	// surface is supplied by ReflectMembers, bound per-instance in the ctor.
public:
	virtual ~ibValueRecordDataObject();

	//support actionData
	virtual ibActionCollection GetActionCollection(const ibFormID& formType) override { return ibActionCollection(); }
	virtual void ExecuteAction(const ibActionID& lNumAction, ibBackendValueForm* srcForm) override {}

	// Feed the record's data-object module into ibRuntimeModuleDataObject's
	// lazy compile-module creation. Every record-data subclass has
	// its own GetMetaObject() (virtual in a parallel ibValueDataObject
	// hierarchy) returning concrete metadata; GetObjectModule() then
	// extracts the compile-target.
	virtual const class ibValueMetaObjectModuleBase* GetMetaForCompile() const override {
		if (auto* m = GetMetaObject())
			return m->GetObjectModule();
		return nullptr;
	}

	virtual ibValueRecordDataObject* CopyObjectValue() = 0;

	// Composed name surface (replaces the old monolithic PrepareNames). The surface
	// is split so the common part lives once on the base and each leaf adds only its
	// own methods (methods and props are separate helper vectors, so bind order
	// across them never shifts a method's dispatch index):
	//  - FillDataMembers — the metaobject's attributes (eProperty) + tabular sections
	//    (eTable) + data-object module exports (eProcUnit). Bound by the base ctor,
	//    shared by every leaf. Attribute writability follows IsDataReference.
	//  - FillBaseMethods — the three fixed methods (GetFormObject/GetTemplate/
	//    GetMetadata) for leaves with no API of their own (DataProcessor, Report);
	//    they bind this. Leaves with their own method set (Catalog, Document, …)
	//    bind their own FillMethods instead.
	void FillDataMembers(ibMemberTable& helper) const;
	void FillBaseMethods(ibMemberTable& helper) const;

	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal) override;
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal) override;

	virtual bool CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray) override;
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray) override;

	//check is empty
	virtual bool IsEmpty() const override { return ibValue::IsEmpty(); }

	//get metaData from object 
	virtual const ibValueMetaObjectGenericData* GetSourceMetaObject() const final { return GetMetaObject(); }

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
	virtual const ibValueMetaObjectRecordData* GetMetaObject() const override = 0;

	//get unique identifier
	virtual ibUniqueKey GetGuid() const override = 0;

	//get frame
	virtual ibBackendValueForm* GetForm() const;

#pragma region _form_builder_h_
	// Universal form-open trampolines. Hoisted from per-leaf code that
	// followed the same shape across HierarchyRef, RecorderRef (Document)
	// and Ext (DataProcessor / Report). Variation per leaf collapses to
	// two hooks: GetCurrentObjectFormID (form-id enum value) and
	// OnFormCreated (post-creation tweak — e.g. CloseOnOwnerClose(false)
	// on ref-flavour leaves, no-op on Ext). valueForm->Modify uses the
	// virtual IsModified() — Ref leaves return m_objModified, Ext keeps
	// the default `false` from ibSourceDataObject.
	virtual void ShowFormValue(const wxString& strFormName = wxEmptyString, ibBackendControlFrame* ownerControl = nullptr);
	virtual ibBackendValueForm* GetFormValue(const wxString& strFormName = wxEmptyString, ibBackendControlFrame* ownerControl = nullptr);

protected:
	// Leaf-specific form-id enum value for the current object state.
	// Hierarchy returns eFormObject / eFormFolder by m_objMode;
	// Document / DataProcessor / Report return a single fixed id.
	virtual ibFormID GetCurrentObjectFormID() const = 0;
public:

#pragma endregion

	//default showing
	virtual void ShowValue() override { ShowFormValue(); }

	//save modify
	virtual bool SaveModify() override { return true; }

	//Get ref class
	virtual ibClassID GetClassType() const override;

	virtual wxString GetClassName() const override;
	virtual wxString GetString() const override;

	//Working with iterators — walks named properties via GetPropVal
	virtual std::shared_ptr<ibValueIteratorState> CreateIterator() override {
		class State : public ibValueIteratorState {
		public:
			State(ibValue* host, unsigned int n) : m_host(host), m_n(n) {}
			bool MoveNext(ibValue& current) override {
				if (m_started) ++m_pos; else m_started = true;
				if (m_pos >= m_n) return false;
				current = ibValue();
				m_host->GetPropVal(static_cast<long>(m_pos), current);
				return true;
			}
			void Reset() override { m_pos = 0; m_started = false; }
		private:
			ibValue* m_host;
			unsigned int m_n;
			unsigned int m_pos = 0;
			bool m_started = false;
		};
		// GetPMethods() lazily builds the surface (the iterator may be the first
		// access), then reports the property count.
		const unsigned int n = GetPMethods()->GetNProps();
		return std::make_shared<State>(this, n);
	}

protected:
	virtual void PrepareEmptyObject();
protected:
	friend class ibMetaData;
	friend class ibValueMetaObjectRecordData;
};

//Object with file
// RAII mix-in for external DP/Report value objects: takes the transient external
// metadata container in its ctor and drops it (CloseDatabase + delete) in its dtor.
// Mixed into ibValueRecordDataObjectExternal* alongside the regular DP/Report value
// class; embedded / config value objects don't inherit it. Generic ibMetaData* —
// CloseDatabase + delete go through the polymorphic base, so no concrete container
// type is needed and the inline dtor compiles in every TU.
class BACKEND_API ibExternalOwnerHelper {
public:
	ibExternalOwnerHelper(ibMetaData* externalMetadata = nullptr) : m_externalMetadata(externalMetadata) {}
	// A copy never owns the source's container — only one object drops it.
	ibExternalOwnerHelper(const ibExternalOwnerHelper&) : m_externalMetadata(nullptr) {}
	~ibExternalOwnerHelper();   // out-of-line in commonObject.cpp — ibMetaData is only forward-declared here
protected:
	ibMetaData* m_externalMetadata;
};

class BACKEND_API ibValueRecordDataObjectExt : public ibValueRecordDataObject {
	public:
protected:
	//override copy constructor
	ibValueRecordDataObjectExt(const ibValueMetaObjectRecordDataExt* metaObject);
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
	virtual const ibValueMetaObjectRecordDataExt* GetMetaObject() const { return m_metaObject; }

protected:
	const ibValueMetaObjectRecordDataExt* m_metaObject;
};

//Object with reference type 
class BACKEND_API ibValueRecordDataObjectRef : public ibValueRecordDataObject {
	public:
protected:

	// Code generator. Allocates the next per-(meta_guid, prefix) sequence
	// number from sys_sequence and formats it per the attribute's type.
	// Instance method (not static) so the call site is guaranteed to run
	// inside an active session — sequence allocation routes through the
	// session's connection (ibSession::Current()->EnsureConnection()), so the
	// increment and the outer document save share one TX. Cross-machine
	// atomic via UPDATE...RETURNING; FB+PG only.
	// See memory/reference_generate_next_identifier.
	ibValue GenerateNextIdentifier(
		ibValueMetaObjectAttributeBase* attribute, const wxString& strPrefix);

	ibValueRecordDataObjectRef(const ibValueMetaObjectRecordDataMutableRef* metaObject, const ibGuid& objGuid);
	ibValueRecordDataObjectRef(const ibValueRecordDataObjectRef& src);
public:

	virtual ~ibValueRecordDataObjectRef();

	virtual bool InitializeObject(const ibGuid& copyGuid = wxNullGuid);
	virtual bool InitializeObject(ibValueRecordDataObjectRef* source, bool generate = false);

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
	virtual const ibValueMetaObjectRecordDataMutableRef* GetMetaObject() const { return m_metaObject; }

	//check is changes data in db
	virtual bool ModifiesData() { return true; }

	//default methods
	virtual bool Generate();

	//filling object
	virtual bool Filling(ibValue cValue = ibValue()) const;

	// Script-exposed FillObject + CopyObject — all 4 ref leaves
	// (Catalog / Document / ChartOfAccounts /
	// ChartOfCharacteristicTypes) had byte-identical inline copies of
	// these. Hoisted here so the convenience layer lives in one place.
	// Filling / CopyObjectValue / ShowFormValue are virtual — leaves
	// don't need to override unless they want different defaults.
	virtual bool FillObject(ibValue& vFillObject) const {
		return Filling(vFillObject);
	}
	virtual ibValueRecordDataObjectRef* CopyObject(bool showValue = false) {
		ibValueRecordDataObjectRef* objectRef = CopyObjectValue();
		if (objectRef != nullptr && showValue)
			objectRef->ShowFormValue();
		return objectRef;
	}

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

	// Optimistic-concurrency Write protection.
	//
	// Two-layer defence (see docs/record-locks.md):
	//   Layer 1 — issues `SELECT DataVersion FROM <tbl> WHERE uuid = ?
	//             <FOR UPDATE>` inside the current TX (m_lockForUpdate; the dialect renders the
	//             clause from m_rowLockSuffix). The driver-side
	//             row-lock pins the row until the surrounding scope
	//             Commit/Rollback fires; concurrent writers block (or
	//             fail-fast under NOWAIT TX options).
	//   Layer 2 — compares the freshly-read DataVersion against the
	//             value captured at ReadData time (m_loadedDataVersion).
	//             A mismatch means somebody else committed between our
	//             Read and now → throws ibBackendLockException::
	//             VersionChanged.
	//   Bump   — when `bump = true` (the default; pass false on Delete
	//             where the row is going away), allocates a fresh
	//             ibDataVersion::NewStamp() and writes it into
	//             m_listObjectValue[DataVersion.MetaID] so the next
	//             SaveData() picks it up in its UPSERT.
	//
	// New objects (m_newObject == true) skip both layers — there's
	// nothing to compare against and nothing to lock.
	//
	// Must be called inside an active TX (the SafeBeginTransaction
	// scope on the WriteObject / DeleteObject hot path).
	bool LockAndCheckDataVersion(bool bump = true);

	// Phase A Write/Delete scaffold helpers — extract the pre/post
	// boilerplate (designer/eval skip, scope/access checks, TX begin,
	// lock + version check; commit + notify + clear-modified) so
	// subclass methods reduce to just the per-type middle (BeforeWrite
	// hook, codegen, SaveData, OnWrite hook).
	//
	// Usage pattern (caller side):
	//
	//   bool ibValueRecordDataObjectCatalog::WriteObject() {
	//     ibConnectionScope scope = ibSession::Current()->OpenConnectionScope();
	//     if (!BeginWriteScope(scope)) return true;          // designer / eval
	//
	//     ibBackendValueForm* const valueForm = GetForm();
	//     const bool newObject = IsNewObject();
	//
	//     /* === middle: BeforeWrite + codegen + SaveData + OnWrite === */
	//
	//     CommitWriteScope(scope, valueForm, newObject);
	//     return true;
	//   }
	//
	// Begin*Scope returns true to proceed, false to skip silently
	// (designer / eval mode). Throws ibBackendAccessException on
	// access denial; ibBackendLockException on lock conflict /
	// version mismatch; ibBackendCoreException on DB-not-open.
	//
	// Commit*Scope assumes the middle completed without throwing. If
	// the middle threw, scope's RAII rollback fires automatically and
	// Commit*Scope never runs.
	//
	// Document keeps its inline scaffold (writeMode/postingMode args +
	// register-cascade state machine) — Phase B may promote it.
	// Constants stay inline (single use, single file).
	bool BeginWriteScope (ibConnectionScope& scope);
	bool BeginDeleteScope(ibConnectionScope& scope);
	void CommitWriteScope (ibConnectionScope& scope,
	                        ibBackendValueForm* valueForm, bool newObject);
	void CommitDeleteScope(ibConnectionScope& scope,
	                        ibBackendValueForm* valueForm);

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
	const ibValueMetaObjectRecordDataMutableRef* m_metaObject;
	ibReference* m_reference_impl;

	// Captured at successful ReadData; compared against the row's
	// current DataVersion at Write/Delete time. Empty for new objects.
	// See LockAndCheckDataVersion + docs/record-locks.md.
	wxString m_loadedDataVersion;

public:
	// Override ibSourceDataObject::TryAcquireFormLock — keys the lock
	// by this object's ref guid in the "<Kind>.<Name>" namespace.
	// No-op for new (unsaved) objects — nothing to identify yet.
	// Throws ibBackendLockException::LockConflict on conflict; caller
	// (form-open path) decides whether to propagate or degrade.
	bool TryAcquireFormLock(ibLockMode mode = ibLockMode::Exclusive) override;
};

//Object with reference type and group/object type
class BACKEND_API ibValueRecordDataObjectHierarchyRef : public ibValueRecordDataObjectRef {
	public:
protected:
	ibValueRecordDataObjectHierarchyRef(const ibValueMetaObjectRecordDataHierarchyMutableRef* metaObject, const ibGuid& objGuid, ibObjectMode objMode = ibObjectMode::OBJECT_ITEM);
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
	virtual const ibValueMetaObjectRecordDataHierarchyMutableRef* GetMetaObject() const {
		return static_cast<const ibValueMetaObjectRecordDataHierarchyMutableRef*>(m_metaObject);
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

	// Phase B template-method scaffolds. The 3 hierarchy-mutable-ref
	// leaves (Catalog / ChartOfAccounts / ChartOfCharacteristicTypes)
	// share byte-identical Write/Delete pipelines — BeforeWrite cancel
	// + SetNewCode codegen + SaveData + OnWrite cancel for Write;
	// predefined-guard + BeforeDelete + DeleteData + OnDelete for
	// Delete. Both are implemented here once and inherited verbatim by
	// the leaves (zero per-subclass override). All variation points
	// resolve through virtual dispatch — GenerateUniqueIdentifier /
	// ResetUniqueIdentifier / SaveData / DeleteData / metaobject's
	// FindPredefinedValue / IsNewObject.
	//
	// Document keeps its inline scaffold (writeMode/postingMode args +
	// posting state machine + register cascade). Promoting it to a
	// hook-based extension of this scaffold is Phase C.
	virtual bool WriteObject()  override;
	virtual bool DeleteObject() override;

	// SaveModify route for the form's auto-save path. 3 hierarchy
	// leaves use it as a thin trampoline — Catalog / ChartOfAccounts /
	// ChartOfCharacteristicTypes each used to declare an identical
	// inline copy; consolidated here. Document overrides because its
	// Write takes (writeMode, postingMode).
	virtual bool SaveModify() override { return WriteObject(); }

	// ShowFormValue / GetFormValue + GetCurrentObjectFormID hook live
	// on ibValueRecordDataObject (universal). Each leaf overrides
	// GetCurrentObjectFormID with its m_objMode-aware pair (Hierarchy)
	// or single id (Document / Ext).

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

// Intermediate base for ref-objects that carry register movements.
// Mirrors ibValueRecordDataObjectHierarchyRef on the other axis —
// Hierarchy adds folder/item mode + predefined-value policy; this
// adds posting state + register cascade. Today there's exactly one
// descendant (ibValueRecordDataObjectDocument), but the class is a
// structural placeholder + actual scaffold owner so that:
//   • a second postable type (business process, task object, custom
//     1С-style document) can slot in without touching the leaf
//     hierarchy;
//   • cross-cutting concerns scoped to "ref with movements" land in
//     one place instead of being scattered across N leaf cpps.
//
// The posting-state-machine + register cascade live here. Document-
// specific bits (DeletionMark posting guard, DocumentPosted attribute
// mutation, default DocDate fill) are hooks the concrete leaf overrides.
class BACKEND_API ibValueRecordDataObjectRecorderRef : public ibValueRecordDataObjectRef {
	public:
	// Per-recorder register holder. Iterates the leaf metaobject's
	// RecordDescription, creates one ibValueRecordSetObject per
	// declared register seeded with this recorder's reference, and
	// fans Write/Delete across all of them.
	class BACKEND_API ibRecorderRegister : public ibValueDynamicMembers {
	public:
		void CreateRecordSet();
		bool WriteRecordSet();
		bool DeleteRecordSet();
		void ClearRecordSet();
		void RefreshRecordSet();

		ibRecorderRegister(ibValueRecordDataObjectRecorderRef* recorder = nullptr);
		virtual ~ibRecorderRegister();

		void FillMembers(ibMemberTable& helper) const;   // bound in ctor (was PrepareNames)
		virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);

		virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);
		virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);

		virtual bool IsEmpty() const { return false; }

	private:
		ibValueRecordDataObjectRecorderRef* m_recorder;
		std::map<ibMetaID, ibValuePtr<ibValueRecordSetObject>> m_records;
	};

protected:
	ibValueRecordDataObjectRecorderRef(const ibValueMetaObjectRecordDataMutableRef* metaObject, const ibGuid& objGuid);
	ibValueRecordDataObjectRecorderRef(const ibValueRecordDataObjectRecorderRef& src);

	// Late-bind of the register-cascade holder. The leaf ctor must
	// call this from its OWN body (not init list) so the most-derived
	// vtable is in place — ibRecorderRegister::CreateRecordSet
	// dispatches the virtual GetRecordDescription() back to the leaf;
	// doing the new in RecorderRef's init list would resolve it to
	// the pure-virtual in RecorderRef and trip __purecall during
	// construction. See documentObject.cpp Document ctors.
	void InitRegisterRecords();

public:
	virtual ~ibValueRecordDataObjectRecorderRef();

	// Recorder init — binds RegisterRecords (exported) before the base compiles,
	// then delegates to the base (ThisObject bind + compile). Shared by all
	// recorder / document-like objects; both creation paths need the bind.
	virtual bool InitializeObject(const ibGuid& copyGuid = wxNullGuid) override;
	virtual bool InitializeObject(ibValueRecordDataObjectRef* source, bool generate = false) override;

	// Public helpers for register access from form scripts / Document
	// subclass override paths.
	void ClearRecordSet()  { wxASSERT(m_registerRecords); m_registerRecords->ClearRecordSet(); }
	void UpdateRecordSet() { wxASSERT(m_registerRecords); m_registerRecords->ClearRecordSet(); m_registerRecords->CreateRecordSet(); }

	// Write/Delete scaffold. The 2-arg WriteObject is the canonical
	// entry; the no-arg trampoline picks (Write/UndoPosting | Posting
	// based on IsPosted, Regular postingMode) — matches how the
	// auto-save / SaveModify path used to call it from Document.
	virtual bool WriteObject() override {
		return WriteObject(
			IsPosted() ? ibDocumentWriteMode::ibDocumentWriteMode_Posting
			           : ibDocumentWriteMode::ibDocumentWriteMode_Write,
			ibDocumentPostingMode::ibDocumentPostingMode_Regular);
	}
	virtual bool WriteObject(ibDocumentWriteMode writeMode, ibDocumentPostingMode postingMode);
	virtual bool DeleteObject() override;
	virtual bool SaveModify() override { return WriteObject(); }

	// Marking a recorder for deletion is the same algorithm as for
	// catalogs / charts (set DeletionMark + SaveModify) plus an
	// up-front un-post — setting DeletionMark on a posted recorder
	// implies undoing its movements before the row is marked.
	// UndoPosting is a no-op when IsPosted() returns false, so a
	// future recorder-flavour without movements still works.
	virtual void SetDeletionMark(bool deletionMark = true) override;

	// Hooks for leaf-specific Document state. Defaults are no-op so a
	// future plain "recorder" type without these Document concepts
	// (e.g. a bare business-process recorder) doesn't need to override.
	virtual bool IsPosted() const                                          { return false; }
	virtual bool CheckDeletionMarkOnPosting(ibDocumentWriteMode /*wm*/) const { return true; }   // true = ok to proceed
	virtual void ApplyPostedAttributeOnWrite(ibDocumentWriteMode /*wm*/)   {}
	virtual void FillDefaultDateForNew()                                   {}

	// Description of registers the recorder writes movements into.
	// Returns null for recorder types with no fixed movement description
	// (rare; the cascade just no-ops in that case). Document forwards
	// to its metaobject's GetRecordDescription so ibRecorderRegister::
	// CreateRecordSet stays Document-agnostic.
	virtual const ibMetaDescription* GetRecordDescription() const = 0;

protected:
	// Register holder owned by the recorder. Created in ctor, lives
	// for the lifetime of the recorder value.
	ibValuePtr<ibRecorderRegister> m_registerRecords;
};

#pragma endregion

//object with register type
#pragma region registers
class BACKEND_API ibValueRecordKeyObject : public ibValueDynamicMembers {
	public:
	ibValueRecordKeyObject(const ibValueMetaObjectRegisterData* metaObject);
	virtual ~ibValueRecordKeyObject();

	// Helper + NVI DoGetPMethods come from ibValueDynamicMembers; FillMembers
	// (bound in the ctor) supplies the surface.
	void FillMembers(ibMemberTable& helper) const;
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray) override;

	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal) override;
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal) override;

	//check is empty
	virtual bool IsEmpty() const override;

	//Get unique key
	ibUniqueKeyPair GetUniqueKey() { return m_metaObject->CreateUniqueKeyPair(m_keyValues); }

	//get metaData from object
	virtual const ibValueMetaObjectRegisterData* GetMetaObject() const { return m_metaObject; };

	//Get ref class
	virtual ibClassID GetClassType() const override;

	virtual wxString GetClassName() const override;
	virtual wxString GetString() const override;

	//Working with iterators — walks named properties via GetPropVal
	virtual std::shared_ptr<ibValueIteratorState> CreateIterator() override {
		class State : public ibValueIteratorState {
		public:
			State(ibValue* host, unsigned int n) : m_host(host), m_n(n) {}
			bool MoveNext(ibValue& current) override {
				if (m_started) ++m_pos; else m_started = true;
				if (m_pos >= m_n) return false;
				current = ibValue();
				m_host->GetPropVal(static_cast<long>(m_pos), current);
				return true;
			}
			void Reset() override { m_pos = 0; m_started = false; }
		private:
			ibValue* m_host;
			unsigned int m_n;
			unsigned int m_pos = 0;
			bool m_started = false;
		};
		const unsigned int n = GetPMethods()->GetNProps();
		return std::make_shared<State>(this, n);
	}

protected:
	const ibValueMetaObjectRegisterData* m_metaObject;
	ibRowMetaValues m_keyValues;
};

class BACKEND_API ibValueRecordSetObject : public ibValueModelRamTableBase, public ibRuntimeModuleDataObject {
	public:

	virtual ibValueModelColumnCollection* GetColumnCollection() const override { return m_recordColumnCollection; }
	virtual ibValueModelReturnLine* GetRowAt(const ibDataViewItem& line) override {
		if (!line.IsOk())
			return nullptr;
		return new ibValueRecordSetObjectRegisterReturnLine(this, line);
	}

	class ibValueRecordSetObjectRegisterColumnCollection : public ibValueModelTableBase::ibValueModelColumnCollection {
	public:

		class ibValueRecordSetRegisterColumnInfo : public ibValueModelTableBase::ibValueModelColumnCollection::ibValueModelColumnInfo {
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
			const ibValueMetaObjectGenericData* metaTable = m_ownerTable->GetMetaObject();
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
	};

	class ibValueRecordSetObjectRegisterReturnLine : public ibValueModelReturnLine {
	public:

		ibValueRecordSetObjectRegisterReturnLine(ibValueRecordSetObject* ownerTable = nullptr,
			const ibDataViewItem& line = ibDataViewItem());
		virtual ~ibValueRecordSetObjectRegisterReturnLine();

		virtual ibValueModelTableBase* GetOwnerModel() const { return m_ownerTable; }

		void FillMembers(ibMemberTable& helper) const;   // bound in ctor (was PrepareNames)

		virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal); //setting attribute
		virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal); //attribute value

		//Get ref class
		virtual ibClassID GetClassType() const;

		virtual wxString GetClassName() const;
		virtual wxString GetString() const;

		friend class ibValueRecordSetObject;
	private:
		ibValueRecordSetObject* m_ownerTable;
	};

	class ibValueRecordSetObjectRegisterKeyValue : public ibValueDynamicMembers {
	public:
		class ibValueRecordSetObjectRegisterKeyDescriptionValue : public ibValueDynamicMembers {
	public:

			ibValueRecordSetObjectRegisterKeyDescriptionValue(ibValueRecordSetObject* recordSet = nullptr, const ibMetaID& id = wxNOT_FOUND);
			virtual ~ibValueRecordSetObjectRegisterKeyDescriptionValue();

			virtual bool IsEmpty() const { return false; }

			//****************************************************************************
			//*                              Support methods                             *
			//****************************************************************************

			void FillMembers(ibMemberTable& helper) const;   // bound in ctor (was PrepareNames)
			virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);       // method call

			virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);//setting attribute
			virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);//attribute value

		protected:
			ibMetaID m_metaId;
			ibValueRecordSetObject* m_recordSet;
		};
	public:

		ibValueRecordSetObjectRegisterKeyValue(ibValueRecordSetObject* recordSet = nullptr);
		virtual ~ibValueRecordSetObjectRegisterKeyValue();

		virtual bool IsEmpty() const { return false; }

		//****************************************************************************
		//*                              Support methods                             *
		//****************************************************************************

		void FillMembers(ibMemberTable& helper) const;   // bound in ctor (was PrepareNames)
		virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);       // method call

		virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);//setting attribute
		virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);//attribute value

	protected:
		ibValueRecordSetObject* m_recordSet;
	};

protected:
	ibValueRecordSetObject(const ibValueMetaObjectRegisterData* metaObject, const ibUniqueKeyPair& uniqueKey);
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

	// Helper + NVI DoGetPMethods come from ibValueDynamicMembers (via the table
	// chain root). The module-export surface autobinds in the descriptor ctor.

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
	virtual const ibValueMetaObjectRegisterData* GetMetaObject() const { return m_metaObject; }

	// Lazy compile-module creation: the record-set compiles against the
	// register's record-set (object) module. The base GetMetaForCompile reads
	// through m_compileModule — circular (null at the first Bind…), so name the
	// module directly; otherwise EnsureCompileModule never builds the compile
	// module and the designer editor / OnTextChange sees GetCompileModule()==null.
	virtual const class ibValueMetaObjectModuleBase* GetMetaForCompile() const override {
		if (auto* m = GetMetaObject())
			return m->GetObjectModule();
		return nullptr;
	}

#pragma region _tabular_data_
	//get metaData from object
	virtual const ibValueMetaObjectCompositeData* GetSourceMetaObject() const { return GetMetaObject(); }

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

	// Phase B template-method scaffolds. The 3 register-set leaves
	// (Accumulation / Accounting / Information) have byte-identical
	// Write/Delete pipelines mod class-name qualification on
	// SaveData / DeleteData — the base owns the scaffold here, the
	// leaves inherit it verbatim (zero per-subclass override).
	// SaveData(replace, clearTable) / DeleteData() resolve through
	// virtual dispatch into the leaf's type-specific UPSERT SQL.
	virtual bool WriteRecordSet(bool replace = true, bool clearTable = true);
	virtual bool DeleteRecordSet();

	//array
	virtual bool GetAt(const ibValue& varKeyValue, ibValue& pvarValue) override;

	//Get ref class
	virtual ibClassID GetClassType() const override;

	virtual wxString GetClassName() const override;
	virtual wxString GetString() const override;

	// Iterator runtime path lives on ibValueModel (RamFetch cursor over
	// BuildVisibleView). GetEmptyRow yields the typed skeleton that
	// the iterator state surfaces as IntelliSense type hint.
	virtual ibValue GetEmptyRow() override {
		return new ibValueRecordSetObjectRegisterReturnLine(this, ibDataViewItem());
	}

protected:

	virtual bool ExistData();
	virtual bool ExistData(ibNumber& lastNum); //for records
	virtual bool ReadData();
	virtual bool ReadData(const ibUniqueKeyPair& key);
	virtual bool SaveData(bool replace = true, bool clearTable = true);
	virtual bool DeleteData();

	// Layer-1 DB row-lock for register record-set writes. Locks the
	// matching rows on this register's own table by the full key set
	// (m_keyValues) — uniform across all register shapes:
	//   • recorder-keyed (Accumulation / Accounting / IR-Subordinate)
	//     → m_keyValues holds Recorder field → lock all rows for that
	//       recorder
	//   • dimension-keyed (Information Register without recorder)
	//     → m_keyValues holds dimensions + optional period → lock the
	//       matching row(s)
	//
	// Concurrent Document.Write / direct RecordSet.Write that produce
	// the same key set serialize through the driver's FOR UPDATE /
	// WITH LOCK. The cascade case (Document.Write fires register
	// writes for its own ref inside the same TX) is re-entrant — the
	// rows are already locked by this TX, so the second acquire is a
	// no-op at the DB level.
	//
	// No DataVersion check (registers don't carry one; Document.Write
	// does the version-check on the Document row itself).
	//
	// Skipped silently when m_keyValues is empty or m_metaObject has
	// no resolvable key attributes (defensive — should not happen on a
	// well-formed register but doesn't crash on a partially-initialized
	// record set).
	//
	// Must be called inside an active TX (the SafeBeginTransaction
	// scope on the WriteRecordSet / DeleteRecordSet hot path).
	// See docs/record-locks.md "Registers — keyed by recorder Document".
	bool LockByKeys();

	// Phase A scaffold helpers — register-side counterparts of the
	// ibValueRecordDataObjectRef Begin*/Commit* pair. Subclasses
	// (Accumulation / Accounting / Information) reduce to per-type
	// middle (BeforeWrite + SaveData + OnWrite cancel handling).
	//
	// Usage:
	//   bool ibValueRecordSetObjectXxx::WriteRecordSet(bool r, bool c) {
	//     ibConnectionScope scope = ibSession::Current()->OpenConnectionScope();
	//     if (!BeginRecordSetWriteScope(scope)) return true;
	//     /* === middle: BeforeWrite + SaveData(r, c) + OnWrite === */
	//     CommitRecordSetScope(scope);
	//     return true;
	//   }
	//
	// Begin runs designer/eval skip + scope/access check + TX begin +
	// LockByKeys; Commit runs SafeCommitTransaction + clears
	// m_objModified. No Notify on register-side (they're owned by a
	// recorder Document, the Document's Write fires the form notify).
	bool BeginRecordSetWriteScope (ibConnectionScope& scope);
	bool BeginRecordSetDeleteScope(ibConnectionScope& scope);
	void CommitRecordSetScope     (ibConnectionScope& scope);

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

	ibRowMetaValues m_keyValues;

	const ibValueMetaObjectRegisterData* m_metaObject;

	ibValuePtr<ibValueRecordSetObjectRegisterColumnCollection> m_recordColumnCollection;
	ibValuePtr<ibValueRecordSetObjectRegisterKeyValue> m_recordSetKeyValue;

};

class BACKEND_API ibValueRecordManagerObject : public ibValueDynamicMembers,
	public ibSourceDataObject, public ibActionDataObject {
	public:
protected:
	ibValueRecordManagerObject(const ibValueMetaObjectRegisterData* metaObject, const ibUniqueKeyPair& uniqueKey);
	ibValueRecordManagerObject(const ibValueRecordManagerObject& source);
public:

	ibValueRecordSetObject* GetRecordSet() const { return m_recordSet; }

	virtual ~ibValueRecordManagerObject();

	virtual void CreateEmptyKey();

	bool InitializeObject(const ibValueRecordManagerObject* source = nullptr, bool newRecord = false);
	bool InitializeObject(const ibUniqueKeyPair& key);

	virtual bool IsNewObject() const override { return !m_recordSet->m_selected; }

	//get metaData from object
	virtual const ibValueMetaObjectGenericData* GetSourceMetaObject() const final { return GetMetaObject(); }

	//Get ref class
	virtual ibClassID GetSourceClassType() const final { return GetClassType(); }

	//Get presentation
	virtual wxString GetSourceCaption() const override {
		return m_metaObject->GetSynonym() + wxT(": ") + (IsNewObject() ? _("Creating") : m_metaObject->GetSynonym());
	}

	//copy new object
	virtual ibValueRecordManagerObject* CopyRegisterValue();

	// Helper + NVI DoGetPMethods come from ibValueDynamicMembers; leaves bind their
	// own fillers. (PrepareNames is gone.)

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
	virtual const ibValueMetaObjectRegisterData* GetMetaObject() const {
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

	//Working with iterators — walks named properties via GetPropVal
	virtual std::shared_ptr<ibValueIteratorState> CreateIterator() override {
		class State : public ibValueIteratorState {
		public:
			State(ibValue* host, unsigned int n) : m_host(host), m_n(n) {}
			bool MoveNext(ibValue& current) override {
				if (m_started) ++m_pos; else m_started = true;
				if (m_pos >= m_n) return false;
				current = ibValue();
				m_host->GetPropVal(static_cast<long>(m_pos), current);
				return true;
			}
			void Reset() override { m_pos = 0; m_started = false; }
		private:
			ibValue* m_host;
			unsigned int m_n;
			unsigned int m_pos = 0;
			bool m_started = false;
		};
		const unsigned int n = GetPMethods()->GetNProps();
		return std::make_shared<State>(this, n);
	}

protected:

	virtual void PrepareEmptyObject(const ibValueRecordManagerObject* source);

protected:

	virtual bool ExistData();
	virtual bool ReadData(const ibUniqueKeyPair& key);
	virtual bool SaveData(bool replace = true);
	virtual bool DeleteData();

protected:

	ibUniqueKeyPair m_objGuid;

	const ibValueMetaObjectRegisterData* m_metaObject;

	ibValuePtr<ibValueRecordSetObject> m_recordSet;
	ibValuePtr<ibValueModelTableBase::ibValueModelReturnLine> m_recordLine;
};
#pragma endregion

#endif 