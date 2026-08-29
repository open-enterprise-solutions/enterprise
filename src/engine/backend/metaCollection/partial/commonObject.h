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
// commonObjectEnum.h carries ibHierarchyType — the shape a hierarchical metaobject declares
// (parent is a FOLDER, as in a catalog, or parent is an ITEM, as in a chart of accounts).
#include "backend/metaCollection/partial/commonObjectEnum.h"
// ibConnectionScope is used by-reference in the Phase A Begin*/Commit*
// scaffold helper signatures on ibValueRecordDataObjectRef +
// ibValueRecordSetObject. Pulling the real header here avoids a
// global forward-decl in this header. ibBackendValueForm is already
// visible transitively via metaFormObject.h below.
#include "backend/databaseLayer/connectionScope.h"

#include "backend/query/queryableFactory.h"   // ibMetaCommandDescriptor (the L4 source descriptor field + its command surface)

//special object
#include "backend/metaCollection/metaModuleObject.h"
#include "backend/metaCollection/metaFormObject.h"
#include "backend/metaCollection/metaSpreadsheetObject.h"

//interface
#include "backend/metaCollection/metaSectionObject.h"

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
#include "backend/standardCommand.h"
#include "backend/moduleInfo.h"
#include "backend/valueInfo.h"
#include "backend/tabularModel.h"

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
class BACKEND_API ibBackendSourceColumn;   // queryColumn.h — neutral source column (GetSourceColumn)

class BACKEND_API ibValueManagerDataObject;

class BACKEND_API ibValueRecordDataObject;
class BACKEND_API ibValueRecordDataObjectExt;
class BACKEND_API ibValueRecordDataObjectRef;
class BACKEND_API ibValueRecordDataObjectHierarchyRef;

class BACKEND_API ibValueRecordKeyObject;
class BACKEND_API ibValueRecordManagerObject;
class BACKEND_API ibValueRecordSetObject;

// ibSourceExplorer is a nested class of ibSourceDataObject (srcDataObject.h, pulled via reference.h);
// the global using-alias makes the bare name resolve, so no forward-decl here.

//special names

//********************************************************************************************
//*                                  Factory & metaData                                      *
//********************************************************************************************

#include "backend/metaCollection/genericData.h"   // ibFormTypeList + ibValueMetaObjectGenericData (extracted so srcDataObject.h gets the covariant GenericData* return)

// The metaobject-coupled SOURCE-DESCRIPTOR templates — moved here from queryableFactory.h so the L4 factory header
// stays metadata-agnostic (base descriptor + factory only) and these live WITH the metaobjects that instantiate them.
// The base ibQueryableSourceDescriptor + ibValue / ibBackendQueryable / ibSourceExplorer / command signatures come in
// via queryableFactory.h (included above).

// The QUERY descriptor — the query-identity HALF, TEMPLATED on the queryable type TQueryable + the metaobject type
// TMeta. It CONTAINS the metaobject's queryable (built from it) AND carries the L4 source identity (GetNamespace /
// GetName / CreateQueryable). This is ALL a pure QUERY source needs — a constant is registered only so From(constant)
// resolves; it is NEVER shown as a list, so it leaves the command + row surface at the base's neutral defaults.
template <typename TQueryable, typename TMeta>
class ibMetaQueryDescriptor : public ibQueryableSourceDescriptor
{
public:
	explicit ibMetaQueryDescriptor(TMeta* meta) : m_meta(meta), m_queryable(meta) {}

	wxString GetNamespace() const override { return ibValue::GetNameObjectFromID(m_meta->GetClassType()); }
	wxString GetName() const override { return m_meta->GetName(); }
	const ibBackendQueryable* CreateQueryable(ibValue** /*paParams*/, long /*lSizeArray*/) override { return &m_queryable; }

	// The contained queryable — the metaobject's GetQueryable() forwards here (stable for the object's life).
	const ibBackendQueryable* GetQueryable() const { return &m_queryable; }

	// WHAT COLUMNS THIS SOURCE HAS — forwarded to the metaobject, which is the only one that knows.
	//
	// ⚠ THIS BELONGS TO THE QUERY HALF, not to the command one. It used to live on
	// ibMetaCommandDescriptor, so every source that is ONLY queryable — a constant, first among
	// them — answered the base's empty default: it appeared in a catalogue as a table with no
	// fields, could be added to a query and offered nothing to select. Asking what a source holds
	// has nothing to do with whether it can be shown as a list.
	void FillSourceExplorer(ibSourceDataObject::ibSourceExplorer& explorer) const override {
		m_meta->FillSourceExplorer(explorer);
	}

	// Restore ROW-KEY from a row's identity VALUE — read the source's PRIMARY-KEY columns off it (the SAME columns
	// the fetch stamps into a node's m_rowKey, so the restore stub matches). UNIVERSAL via the source hop gate: a
	// record's reference yields its self-reference, a register's record-manager decomposes into its composite key.
	// Empty value / no PK columns → the base fallback (the value IS the key).
	std::vector<ibValue> GetRowKeyByValue(const ibValue& value) const override {
		ibSourceDataObject* src = nullptr;
		if (value.IsEmpty() || !value.ConvertToValue(src) || src == nullptr)
			return ibQueryableSourceDescriptor::GetRowKeyByValue(value);
		std::vector<ibValue> rowKey;
		for (const ibBackendQueryColumn* kc : m_queryable.GetPrimaryKeyColumns()) {
			if (kc == nullptr) continue;
			ibValue v;
			src->GetValueBySourceHop(ibSourceHop{ kc->GetColumnId() }, v);
			rowKey.push_back(v);
		}
		return rowKey.empty() ? ibQueryableSourceDescriptor::GetRowKeyByValue(value) : rowKey;
	}

protected:
	TMeta*     m_meta;        // for name / clsid + to build the queryable (non-const, like the old `this`)
	TQueryable m_queryable;   // the contained queryable (ibRecordQueryable / ibRegisterDataQueryable / ibConstantQueryable / …)
};

// The full SOURCE descriptor — a LIST source (record / register): the query descriptor PLUS the command + row surface,
// each call FORWARDED to the metaobject (which the descriptor knows by type). The metaobject carries the real
// behaviour, polymorphic down its OWN inheritance. No mixin, no interface — TMeta must have these methods or the
// template won't compile. A pure query source (constant) uses ibMetaQueryDescriptor instead. (m_meta is inherited —
// `this->` reaches the dependent base member.)
template <typename TQueryable, typename TMeta>
class ibMetaCommandDescriptor : public ibMetaQueryDescriptor<TQueryable, TMeta>
{
public:
	explicit ibMetaCommandDescriptor(TMeta* meta) : ibMetaQueryDescriptor<TQueryable, TMeta>(meta) {}

	// ROW DATA / presentation
	ibValue GetSelectValue(const ibRowMetaValues& rowValues) const override { return this->m_meta->GetSelectValue(rowValues); }
	ibUniqueKey GetItemKey(const ibRowMetaValues& rowValues) const override { return this->m_meta->GetItemKey(rowValues); }
	// (FillSourceExplorer is INHERITED from the query descriptor — asking a source what it holds is
	//  a query question, and a source that is not a list has it too.)
	// COMMAND INTERFACE
	void GetCommandCollection(const ibFormID& formType, std::vector<ibCommandItem>& commands) const override { this->m_meta->GetCommandCollection(formType, commands); }
	void CallAsCommand(ibActionID id, const ibUniqueKey& anchor, const ibUniqueKey& key, ibBackendValueForm* srcForm) const override { this->m_meta->CallAsCommand(id, anchor, key, srcForm); }
	// ENTRY
	void ShowValueByKey(const ibUniqueKey& key, ibBackendValueForm* srcForm) const override { this->m_meta->ShowValueByKey(key, srcForm); }
};

//meta object
class BACKEND_API ibValueMetaObjectRecordData
	: public ibValueMetaObjectGenericData {
	public:

	// EVERY STORED KIND MAY GO INTO A SECTION — catalogs and documents, processors and
	// reports, all three register families. This is the line they share, so the answer is
	// given once here instead of being spelled out as a branch per metatype in the editor.
	virtual bool IsInterfaceAllowed() const override { return true; }

public:

	virtual ibClassID ResolveChild(const ibClassID& clsid) const {
		if (clsid == g_metaAttributeCLSID)
			return clsid;
		// A common attribute's copy — gated by the object's own answer, not by a list kept
		// elsewhere. Only a declaration under Common ever creates one.
		if (clsid == g_metaCommonAttributeColumnCLSID && IsCompositionAllowed())
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
		FillArrayObjectByCommonAttribute(array);
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

	// ⭐ THE ONE QUESTION ABOUT WRITABILITY — "may this attribute be assigned?" — and the object's own
	// reference is its FIRST answer, not a separate check beside it. Callers ask this and nothing else;
	// a metatype that has more read-only columns of its own extends the answer.
	//
	// The reason it is one question: a caller that asked only about the reference would let through
	// every OTHER derived column, and each new one would have to be found and added at every call
	// site. Asked here, a metatype declares its own and every site obeys at once.
	//
	// Derived is the general case: a chart of accounts unfolds its analytics kinds into numbered
	// columns for the list and the query, but the fact lives in the SECTION and the copy is refreshed
	// from it on every write. Assigning such a column would create a second author for one fact, and
	// the write would overrule it without a word — a change that seems to work and is gone by the
	// next save.
	virtual bool IsReadOnlyAttribute(const ibMetaID& id) const { return IsDataReference(id); }

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
		// The copies belong here: an object's attributes ARE its own plus the common ones it
		// carries, and this is the list the RUNTIME reads to build a form
		// (ibValueRecordDataObjectCatalog::GetSourceExplorer walks it). Hiding them from the
		// DESIGNER TREE is a display decision and belongs where the tree is drawn — taking
		// them out of this list took them out of the form as well.
		FillArrayObjectByFilter<ibValueMetaObjectAttributeBase>(array, { g_metaAttributeCLSID, g_metaCommonAttributeColumnCLSID });
		return array;
	}

	//table — every tabular-section id (g_tabularSectionCLSIDs, metaObject.h): the RAM and DB-backed
	// variants plus each predefined section. An id missing from that list is not a table to this walk.
	std::vector<ibValueMetaObjectTableData*> GetTableArrayObject(
		std::vector<ibValueMetaObjectTableData*> array = std::vector<ibValueMetaObjectTableData*>()) const {
		FillArrayObjectByFilter<ibValueMetaObjectTableData>(array, g_tabularSectionCLSIDs);
		return array;
	}

#pragma endregion
#pragma region __filter_h__

	//any attribute 
	template <typename _T1>
	ibValueMetaObjectAttributeBase* FindAnyAttributeObjectByFilter(const _T1& id) const {
		return FindObjectByFilter<ibValueMetaObjectAttributeBase>(id, { g_metaAttributeCLSID, g_metaPredefinedAttributeCLSID, g_metaCommonAttributeColumnCLSID });
	}

	//attribute 
	template <typename _T1>
	ibValueMetaObjectAttributeBase* FindAttributeObjectByFilter(const _T1& id) const {
		return FindObjectByFilter<ibValueMetaObjectAttributeBase>(id, { g_metaAttributeCLSID });
	}

	//table — the same id list as GetTableArrayObject
	template <typename _T1>
	ibValueMetaObjectTableData* FindTableObjectByFilter(const _T1& id) const {
		return FindObjectByFilter<ibValueMetaObjectTableData>(id, g_tabularSectionCLSIDs);
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
	virtual ibBackendValueForm* GetFormByCommandType(ibInterfaceCommandType cmdType = ibInterfaceCommandType::ibInterfaceCommandType_Default) const {

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

// DOES A FIELD DECLARED FOR `itemMode` BELONG TO A RECORD OF `objMode`?
//
// The one sentence that answers it, written once. Every form builder spells this out by hand while
// laying out its fields (catalog, chart of accounts, chart of characteristic types — the same three
// lines each time), and the fill check did not spell it at all: a FOLDER was refused over a field
// its own form never shows it.
inline bool ibItemModeFits(ibItemMode itemMode, ibObjectMode objMode) {
	return objMode == ibObjectMode::OBJECT_FOLDER
		? (itemMode == ibItemMode::ibItemMode_Folder || itemMode == ibItemMode::ibItemMode_Folder_Item)
		: (itemMode == ibItemMode::ibItemMode_Item   || itemMode == ibItemMode::ibItemMode_Folder_Item);
}

//meta object with file
class BACKEND_API ibValueMetaObjectRecordDataExt : public ibValueMetaObjectRecordData {
	public:

	// AN OBJECT AND A MANAGER, and no reference — a data processor or a report is
	// something you OPEN and RUN, not something other data points at. It stores
	// no rows of its own, so there is nothing to reference; the external variants
	// (external data processor / external report) inherit exactly this.
	static constexpr unsigned s_features =
		ibMetaFeature_Object | ibMetaFeature_Manager;

#pragma region access_generic
	virtual bool AccessRight_Show() const { return AccessRight_Use(); }
	virtual bool AccessRight_Modify() const { return true; }
#pragma endregion
#pragma region access
	bool AccessRight_Use() const { return IsFullAccess() || AccessRight(m_roleUse); }
#pragma endregion

	//ctor
	ibValueMetaObjectRecordDataExt();

	//create from file?
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
// ⭐ TEMPLATED ON THE METAOBJECT IT READS, for one reason: a subclass that reads a MORE SPECIFIC
// metatype would otherwise keep a second pointer to the same object beside `m_meta`, just to have it
// under the right type. Naming the type here means a specialisation inherits it already typed
// (ibRecorderQueryable, below). Same arrangement as ibComputedRegisterQueryable<TReg>.
//
// Bodies stay in commonObjectMetaQuery.cpp — the specialisations that exist are instantiated
// explicitly at the end of that file, so the header carries declarations only.
// ⭐ WHERE A READING SURFACE IS DECLARED: immediately before the KIND it reads for, not in a block of
// its own. Each kind owns one — an enumeration, a hierarchy, a recorder, a register — and reading it
// beside its metaobject is how the pair stays legible. (The generic ibValueMetaObjectRecordDataRef
// has none: "a reference" is a base to mix into, and it declares GetQueryable pure so a kind cannot
// forget to name its own.)
template <typename TMeta>
class ibRecordQueryable : public ibBackendQueryable {
public:
	explicit ibRecordQueryable(const TMeta* meta) : m_meta(meta) {}

	// The attribute by name AS a column (an attribute IS-A ibBackendQueryColumn). The L3 surface
	// returns a column; the DB provider static_casts it back to the attribute when it needs the field
	// machinery — the queryable itself names no attribute on its contract.
	virtual const ibBackendQueryColumn* ResolveColumnByName(const wxString& name) const override {
		return m_meta->template FindObjectByFilter<ibValueMetaObjectAttributeBase>(name,
			{ g_metaAttributeCLSID, g_metaPredefinedAttributeCLSID, g_metaCommonAttributeColumnCLSID });
	}

	// All generic attributes (predefined + plain) — each IS-A column. Drives SELECT * over this source.
	virtual std::vector<const ibBackendQueryColumn*> GetColumns() const override {
		std::vector<const ibBackendQueryColumn*> cols;
		for (const ibValueMetaObjectAttributeBase* a : m_meta->GetGenericAttributeArrayObject())
			cols.push_back(a);
		return cols;
	}

	virtual wxString GetQueryTableName() const override { return m_meta->GetPhysicalTableName(); }
	virtual wxString GetQueryName()      const override { return m_meta->GetName(); }
	virtual const ibMetaData* GetMetaData() const override { return m_meta->GetMetaData(); }

	// The uniqueness key (UPSERT match + dot-walk self-reference + row identity) — the data-reference
	// attribute (the pure-guid self-reference blob the row stores; type is the _RTRef column). No
	// GetRowKeyColumn / IsReferenceAttribute / GetIdentitySort — all of them derived from this one
	// authority, and the last was retired for pretending to be a second: it answered with a SORT whose
	// tail happened to be the key, so a source sorting by something else first handed a number to
	// everyone who wanted identity. (docs/query-language-arc.md §22.1)
	virtual std::vector<const ibBackendQueryColumn*> GetPrimaryKeyColumns() const override {
		const ibValueMetaObjectAttributeBase* refAttr = m_meta->GetDataReference();
		if (refAttr == nullptr)
			return {};
		return { refAttr };
	}
	// (ResolveReferenceTarget/Targets moved to ibDbTableProvider — the provider owns metadata.)

	// The metaobject knows whether it is hierarchical — its CLASS is the signal. Virtual dispatch down
	// its own inheritance, no cast; null on a flat ref.
	virtual const ibBackendQueryColumn* GetHierarchyColumn() const override { return m_meta->GetHierarchyColumn(); }

	// Same route: the ARRANGEMENT travels whole, and each caller reads off it the distinction it needs
	// instead of being handed one boolean per question.
	virtual ibHierarchyType GetHierarchyType() const override { return m_meta->GetHierarchyType(); }

	// The metaobject behind the source — the front reads its icon / presentation off it (through the
	// dynamic list's GetSourceMetaObject → GetSourceQueryable → here).
	virtual const ibValueMetaObjectGenericData* GetSourceMetaObject() const override { return m_meta; }

protected:
	const TMeta* m_meta;   // …already the type a specialisation needs — no second pointer beside it
};

// (NO ALIAS FOR ONE SPECIALISATION. Each KIND names the one it reads through, spelled out where it
//  declares its descriptor — an alias for the "usual" one is a default nobody chose, and it made
//  `friend class ibRecordQueryable` declare a NEW class instead of befriending the template.)

// The source's COMMAND surface lives on the metaobjects below (record / register / constant), as plain virtual
// methods polymorphic down their OWN inheritance. The templated source descriptor (queryableFactory.h) FORWARDS
// each call to its metaobject — no mixin, no separate command interface, nothing on the generic metaobject base.

//meta object with reference
class BACKEND_API ibValueMetaObjectRecordDataRef : public ibValueMetaObjectRecordData, public ibBackendQueryableHolder {
	public:


	// A REFERENCE AND A MANAGER — the least a reference-bearing metatype has.
	// Every one of them registers both (OnBeforeRunMetaObject: registerReference
	// + registerManager), and an ENUMERATION stops exactly here: it has
	// references to its values and a manager to reach them, and no object,
	// because there is nothing to open and edit.
	static constexpr unsigned s_features =
		ibMetaFeature_Reference | ibMetaFeature_Manager;

protected:
	//ctor
	ibValueMetaObjectRecordDataRef();
	virtual ~ibValueMetaObjectRecordDataRef();
public:

	// The metaobject VENDS its queryable; it does NOT navigate itself. ibRecordQueryable
	// (a friend) owns the navigation, built from this metaobject's primitives
	// (FindObjectByFilter / GetPhysicalTableName / IsDataReference / guidName). L4 and the door
	// read through GetQueryable().
	// ⭐⭐ PURE HERE — every KIND must name its own. "A reference" has no reading surface of its own,
	// and leaving an inherited implementation in place would let a kind forget to declare one and read
	// through somebody else's without a word. Declared abstract, the compiler asks the question at the
	// only moment it is cheap to answer.
	virtual const ibBackendQueryable* GetQueryable() const override = 0;

	// ⚠ THE FRIEND IS THE TEMPLATE, not a class — `friend class ibRecordQueryable` would declare a NEW
	// class of that name beside the template (C2371, and 2800 errors behind it).
	template <typename TMeta> friend class ibRecordQueryable;

	virtual ibValueMetaObjectAttributePredefined* GetDataReference() const { return m_propertyAttributeReference->GetMetaObject(); }


	virtual bool IsDataReference(const ibMetaID& id) const override { return id == (*m_propertyAttributeReference)->GetMetaID(); }

	// value(...) — a reference record vends its EMPTY reference (member "EmptyRef"); the hierarchy level below adds
	// predefined items. False on an unknown member (the query engine throws). (Body in commonObjectMetaQuery.cpp.)
	virtual bool ResolveQueryConstant(const wxString& member, ibValue& out) const override;

	// ---- Source surface (the descriptor forwards each call here). The reference-record BASE = the ENUM variant:
	// it selects by the reference cell, fills the list showing ONLY the reference visible (an enum's value IS its
	// reference), and lists no commands (an enum shows none — the writeable levels override to add them). The
	// writeable / hierarchy levels OVERRIDE FillSourceExplorer with their own column set. Bodies in commonObjectAction.cpp.
	//
	// ROW DATA / presentation — what a row IS and how it shows:
	virtual ibValue GetSelectValue(const ibRowMetaValues& rowValues) const;                        // the row's REFERENCE cell
	virtual ibUniqueKey GetItemKey(const ibRowMetaValues& rowValues) const;                        // the row's REFERENCE guid
	virtual void FillSourceExplorer(ibSourceDataObject::ibSourceExplorer& explorer) const;         // enum: reference VISIBLE, rest hidden
	// COMMAND INTERFACE — the command band (the writeable levels override to fill these):
	virtual void GetCommandCollection(const ibFormID& /*formType*/, std::vector<ibCommandItem>& /*commands*/) const {}
	virtual void CallAsCommand(ibActionID /*id*/, const ibUniqueKey& /*anchor*/, const ibUniqueKey& /*key*/, ibBackendValueForm* /*srcForm*/) const {}
	// ENTRY — open a row's value directly (writeable levels override):
	virtual void ShowValueByKey(const ibUniqueKey& /*key*/, ibBackendValueForm* /*srcForm*/) const {}

	// The hierarchy (parent) column — a metadata source's OWN structural signal, reached by the record queryable
	// through VIRTUAL dispatch (no cast: the metaobject's CLASS is the "hierarchical" indicator — a catalog / chart
	// IS hierarchical by being one). Null on a flat reference; a HIERARCHY overrides it with its parent attribute.
	// (Folder column removed — folders are a creation-time sort/filter setting, not a structural column.)
	virtual const ibBackendQueryColumn* GetHierarchyColumn() const { return nullptr; }

	// THE ARRANGEMENT — one value, four states (queryable.h). A metaobject that cannot have a parent
	// answers `eNone`; the hierarchy class overrides with what its property says. The record queryable
	// forwards here, same route as the column above.
	virtual ibHierarchyType GetHierarchyType() const { return ibHierarchyType::eNone; }

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
		// Same gate as the RAM owner above — the object answers for itself.
		if (clsid == g_metaCommonAttributeColumnCLSID && IsCompositionAllowed())
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

	// ⭐⭐ HOW A METATYPE'S OWN VALUES ARE READ, AND HOW THEY ARE ORDERED. Two questions of one kind: both
	// are facts about the METATYPE, and both are answered where the metatype lives rather than by whoever
	// happens to be holding one of its values.
	//
	// ⭐⭐ THE FIRST ANSWERS WHETHER THERE IS ONE, SEPARATELY FROM WHAT IT SAYS. `false` means this
	// metatype has nothing to show for that value; `true` with an empty `out` means the presentation IS
	// empty, which is a different fact and a legitimate one.
	//
	// 🛑 Returning a bare string made those two the same answer, and the caller then had to guess — the
	// very shape that cost the whole of 2026-08-29 elsewhere: a comparison that said "equal" when it
	// meant "I don't know", and a filter that dropped a line because its value was falsy. One value
	// carrying two meanings is a defect wherever it appears.
	virtual bool GenerateDataDesc(const ibValueDataObject* objValue, wxString& out) const = 0;

	// ⚠ PURE, like its neighbour, so no family can forget to say. "I have no order of my own" is an
	// ANSWER — `return 0` — and the caller then settles it by IDENTITY, which is the only comparison
	// allowed to end at 0. A catalog says exactly that; an ENUMERATION does not, because its members are
	// a sequence the author wrote down and the platform keeps as data.
	//
	// 🛑 This used to be asked INSIDE the reference value: a `dynamic_cast` to the enumeration metaobject
	// and a reach for its `Order` attribute, sitting in a class that has no business knowing what an
	// enumeration is — and the next metatype with a sequence of its own would have joined the same chain
	// (Max, 2026-08-29: *"like I did with the presentation — then you have no casts at all"*).
	//
	// ⚠ NEVER A DATABASE READ. A comparison runs per pair in every sort; the reference only asks this
	// once both rows are already in hand, and an override must not go looking for more.
	virtual int CompareDataValues(const ibValueDataObject* lhs, const ibValueDataObject* rhs) const = 0;

	//special functions for DB
	virtual wxString GetPhysicalTableName() const;

protected:


	//get default form
	virtual ibBackendValueForm* GetFormByCommandType(ibInterfaceCommandType cmdType = ibInterfaceCommandType::ibInterfaceCommandType_Default) const {

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
	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

protected:

	ibPropertyCategory* m_categoryData = ibPropertyObject::CreatePropertyCategory(wxT("Data"), _("Data"));
	ibPropertyCategory* m_categoryPresentation = ibPropertyObject::CreatePropertyCategory(wxT("Presentation"), _("Presentation"));
	ibPropertyBoolean* m_propertyQuickChoice = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryPresentation, wxT("QuickChoice"), _("Quick choice"), false);
	ibPropertyContainer<>* m_propertyAttributeReference = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateSpecialType(wxT("Ref"), _("Ref"), wxEmptyString, ibValue::GetIDByVT(ibValueTypes::TYPE_EMPTY)));

	// ⭐⭐ NO SOURCE DESCRIPTOR HERE — "a reference" is a base to MIX INTO, not a readable source.
	//
	// The four KINDS below own one each, typed to themselves, and each registers its own with the
	// factory: an ENUMERATION, a HIERARCHY, a RECORDER (document), a REGISTER. Declared once up here it
	// had ONE type for everything beneath it — so a kind with its own reading surface could vend a
	// column and watch every query resolve through the other descriptor and never see it. That is what
	// hid the recorder's moment column (2026-08-23).

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

	// THE ENUMERATION'S PAIR — its own queryable, typed to this kind, registered by this kind
	// (OnAfterRun / OnBeforeClose in the .cpp).
	virtual const ibBackendQueryable* GetQueryable() const override { return m_queryable.GetQueryable(); }

	//descriptions...
	//
	// ⭐⭐ AN ENUMERATION MEMBER IS DECLARED, NOT STORED — it reads as the configuration writes it in the
	// designer and as the synonym a person put on it at run time.
	virtual bool GenerateDataDesc(const ibValueDataObject* objValue, wxString& out) const override;

	// ⭐⭐ …AND IT HAS AN ORDER OF ITS OWN, which is the whole point of this override: the members are a
	// SEQUENCE the author wrote down — "Wholesale, Retail" means something in that order — and the
	// platform keeps it as DATA, in the predefined `Order` attribute this class owns. By the guid the
	// members came out in an order nobody chose; by their text they would follow the alphabet of the
	// presentations, which moves when they are translated.
	virtual int CompareDataValues(const ibValueDataObject* lhs, const ibValueDataObject* rhs) const override;

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
		// ORDER IS AN ATTRIBUTE OF THE OBJECT, so it belongs in the list that MAKES attributes real:
		// this list becomes the columns, the queryable's columns and the object's members. It was
		// declared as a property and left out of here, so the position existed in the metadata and
		// nowhere else - no column to write it into, and nothing for a list to sort by. The values
		// then came back in whatever order the table handed them over, which is not the order the
		// configuration declares them in and is what a person picking from the list actually reads.
		array.push_back(m_propertyAttributeOrder->GetMetaObject());
		return true;
	}

	//searched array 
	virtual bool FillArrayObjectBySearched(std::vector<ibValueMetaObjectAttributeBase*>& array) const { return true; }

	// Declare the enum table (uuid key) + its values as SEED rows (diffed by uuid: new -> insert, gone -> delete).
	virtual void ContributeTables(ibSchemaSnapshot& out) const override;

	//load & save metaData from DB
	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

private:

	//default attributes 
	ibPropertyContainer<>* m_propertyAttributeOrder = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateNumber(wxT("Order"), _("Order"), wxEmptyString, 6, true));

	// …and this kind's own source descriptor — typed to the ENUMERATION kind, registered by it.
	ibMetaCommandDescriptor<ibRecordQueryable<ibValueMetaObjectRecordDataEnumRef>, ibValueMetaObjectRecordDataEnumRef> m_queryable{ this };
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


	// …AN OBJECT AND A SELECTION. What separates a catalog or a document from an
	// enumeration: its rows are stored, so they are opened and edited
	// (registerObject) and they can be walked (registerSelection). An enumeration
	// has neither — which is why both bits are added HERE and not on the
	// reference-bearing base it shares with one.
	static constexpr unsigned s_features =
		ibValueMetaObjectRecordDataRef::s_features
		| ibMetaFeature_Object | ibMetaFeature_Selection;

	// COMMON ATTRIBUTES LAND HERE — on the line a catalog and a document share, which is
	// the line that has stored, editable rows to put a column on. Registers and the
	// unstored kinds (processors, reports) are deliberately left out: for a register it is
	// a decision about data separation that has not been taken, and an unstored object has
	// no table at all.
	virtual bool IsCompositionAllowed() const override { return true; }

	public:
protected:
	//ctor
	ibValueMetaObjectRecordDataMutableRef();
	virtual ~ibValueMetaObjectRecordDataMutableRef();
public:

#pragma region access_generic
	virtual bool AccessRight_Show() const { return AccessRight_Read(); }
	virtual bool AccessRight_Modify() const { return AccessRight_Write(); }
	virtual bool AccessRight_Erase() const { return AccessRight_Delete(); }
#pragma endregion

	// ("Write" carries the legacy storage typo — kept until the migration arc.)
	IB_DECLARE_RWD_ROLE_TRIPLET("Write")

	ibMetaDescription& GetGenerationDescription() const { return m_propertyGeneration->GetValueAsMetaDesc(); }

	ibValueMetaObjectAttributePredefined* GetDataVersion() const { return m_propertyAttributeDataVersion->GetMetaObject(); }
	bool IsDataVersion(const ibMetaID& id) const { return id == (*m_propertyAttributeDataVersion)->GetMetaID(); }

	ibValueMetaObjectAttributePredefined* GetDataDeletionMark() const { return m_propertyAttributeDeletionMark->GetMetaObject(); }
	bool IsDataDeletionMark(const ibMetaID& id) const { return id == (*m_propertyAttributeDeletionMark)->GetMetaID(); }

	// (THE POINT IN TIME MOVED DOWN, to ibValueMetaObjectRecordDataRecorderRef — the layer that has a
	//  DATE. It used to be declared here, and from here a catalogue inherited it: a moment is a date
	//  plus the record standing at it, and a catalogue has no date, so what it inherited could never
	//  be read. Recording movements and being placed in time are the same layer, which is the one
	//  below.)

	// DOCUMENT variant — reference / deletion mark / data version hidden; number / date / posted / attributes
	// visible. Fills its columns directly by its own predicates. Body in commonObjectAction.cpp.
	virtual void FillSourceExplorer(ibSourceDataObject::ibSourceExplorer& explorer) const override;

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

	// This source's OWN command ids (class-scoped — no global enum, no collision with the action system). eEditValue
	// bakes the front inline-edit flag (standardCommand.h eStartEditingFlag). Ids < 32767 (wxMenuItem), band 1..27.
	// Subclasses inherit these and add their own (Document +Post, hierarchy +AddFolder).
	enum {
		eAddValue = 1,
		eCopyValue,
		eEditValue = 3 | eStartEditingFlag,
		eDeleteValue = 4,
		eMarkAsDeleteValue = 26,
	};

	// source COMMANDS (override of GenericData) — the writeable-ref base command set. GetCommandCollection lists
	// Add/Copy/Edit/Delete/MarkAsDelete (+AddFolder when the source has a folder column); CallAsCommand runs one
	// by id against `key` (create/copy/open/delete/mark) and refreshes `srcForm`; ShowValueByKey opens the row's form.
	// Bodies in objectList.cpp (next to the list models they were lifted from). Document overrides to add Post.
	virtual void GetCommandCollection(const ibFormID& formType, std::vector<ibCommandItem>& commands) const override;
	virtual void CallAsCommand(ibActionID id, const ibUniqueKey& anchor, const ibUniqueKey& key, ibBackendValueForm* srcForm) const override;
	virtual void ShowValueByKey(const ibUniqueKey& key, ibBackendValueForm* srcForm) const override;

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
	virtual void OnPropertyRefresh() override;
	virtual bool OnPropertyChanging(ibProperty* property, const wxVariant& newValue);
	virtual void OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue);

	// Declare the record's main table + its tabular-section tables into the structure snapshot.
	virtual void ContributeTables(ibSchemaSnapshot& out) const override;

	//load & save metaData from DB
	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

	//create empty object
	virtual ibValueRecordDataObjectRef* CreateObjectRefValue(const ibGuid& objGuid = wxNullGuid) const = 0; //create object and read by guid

protected:

	ibPropertyGeneration* m_propertyGeneration = ibPropertyObject::CreateProperty<ibPropertyGeneration>(m_categoryData, wxT("ListGeneration"), _("List generation"));
	ibPropertyContainer<>* m_propertyAttributeDataVersion = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateString(wxT("DataVersion"), _("Data version"), wxEmptyString, 12, ibItemMode_Folder_Item));
	ibPropertyContainer<>* m_propertyAttributeDeletionMark = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateBoolean(wxT("DeletionMark"), _("Deletion mark"), wxEmptyString));

	// Read/Write/Delete role triplet emitted by IB_DECLARE_RWD_ROLE_TRIPLET
	// above (in the public access region).
};

class BACKEND_API ibValueMetaObjectRecordDataRecorderRef;

// ==========================================================================
// ibRecorderQueryable — THE RECORDER'S READING SURFACE: the record queryable plus the MOMENT.
//
// A metaobject holds what it DECLARES, and everything it declares is physical — attributes with
// fields behind them. A column that is CONSTRUCTED rather than stored belongs where the reading is,
// which is here; the tabular section arranges its owner reference exactly so (ibTabularQueryable).
//
// So this specialises the shared ibRecordQueryable in one respect: it vends one more column beside
// the attributes. Nothing in the shared surface knows moments exist.
// ==========================================================================
class BACKEND_API ibRecorderQueryable : public ibRecordQueryable<ibValueMetaObjectRecordDataRecorderRef> {
public:
	explicit ibRecorderQueryable(const ibValueMetaObjectRecordDataRecorderRef* meta);

	// The moment answers first, then the attributes — a query naming it resolves to the column that
	// knows how to build it. (Bodies in commonObjectMetaQuery.cpp.)
	virtual const ibBackendQueryColumn* ResolveColumnByName(const wxString& name) const override;
	virtual std::vector<const ibBackendQueryColumn*> GetColumns() const override;

	// ⭐⭐ THE MOMENT AS A COLUMN — and it reads ITSELF.
	//
	// There is no such thing as a "synthetic column" in general: this one is the moment and nothing
	// else, which is why it is declared here rather than in the query tiers. Its identity is the
	// moment attribute's (name, id, type); its VALUE is stored nowhere, so the tagged read the codec
	// performs cannot describe it — and rather than teaching the codec about moments, the column
	// overrides the read and assembles the value out of the date and the reference, each of which is
	// read exactly as it is read anywhere else.
	class BACKEND_API ibBackendColumnPointInTime : public ibBackendQueryColumn {
	public:
		explicit ibBackendColumnPointInTime(const ibValueMetaObjectRecordDataRecorderRef* owner) : m_owner(owner) {}

		wxString GetName()         const override;
		wxString GetSynonym()      const override;
		wxString GetPhysicalName() const override;
		ibMetaID GetColumnId()     const override;
		ibTypeDescription& GetTypeDesc() const override;

		// WHERE IT LIES: nowhere of its own — in the DATE, then in the REFERENCE. Answering with their
		// layouts is what makes the existing mechanisms work on it: a sort emits date, then identifier
		// (a reference already sorts by its own two fields), and a comparison keys on the same ones.
		std::vector<ibColumnSlot> DescribeLayout() const override;

		// …and its KIND says why: synthetic — those fields are the date's and the reference's, and
		// they are the ones that write them.
		Kind GetColumnKind() const override { return Kind::Synthetic; }

		bool ReadValue(const wxString& fieldName, const class ibMetaData* metaData,
		               class ibValue& retValue, class ibQueryResult& result, bool createData = true) const override;

		// …and it BINDS the same way: into the parts' fields, in the order the layout names them.
		// Nothing STORES a moment — a document stores its date and its reference, and the pair is the
		// moment — but a query WRITES one every time it compares against it (`WHERE Moment <= &Point`
		// decomposes lexicographically over exactly these fields), and that is the value it compares.
		void BindValue(class ibQueryStatement& statement, const class ibMetaData* metaData,
		               const class ibValue& value, int& position) const override;

	private:
		const ibValueMetaObjectRecordDataRecorderRef* m_owner;
	};

private:
	ibBackendColumnPointInTime m_momentColumn;   // `m_meta` above is already the recorder — see the template
};

// ==========================================================================
// ibValueMetaObjectRecordDataRecorderRef — A REF THAT RECORDS MOVEMENTS, AND THEREFORE SITS IN TIME.
//
// The metaobject twin of ibValueRecordDataObjectRecorderRef (the VALUE, below in this file — "ref
// with movements": posting, the register cascade, un-posting on a deletion mark). The value side has
// had this layer since 2026-05-25; the metaobject side never did, and its absence is what put the
// MOMENT on the shared base, from where a catalogue inherited a field it could not build.
//
// It sits beside the hierarchy layer, not above it: `MutableRef` splits into what RECORDS (this) and
// what is ARRANGED IN A TREE (…HierarchyMutableRef), and a metatype takes the one it is.
//
// Three things live here because they are one fact:
//   * the DATE — when it happened;
//   * the RECORD DESCRIPTION — which registers it writes into;
//   * the MOMENT — the date plus the record standing at it, which is how "everything up to THIS
//     document" is asked. It is not stored: it is CONSTRUCTED from the date and the reference
//     (m_momentColumn), and the codec reads a point in time out of the three fields those two have.
// ==========================================================================
class BACKEND_API ibValueMetaObjectRecordDataRecorderRef : public ibValueMetaObjectRecordDataMutableRef {
public:

	ibMetaDescription& GetRecordDescription() const { return m_propertyRegisterRecord->GetValueAsMetaDesc(); }

	// The NUMBER and the DATE — what a recorded fact is identified and placed by. They come as a pair:
	// a document is looked up by number and ordered by date, and both are indexed for that reason.
	ibValueMetaObjectAttributePredefined* GetDocumentNumber() const { return m_propertyAttributeNumber->GetMetaObject(); }
	ibValueMetaObjectAttributePredefined* GetDocumentDate() const { return m_propertyAttributeDate->GetMetaObject(); }
	bool IsDocumentDate(const ibMetaID& id) const { return id == (*m_propertyAttributeDate)->GetMetaID(); }

	// ⚠ IT REACHES THE QUERY AND NOTHING ELSE. Deliberately absent from
	// FillArrayObjectByPredefinedAttribute — the list that becomes columns, object members and form
	// fields — so the moment gets no storage, appears on no form, and is not part of what a record is.
	// It is a way to ASK, not a thing to keep: the date and the reference it is built from are already
	// stored, and storing their pair again would be two answers to one question.
	ibValueMetaObjectAttributePredefined* GetPointInTime() const { return m_propertyAttributePointInTime->GetMetaObject(); }
	bool IsPointInTime(const ibMetaID& id) const { return id == (*m_propertyAttributePointInTime)->GetMetaID(); }

	// (THE MOMENT AS A COLUMN lives in the QUERYABLE — ibRecorderQueryable, below. A metaobject holds
	//  what it DECLARES, which is physical: the attributes and their properties. A column that is
	//  constructed rather than stored belongs where the reading happens, the same way a tabular
	//  section's owner reference belongs to ibTabularQueryable and not to the section metaobject.)

	// The base variant plus the moment, which only this layer has.
	virtual void FillSourceExplorer(ibSourceDataObject::ibSourceExplorer& explorer) const override;

	//events:
	virtual bool OnCreateMetaObject(ibMetaData* metaData, int flags) override;
	virtual bool OnLoadMetaObject(ibMetaData* metaData) override;
	virtual bool OnSaveMetaObject(int flags) override;
	virtual bool OnDeleteMetaObject() override;

	virtual bool OnBeforeRunMetaObject(int flags) override;
	virtual bool OnAfterRunMetaObject(int flags) override;

	virtual bool OnBeforeCloseMetaObject() override;
	virtual bool OnAfterCloseMetaObject() override;

	//descriptions...
	//
	// ⭐ A RECORDED FACT READS AS ITS NUMBER AND ITS MOMENT — which is exactly the pair this class owns,
	// so the sentence belongs here and not in each metatype that records one.
	virtual bool GenerateDataDesc(const ibValueDataObject* objValue, wxString& out) const override;

	// …and it has no order of its own: two records are told apart by identity.
	virtual int CompareDataValues(const ibValueDataObject* lhs, const ibValueDataObject* rhs) const override;

protected:

	ibValueMetaObjectRecordDataRecorderRef();
	virtual ~ibValueMetaObjectRecordDataRecorderRef();

	// Additive contract — chains to MutableRef and adds the NUMBER and the DATE. The moment stays out,
	// for the reason on GetPointInTime.
	virtual bool FillArrayObjectByPredefinedAttribute(std::vector<ibValueMetaObjectAttributeBase*>& array) const override {
		ibValueMetaObjectRecordDataMutableRef::FillArrayObjectByPredefinedAttribute(array);
		array.push_back(m_propertyAttributeNumber->GetMetaObject());
		array.push_back(m_propertyAttributeDate->GetMetaObject());
		return true;
	}

	// What a search runs over — the two a person actually looks a recorded fact up by.
	virtual bool FillArrayObjectBySearched(std::vector<ibValueMetaObjectAttributeBase*>& array) const override {
		array = { m_propertyAttributeNumber->GetMetaObject(), m_propertyAttributeDate->GetMetaObject() };
		return true;
	}

	// The "code" of a recorded fact is its NUMBER — what the picker shows and what a quick choice
	// matches against.
	virtual ibValueMetaObjectAttributeBase* GetAttributeForCode() const override { return m_propertyAttributeNumber->GetMetaObject(); }

	//load & save metaData from DB
	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

protected:

	ibPropertyRecord* m_propertyRegisterRecord = ibPropertyObject::CreateProperty<ibPropertyRecord>(m_categoryData, wxT("ListRegisterRecord"), _("List register record"));
	ibPropertyContainer<>* m_propertyAttributeNumber = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateString(wxT("Number"), _("Number"), wxEmptyString, 11, true, ibItemMode::ibItemMode_Item, ibSelectMode::ibSelectMode_Items, ibIndexingMode::ibIndexingMode_Index));
	ibPropertyContainer<>* m_propertyAttributeDate = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateDate(wxT("Date"), _("Date"), wxEmptyString, ibDateFractions::ibDateFractions_DateTime, true, ibItemMode::ibItemMode_Item, ibSelectMode::ibSelectMode_Items, ibIndexingMode::ibIndexingMode_Index));
	// The moment — registered like the date above, typed as the PointInTime value. See GetPointInTime
	// for why it never joins the predefined list.
	ibPropertyContainer<>* m_propertyAttributePointInTime = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateSpecialType(wxT("PointInTime"), _("Point in time"), wxEmptyString, value_to_clsid(wxT("PointInTime"))));

	// …and this kind's own source descriptor. THIS is the queryable that vends the MOMENT beside the
	// stored attributes — the reason the kinds have their own at all: a synthetic column belongs to a
	// KIND, and only the descriptor of that kind can hand it out.
	ibMetaCommandDescriptor<class ibRecorderQueryable, ibValueMetaObjectRecordDataRecorderRef> m_queryable{ this };

public:
	virtual const ibBackendQueryable* GetQueryable() const override { return m_queryable.GetQueryable(); }
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

	// What a parent may be here — THE declaration, and the one the query tier reads through the
	// queryable (it overrides the base's `eNone`). Asked wherever a parent is CHOSEN or IMPLIED — the
	// select mode of the Parent field, the "create inside this node" rule, the list a folder picker
	// shows, and whether a list walks levels at all.
	virtual ibHierarchyType GetHierarchyType() const override { return m_propertyHierarchyType->GetValueAsEnum(); }

	// An item-subordinated hierarchy (a chart of accounts) has no separate container kind: every
	// node is the same thing, and any node may hold children. Asking this instead of comparing the
	// enum keeps the question readable at the call sites that only care which of the two it is.
	bool IsItemHierarchy() const { return GetHierarchyType() == ibHierarchyType::eItems; }

	// Does the ENGINE navigate a tree here — level fetch, drill, hierarchy folding? Only the two real
	// hierarchies. SUBORDINATION keeps its parent as ordinary data and answers NO: nothing is built on
	// it unless someone asks for it in a query or a grouping.
	bool IsHierarchical() const {
		const ibHierarchyType type = GetHierarchyType();
		return type == ibHierarchyType::eItems || type == ibHierarchyType::eFoldersAndItems;
	}

	// Is there a PARENT FIELD at all? True for the three arrangements that record one; only `None`
	// has no parent to speak of. Separate from IsHierarchical on purpose — storing a parent and
	// navigating one are different claims.
	bool HasParentLink() const { return GetHierarchyType() != ibHierarchyType::eNone; }

	// Are there FOLDERS — a second kind of node whose whole purpose is to contain? Only the
	// folders-and-items arrangement has them.
	bool HasFolders() const { return GetHierarchyType() == ibHierarchyType::eFoldersAndItems; }

	// TURN THE DECLARATION INTO THE TWO PREDEFINED ATTRIBUTES IT GOVERNS.
	//
	// Parent's SELECT MODE is where every consumer already looks — choosing a parent, drawing the
	// picker and greying the field all read GetSelectMode(), so none of them has to learn about
	// hierarchy types.
	//
	// And PRESENCE, which is the part a boolean-plus-enum could not express: an attribute that this
	// arrangement has no use for is not present in this configuration, which is exactly what
	// metaDisableFlag says (the same flag a catalog with no owner puts on Owner, and an independent
	// register on Recorder). Being absent, it leaves every metadata walk — so its column leaves the
	// schema too, and a flat catalog stops carrying a parent it can never fill.
	void ApplyHierarchyType() {
		(*m_propertyAttributeParent)->SetSelectMode(HasFolders()
			? ibSelectMode::ibSelectMode_Folders
			: ibSelectMode::ibSelectMode_Items);

		auto present = [](ibValueMetaObjectAttributePredefined* attribute, bool exists) {
			if (attribute == nullptr) return;
			if (exists) attribute->ClearFlag(metaDisableFlag);
			else        attribute->SetFlag(metaDisableFlag);
		};
		present(GetDataParent(),   HasParentLink());   // nothing to point at only in a flat list
		present(GetDataIsFolder(), HasFolders());      // no folders unless folders ARE the arrangement
	}

	// Declared by the OWNER of the hierarchy, so a subclass whose nodes are all of one kind (a chart
	// of accounts) states it once at construction instead of leaving it to the user to discover.
	void SetHierarchyType(ibHierarchyType type) {
		m_propertyHierarchyType->SetValue(type);
		ApplyHierarchyType();
	}

	// A catalog / chart IS hierarchical by its class — it returns its parent attribute (the record queryable forwards
	// here, no cast). Body in commonObjectMetaQuery.cpp (attribute→column upcast complete there).
	virtual const ibBackendQueryColumn* GetHierarchyColumn() const override;

	// CATALOG variant — reference / deletion mark / data version / predefined name / parent / is-folder hidden;
	// code / description / attributes visible. Fills its columns directly. Body in commonObjectAction.cpp.
	virtual void FillSourceExplorer(ibSourceDataObject::ibSourceExplorer& explorer) const override;

	virtual bool HasQuickChoice() const { return m_propertyQuickChoice->GetValueAsBoolean(); }

	ibValueMetaObjectRecordDataHierarchyMutableRef();
	virtual ~ibValueMetaObjectRecordDataHierarchyMutableRef();

	enum { eAddFolder = 27 };   // this source's OWN folder command (adds to the inherited writeable set)

	// source COMMANDS — the base writeable set PLUS AddFolder (gated by the source having a folder column, so a
	// flat catalog reaching here shows no folder command). CallAsCommand handles eAddFolder (new folder) and
	// delegates the rest to the base. Bodies in objectList.cpp.
	virtual void GetCommandCollection(const ibFormID& formType, std::vector<ibCommandItem>& commands) const override;
	virtual void CallAsCommand(ibActionID id, const ibUniqueKey& anchor, const ibUniqueKey& key, ibBackendValueForm* srcForm) const override;

	//process choice
	virtual bool ProcessChoice(ibBackendControlFrame* ownerValue,
		const wxString& strFormName = wxEmptyString, ibSelectMode selMode = ibSelectMode::ibSelectMode_Items) const;

	// ResolveChild — inherited from ibValueMetaObjectRecordDataRef unchanged (it was copied here verbatim,
	// comments included, and neither intermediate class re-declares it).

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

	//descriptions...
	//
	// ⭐⭐ HOW A ROW OF ANY OF THEM READS — ONE implementation, here, because it was one sentence copied
	// three times. A catalog, a chart of accounts and a chart of characteristic types all say the same
	// thing: in the designer the DECLARED form (the empty reference, or a predefined value written as
	// `CatalogRef.Goods.Chair`), and at run time the row's own `Description` — the very attribute this
	// class owns. Three copies meant three places to fix and three chances to drift; the families that
	// genuinely read differently (a document by number and date, an enumeration by its member) say so on
	// THEIR base, which is what a base is for (Max, 2026-08-29: *"it is duplicated in all of them except
	// the base classes — it can be left in the base"*, and *"we require it of the base metadata — base
	// enumeration, base hierarchy, base document — and each overrides it at its own level"*).
	virtual bool GenerateDataDesc(const ibValueDataObject* objValue, wxString& out) const override;

	// …and no order of its own: the rows of a catalog, a chart of accounts or a chart of characteristic
	// types are told apart by identity.
	virtual int CompareDataValues(const ibValueDataObject* lhs, const ibValueDataObject* rhs) const override;


	//is predefined value?
	bool HasPredefinedValue(const ibGuid& valueGuid) const { return FindPredefinedValue(valueGuid) != nullptr; }
	bool HasPredefinedValue(const wxString& strPredefinedName) const { return FindPredefinedValue(strPredefinedName) != nullptr; }

	// value(...) — the hierarchy level adds PREDEFINED items on top of the record base's EmptyRef: member ==
	// "EmptyRef" → the empty reference; else FindPredefinedValue(member) → a reference to that predefined item;
	// false on an unknown member (the query engine throws). (Body in commonObjectMetaQuery.cpp.)
	virtual bool ResolveQueryConstant(const wxString& member, ibValue& out) const override;

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

	virtual void OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue) override;

	// Declare the main table (via the base) + the predefined values as its SEED rows (cells keyed by
	// column id; the builder diffs by uuid + cells).
	virtual void ContributeTables(ibSchemaSnapshot& out) const override;

	//load & save metaData from DB 
	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

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
		// Parent / IsFolder are pushed UNCONDITIONALLY, and that is deliberate for now. ApplyHierarchyType
		// marks the unused one with metaDisableFlag, but making THIS list obey the mark is not a one-line
		// change: the readers of the list do not obey it either. ibValueRecordDataObjectHierarchyRef::
		// ReadData asks GetValueByMetaID for IsFolder on every record it loads, and the schema differ
		// compares a snapshot built from this list against one taken before the mark was applied - so
		// dropping the column here asserted on open and emitted an ALTER for a column the table already
		// had. Retiring a predefined column is its own arc: every walker has to agree at once.
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

	// THE SHAPE OF THE HIERARCHY — what a parent is allowed to be.
	//
	// A catalog nests items inside FOLDERS; a chart of accounts subordinates an account to another
	// ACCOUNT. Both are the same storage (a Parent column and an IsFolder flag), and the display
	// layer already copes with either — a node is a container when it is a folder OR when it has
	// children. What differs is only what a Parent field ACCEPTS, so this is a declaration rather
	// than a second mechanism, and switching it is not a restructuring.
	//
	// Default is FoldersAndItems, which is what every existing metaobject already behaves like.
	//
	// A CATEGORY OF ITS OWN: under Common this read as one more field beside Name / Synonym / Comment,
	// while it is the single declaration that decides whether the object has a tree at all, what a
	// parent may be, and whether Parent and IsFolder exist as columns. It governs an area, so it is
	// shown as one — and the settings that belong to the same subject have a place to land next to it.
	ibPropertyCategory* m_categoryHierarchy = ibPropertyObject::CreatePropertyCategory(wxT("Hierarchy"), _("Hierarchy"));
	ibPropertyEnum<ibValueEnumHierarchyType>* m_propertyHierarchyType = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumHierarchyType>>(m_categoryHierarchy, wxT("HierarchyType"), _("Hierarchy type"), ibHierarchyType::eFoldersAndItems);

	//create default attributes
	ibPropertyContainer<>* m_propertyAttributePredefined = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateString(wxT("PredefinedName"), _("Predefined name"), wxEmptyString, 150, ibItemMode::ibItemMode_Folder_Item));
	ibPropertyContainer<>* m_propertyAttributeCode = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateString(wxT("Code"), _("Code"), wxEmptyString, 8, true, ibItemMode::ibItemMode_Folder_Item, ibSelectMode::ibSelectMode_Items, ibIndexingMode::ibIndexingMode_Index));
	ibPropertyContainer<>* m_propertyAttributeDescription = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateString(wxT("Description"), _("Description"), wxEmptyString, 150, true, ibItemMode::ibItemMode_Folder_Item, ibSelectMode::ibSelectMode_Items, ibIndexingMode::ibIndexingMode_Index));
	ibPropertyContainer<>* m_propertyAttributeParent = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateEmptyType(wxT("Parent"), _("Parent"), wxEmptyString, ibItemMode::ibItemMode_Folder_Item, ibSelectMode::ibSelectMode_Folders, ibIndexingMode::ibIndexingMode_Index));
	ibPropertyContainer<>* m_propertyAttributeIsFolder = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryCommon, ibValueMetaObjectCompositeData::CreateBoolean(wxT("IsFolder"), _("Is folder"), wxEmptyString, ibItemMode::ibItemMode_Folder_Item));

	//predefinded vector
	std::vector<wxObjectDataPtr<ibPredefinedValueObject>> m_predefinedObjectVector;

	// …and this kind's own source descriptor — typed to the HIERARCHY kind, registered by it
	// (a catalogue, a chart of accounts, a chart of characteristic types, a job all read through it).
	ibMetaCommandDescriptor<ibRecordQueryable<ibValueMetaObjectRecordDataHierarchyMutableRef>, ibValueMetaObjectRecordDataHierarchyMutableRef> m_queryable{ this };

public:
	virtual const ibBackendQueryable* GetQueryable() const override { return m_queryable.GetQueryable(); }
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
	virtual const ibMetaData* GetMetaData() const override;                      // metadata context for column-based value reads
	virtual std::vector<const ibBackendQueryColumn*> GetPrimaryKeyColumns() const override;   // recorder+line+period / period+dims
	virtual const ibValueMetaObjectGenericData* GetSourceMetaObject() const override;   // = m_meta (body in .cpp — the metaobject type is complete there)
private:
	const ibValueMetaObjectRegisterData* m_meta;
};

// (The totals-table identity holder moved to registerQueryLowering.h, beside the rest of what the
// three registers share — it is register machinery, not part of the object family.)

//meta object with key
class BACKEND_API ibValueMetaObjectRegisterData :
	public ibValueMetaObjectGenericData, public ibBackendQueryableHolder {
	public:

	// A MANAGER, RECORD SETS AND A SELECTION, and no reference at all — a register
	// has no identity of its own to point at: its rows belong to whatever recorded
	// them. That is exactly why `InformationRegisterRef` must not exist, and why
	// the family registration skips it without anybody listing the exceptions.
	static constexpr unsigned s_features =
		ibMetaFeature_Manager | ibMetaFeature_RecordSet | ibMetaFeature_Selection;

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
	virtual bool AccessRight_Modify() const { return AccessRight_Write(); }
	virtual bool AccessRight_Erase() const { return AccessRight_Delete(); }
	// A register is a SET, addressed by its recorder — the exception to the row-controlled default:
	// its rights control the TABLE, so an empty set stays a normal state and no row count is ever
	// read as a verdict on rights. The row filter still applies: an RLS handler that refuses a
	// movement fails the INSERT, and the posting that produced it fails with it.
	virtual bool IsAccessPerRecord() const override { return false; }
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

	// ⭐⭐ WHICH ATTRIBUTE IS THIS COLUMN? — `FindAnyAttributeObjectByFilter(id)`, further down this
	// class. It exists, it is the ECONOMICAL form (one walk over the children, the id compared as it
	// goes, a clsid filter deciding what counts — no array built, nothing allocated), and it is the
	// very function `ibRegisterDataQueryable::ResolveColumnByName` answers a query's column names with.
	//
	// Said here because both registers grew a hand-written list to answer it instead — period,
	// dimensions, resources — and both lists were short: one did not know the recorder or the line
	// number, the other did not know the analytics slots. An id nobody recognised was then published as
	// a plain synthetic column, which carries no picture and does not say it holds a REFERENCE, so the
	// same field unfolded on the register one node up and refused to unfold here.

	///////////////////////////////////////////////////////////////////

#pragma region __generic_h__

	using ibValueMetaObjectCompositeData::GetGenericAttributeArrayObject;
	//attribute
	virtual std::vector<ibValueMetaObjectAttributeBase*> GetGenericAttributeArrayObject(
		std::vector<ibValueMetaObjectAttributeBase*>& array) const {
		FillArrayObjectByPredefinedAttribute(array);
		FillArrayObjectByCommonAttribute(array);
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
		FillArrayObjectByCommonAttribute(array);
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
		// The copies come along: the designer tree lists what an object HAS, and a common
		// attribute it carries is one of those — visible where it lives, and edited only
		// where it was declared.
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
		return FindObjectByFilter<ibValueMetaObjectAttributeBase>(id, { g_metaDimensionCLSID, g_metaResourceCLSID, g_metaAttributeCLSID, g_metaPredefinedAttributeCLSID, g_metaCommonAttributeColumnCLSID });
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

	// ⚠ THE TEMPLATE ARGUMENT AND THE FILTER MUST NAME THE SAME THING — they said Resource and
	// Attribute. Copied from the resource finder directly above with only the clsid edited, and nothing
	// can catch that: inside FindObjectByFilter the child is handed over by an unchecked
	// `static_cast<_T1*>`, guarded ONLY by this hand-written clsid list. It survived because
	// Resource : Attribute : AttributeBase is a single chain with ibValueMetaObject first, so the
	// adjustment happened to be zero — ABI luck, not a guarantee, and the same shape once made a
	// constant stop being a column (an attribute inherits ibBackendQueryColumn as a LATER base, where
	// the offset is not zero).
	template <typename _T1>
	ibValueMetaObjectAttributeBase* FindAttributeObjectByFilter(const _T1& id) const {
		return FindObjectByFilter<ibValueMetaObjectAttributeBase>(id, { g_metaAttributeCLSID });
	}

#pragma endregion 

	//has record manager 
	virtual bool HasRecordManager() const { return false; }

	//has recorder and period 
	virtual bool HasPeriod() const { return false; }
	virtual bool HasRecorder() const { return true; }

	ibValueRecordKeyObject* CreateRecordKeyObjectValue() const;
	// Build a record key POPULATED from a set of dimension values (a register-list row → its key).
	ibValueRecordKeyObject* CreateRecordKeyObjectValue(const ibRowMetaValues& keyValues) const;

	ibValueRecordSetObject* CreateRecordSetObjectValue(bool needInitialize = true) const;
	ibValueRecordSetObject* CreateRecordSetObjectValue(const ibUniqueKeyPair& uniqueKey, bool needInitialize = true) const;
	ibValueRecordSetObject* CreateRecordSetObjectValue(ibValueRecordSetObject* source, bool needInitialize = true) const;

	ibValueRecordSetObject* CopyRecordSetObjectValue(const ibUniqueKeyPair& uniqueKey);

	ibValueRecordManagerObject* CreateRecordManagerObjectValue() const;
	ibValueRecordManagerObject* CreateRecordManagerObjectValue(const ibUniqueKeyPair& uniqueKey) const;
	ibValueRecordManagerObject* CreateRecordManagerObjectValue(ibValueRecordManagerObject* source) const;

	ibValueRecordManagerObject* CopyRecordManagerObjectValue(const ibUniqueKeyPair& uniqueKey) const;

	// This register's OWN command ids (class-scoped). eEditValue bakes the front inline-edit flag.
	enum {
		eAddValue = 1,
		eCopyValue,
		eEditValue = 3 | eStartEditingFlag,
		eDeleteValue = 4,
	};

	// ---- Source COMMAND surface (the descriptor forwards each call here). A register is its OWN family (no shared
	// record base), so these are FRESH virtuals. GetCommandCollection lists Add/Copy/Edit/Delete only for a register
	// WITHOUT a recorder (an independent record-manager register); a recorder-based register (document movements)
	// shows none. Execute / open run through the record manager (by the row's key). Bodies in objectList.cpp.
	virtual void GetCommandCollection(const ibFormID& formType, std::vector<ibCommandItem>& commands) const;
	virtual void CallAsCommand(ibActionID id, const ibUniqueKey& anchor, const ibUniqueKey& key, ibBackendValueForm* srcForm) const;
	virtual void ShowValueByKey(const ibUniqueKey& key, ibBackendValueForm* srcForm) const;
	// SELECT value — a register has no single reference; its picker value IS the composite RECORD KEY built from
	// the row's dimension cells (the whole value map). Body in objectList.cpp.
	virtual ibValue GetSelectValue(const ibRowMetaValues& rowValues) const;
	// ITEM KEY — a register has SEVERAL key columns; its identity is the COMPOSITE record key built from the row's
	// dimension cells (CreateUniqueKeyPair). commonObjectAction.cpp.
	virtual ibUniqueKey GetItemKey(const ibRowMetaValues& rowValues) const;
	// REGISTER variant — all columns (dimensions / resources / period …) visible by default. commonObjectAction.cpp.
	virtual void FillSourceExplorer(ibSourceDataObject::ibSourceExplorer& explorer) const;

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
	virtual ibBackendValueForm* GetFormByCommandType(ibInterfaceCommandType cmdType = ibInterfaceCommandType::ibInterfaceCommandType_Default) const {

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

	// Declare the register table (dimension / resource columns + the lookup index).
	virtual void ContributeTables(ibSchemaSnapshot& out) const override;

	//load & save metaData from DB
	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

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
	ibMetaCommandDescriptor<ibRegisterDataQueryable, ibValueMetaObjectRegisterData> m_queryable{ this };

	// Read/Write/Delete role triplet emitted by IB_DECLARE_RWD_ROLE_TRIPLET
	// above (in the public access region).
};

//********************************************************************************************
//*                                      Base data                                           *
//********************************************************************************************

#include "backend/srcDataObject.h"

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

	// NOT transferable across sessions — declared once here, so every manager
	// (catalog, document, information / accumulation / accounting register)
	// inherits it.
	//
	// A manager looks like a stateless door onto a metaobject, and that is the
	// trap: it is bound to the SESSION's runtime. Its methods run in the manager
	// module compiled for that session (CompileRoot builds one per session), and
	// what it reads it reads through that session's connection and its RLS policy.
	// Handed to a job, it would either run one session's bytecode on another's
	// interpreter or read under rights that are not the job's.
	//
	// The job resolves its own manager by name on its own side — that costs
	// nothing and gets the right runtime, the right connection and the right
	// rights in one step.
	virtual bool IsTransferable() const override { return false; }

	virtual const ibValueMetaObject* GetMetaObject() const = 0;
};

class BACKEND_API ibValueManagerDataObject : public ibValueManagerObject {
	public:

	// Helper + NVI DoGetPMethods come from ibValueDynamicMembers. The surface is
	// composed from member fillers bound along the ctor chain: this base contributes
	// the manager module's methods (FillMembers surfaces them via the module's
	// descriptor, ExportMethodsToHelper); subclasses add their own.
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

	// `const ibValue&`, matching the base. Declared as a plain `ibValue&` this did not
	// override ibValue::SetPropVal at all — it HID it, so a virtual call through the base
	// (value.cpp: m_pRef->SetPropVal) never reached this class. It stayed harmless only
	// because predefined properties register as read-only (AppendProp(..., true, false)),
	// so nothing ever tried to write one. `override` is what keeps the two in step.
	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal) override;   //setting attribute
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal) override;        //attribute value
};

#pragma endregion 

//object with metaobject 
#pragma region objects 
class BACKEND_API ibValueRecordDataObject : public ibValueDynamicMembers, public ibStandardCommandSource,
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

	// NOT transferable across sessions. A loaded object (a document, a catalog
	// item) is MUTABLE state owned by the session that read it: its attributes and
	// tabular sections are being edited, and it carries a record lock and an
	// unfinished write. A second session holding the same instance would edit the
	// first one's buffer with nothing coordinating the two.
	//
	// A REFERENCE to the same row travels perfectly well — guid plus type, which
	// the target session resolves through its own metadata. That is the value to
	// pass a job: it re-reads the object on its own side, under its own rights.
	virtual bool IsTransferable() const override { return false; }

	//support actionData
	virtual ibStandardCommandSet GetStandardCommands(const ibFormID& formType) override { return ibStandardCommandSet(); }
	virtual void CallAsAction(const ibActionID& lNumAction, ibBackendValueForm* srcForm) override {}

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
	// Metadata via THIS source's metaobject (it has one here).
	virtual const ibMetaData* GetSourceMetaData() const override { const auto* mo = GetMetaObject(); return mo != nullptr ? mo->GetMetaData() : nullptr; }

	//Get ref class 
	virtual ibClassID GetSourceClassType() const final { return GetClassType(); }

	//Get presentation 
	virtual wxString GetSourceCaption() const override {
		return GetMetaObject() ? stringUtils::GenerateSynonym(GetMetaObject()->GetClassName()) + wxT(": ") + GetMetaObject()->GetSynonym() : GetString();
	}

	//support source data
	virtual const ibSourceExplorer* GetSourceExplorer() const override;

	//support source set/get data — id primitive
	virtual bool SetValueByMetaID(const ibMetaID& id, const ibValue& varMetaVal) override;
	virtual bool GetValueByMetaID(const ibMetaID& id, ibValue& pvarMetaVal) const override;

	// ibSourceDataObject hop gate — resolve the id, then honour the pinned TYPE (CoerceHopType): a composite
	// reference FIELD of this object seeds UNDEFINED, so the shared helper hands back an empty typed twin.
	virtual bool SetValueBySourceHop(const ibSourceHop& hop, const ibValue& value) override { return SetValueByMetaID(hop.m_id, value); }
	virtual bool GetValueBySourceHop(const ibSourceHop& hop, ibValue& out) const override;   // out-of-line (commonObject.cpp): filters the pin by the field's live declared type

	ibValue GetValueByMetaID(const ibMetaID& id) const {
		ibValue retValue;
		if (GetValueByMetaID(id, retValue))
			return retValue;
		return ibValue();
	}

	//support tabular section
	virtual ibValueModel* GetTableByMetaID(const ibMetaID& id) const;

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
	virtual const ibSourceExplorer* GetSourceExplorer() const;

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

	// WHICH SHAPE THIS RECORD IS. A plain reference object has only one, so it answers ITEM and
	// every field applies to it; a hierarchical one answers what it actually is. A FACT about the
	// object, not a question about attributes — what follows from it (which fields belong) is
	// ibItemModeFits, written once above.
	virtual ibObjectMode GetObjectMode() const { return ibObjectMode::OBJECT_ITEM; }

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
	// Document's (writeMode, postingMode) scaffold + register-cascade state
	// machine were PROMOTED onto ibValueRecordDataObjectRecorderRef, built on
	// these same Begin/Commit helpers (bodies in commonObject.cpp).
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

	// Mirror the row's DataVersion (held in the DataVersion attribute slot)
	// into m_loadedDataVersion. Called at the ONLY two points where the
	// in-memory marker legitimately equals the committed DB row: after a
	// successful ReadData, and after CommitWriteScope's durable commit. The
	// bump in LockAndCheckDataVersion deliberately does NOT advance the
	// marker — a rolled-back Write (RLS deny, SaveData failure, BeforeWrite/
	// OnWrite cancel) must leave it matching the unchanged row so the next
	// Save retries instead of raising a false "changed by another user".
	void CaptureLoadedDataVersion();
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
	virtual const ibSourceExplorer* GetSourceExplorer() const;

	//Get presentation
	virtual wxString GetSourceCaption() const {
		return m_metaObject->GetSynonym() + wxT(": ") + (IsNewObject() ? _("Creating") : GetString());
	}

	//get metaData from object
	virtual const ibValueMetaObjectRecordDataHierarchyMutableRef* GetMetaObject() const {
		return static_cast<const ibValueMetaObjectRecordDataHierarchyMutableRef*>(m_metaObject);
	}

	virtual ibObjectMode GetObjectMode() const override { return m_objMode; }

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
	// Document's (writeMode, postingMode) scaffold + posting state machine +
	// register cascade now live on ibValueRecordDataObjectRecorderRef, as a
	// hook-based extension of this one — the promotion is done, not pending.
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
//     a header-and-lines document) can slot in without touching the leaf
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
	// Built directly FROM a set of dimension / key values (a register-list row → its record key).
	ibValueRecordKeyObject(const ibValueMetaObjectRegisterData* metaObject, const ibRowMetaValues& keyValues);
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

class BACKEND_API ibValueRecordSetObject : public ibValueModelStorage, public ibRuntimeModuleDataObject {
	public:

	// NOT transferable across sessions. Not because it is a table — a value table
	// is one too and travels fine — but because it is the register's records:
	// keyed by m_keyValues, written back by Write(), and edited right up to that
	// moment. A second session holding this instance would be appending to a
	// buffer the first is about to write, and the later write would silently
	// decide the register's movements.
	//
	// A job that needs these takes the KEY — the recorder reference, the dimension
	// values — and reads the set on its own side.
	virtual bool IsTransferable() const override { return false; }

	virtual ibValueModelColumnCollection* GetColumnCollection() const override { return m_recordColumnCollection; }
	virtual ibValueModelReturnLine* GetRowAt(const ibDataViewItem& line) override {
		if (!line.IsOk())
			return nullptr;
		return new ibValueRecordSetObjectRegisterReturnLine(this, line);
	}

	class ibValueRecordSetObjectRegisterColumnCollection : public ibValueModel::ibValueModelColumnCollection {
	public:

		class ibValueRecordSetRegisterColumnInfo : public ibValueModel::ibValueModelColumnCollection::ibValueModelColumnInfo {
	public:

			ibValueRecordSetRegisterColumnInfo();
			ibValueRecordSetRegisterColumnInfo(ibValueMetaObjectAttributeBase* attribute);
			virtual ~ibValueRecordSetRegisterColumnInfo();

			virtual unsigned int GetColumnID() const { return m_metaAttribute->GetMetaID(); }
			virtual wxString GetColumnName() const { return m_metaAttribute->GetName(); }
			virtual wxString GetColumnCaption() const { return m_metaAttribute->GetSynonym(); }
			virtual const ibTypeDescription GetColumnType() const { return m_metaAttribute->GetTypeDesc(); }
			// The attribute tells the declaration from what a value may be (a characteristic answers with
			// its chart's list) — ask it rather than deciding here.
			virtual const ibTypeDescription GetColumnTypeValue() const { return m_metaAttribute->GetTypeValueDesc(); }

			friend ibValueRecordSetObjectRegisterColumnCollection;

		private:
			ibValueMetaObjectAttributeBase* m_metaAttribute;
		};

	public:

		ibValueRecordSetObjectRegisterColumnCollection();
		ibValueRecordSetObjectRegisterColumnCollection(ibValueRecordSetObject* ownerTable);
		virtual ~ibValueRecordSetObjectRegisterColumnCollection();

		virtual ibValueModelColumnInfo* GetColumnInfo(unsigned int idx) const override {
			if (idx >= m_listColumnInfo.size())   // `size() < idx` let idx == size() through onto end()
				return nullptr;
			auto it = m_listColumnInfo.begin();
			std::advance(it, idx);
			return it->second;
		}

		// The collection IS the count: the ctor inserts every generic attribute, keyed by its (unique)
		// metaID, with no filter. Asking the metaobject again rebuilt a vector on every call - and
		// GetColumnByID calls this once per iteration, so an id lookup allocated once per column visited.
		virtual unsigned int GetColumnCount() const override { return m_listColumnInfo.size(); }

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

		virtual ibValueModel* GetOwnerModel() const { return m_ownerTable; }

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

	//get metaData: the record set's own config via its meta object; no object -> the active config, so reference-
	//typed columns still resolve their targets. Mirrors ibValueModelTable / the object list.
	virtual const ibMetaData* GetSourceMetaData() const override {
		const ibValueMetaObjectCompositeData* mo = GetSourceMetaObject();
		return mo != nullptr ? mo->GetMetaData() : nullptr;
	}

	//Get ref class
	virtual ibClassID GetSourceClassType() const { return GetClassType(); }
#pragma endregion

	virtual bool AutoCreateColumn() const { return false; }
	virtual bool EditableLine(const ibDataViewItem& item, unsigned int col) const {
		return false;
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

	virtual bool LoadDataFromTable(ibValueModel* srcTable);
	virtual ibValueModel* SaveDataToTable() const;

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

	// Iterator runtime path lives on ibValueModel (the paged Get*Fetch cursor over RunComposerPage).
	// GetEmptyRow yields the typed skeleton that the iterator state surfaces as IntelliSense type hint.
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
	public ibSourceDataObject, public ibStandardCommandSource {
	public:
protected:
	ibValueRecordManagerObject(const ibValueMetaObjectRegisterData* metaObject, const ibUniqueKeyPair& uniqueKey);
	ibValueRecordManagerObject(const ibValueRecordManagerObject& source);
public:

	ibValueRecordSetObject* GetRecordSet() const { return m_recordSet; }

	// NOT transferable across sessions. A record manager is not a door either — it
	// OWNS a record set (m_recordSet above) that is being edited, and it holds the
	// key that set is keyed by. Passing it would hand another session a live,
	// half-written buffer; the fact that it also carries the session's runtime and
	// rights, like every other manager, is the second reason rather than the first.
	virtual bool IsTransferable() const override { return false; }

	virtual ~ibValueRecordManagerObject();

	virtual void CreateEmptyKey();

	bool InitializeObject(const ibValueRecordManagerObject* source = nullptr, bool newRecord = false);
	bool InitializeObject(const ibUniqueKeyPair& key);

	virtual bool IsNewObject() const override { return !m_recordSet->m_selected; }

	//get metaData from object
	virtual const ibValueMetaObjectGenericData* GetSourceMetaObject() const final { return GetMetaObject(); }
	// Metadata via THIS source's metaobject (it has one here).
	virtual const ibMetaData* GetSourceMetaData() const override { const auto* mo = GetMetaObject(); return mo != nullptr ? mo->GetMetaData() : nullptr; }

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

	//support source set/get data — id primitive
	virtual bool SetValueByMetaID(const ibMetaID& id, const ibValue& varMetaVal);
	virtual bool GetValueByMetaID(const ibMetaID& id, ibValue& pvarMetaVal) const;
	
	// ibSourceDataObject hop gate — resolve the id, then honour the pinned TYPE (CoerceHopType): a composite
	// reference FIELD of this object seeds UNDEFINED, so the shared helper hands back an empty typed twin.
	virtual bool SetValueBySourceHop(const ibSourceHop& hop, const ibValue& value) override { return SetValueByMetaID(hop.m_id, value); }
	virtual bool GetValueBySourceHop(const ibSourceHop& hop, ibValue& out) const override;   // out-of-line (commonObject.cpp): filters the pin by the field's live declared type

	ibValue GetValueByMetaID(const ibMetaID& id) const {
		ibValue retValue;
		if (GetValueByMetaID(id, retValue))
			return retValue;
		return ibValue();
	}

	//support source data
	virtual const ibSourceExplorer* GetSourceExplorer() const override;

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
	ibValuePtr<ibValueModel::ibValueModelReturnLine> m_recordLine;
};
#pragma endregion


#endif
