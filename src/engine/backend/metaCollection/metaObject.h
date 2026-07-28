#ifndef __META_OBJECT_H__
#define __META_OBJECT_H__

#include "backend/propertyManager/propertyManager.h"

#include "backend/backend_metatree.h"
#include "backend/metaCtor.h"

#include "backend/restructureInfo.h"

#include "backend/interfaceHelper.h"
#include "backend/roleHelper.h"

//*******************************************************************************
class BACKEND_API ibMetaData;
class BACKEND_API ibDataNode;   // serialize/dataBuilder.h — universal structure node
class BACKEND_API ibDataValue;  // serialize/dataBuilder.h — a node value (Child for a nested object)
//*******************************************************************************
//*                          define commom clsid                                *
//*******************************************************************************

//COMMON METADATA
constexpr ibClassID g_metaCommonMetadataCLSID = metadata_to_clsid("MD_MTD");

//COMMON OBJECTS
constexpr ibClassID g_metaCommonModuleCLSID = metadata_to_clsid("MD_CMOD");
constexpr ibClassID g_metaCommonFormCLSID = metadata_to_clsid("MD_CFRM");
constexpr ibClassID g_metaCommonTemplateCLSID = metadata_to_clsid("MD_CTMP");
constexpr ibClassID g_metaCommonCommandCLSID = metadata_to_clsid("MD_CMD");    // COMMON command (config-level, like CommonForm)

constexpr ibClassID g_metaRoleCLSID = metadata_to_clsid("MD_ROLE");
constexpr ibClassID g_metaSectionCLSID = metadata_to_clsid("MD_SSYST");
constexpr ibClassID g_metaPictureCLSID = metadata_to_clsid("MD_PICTR");
constexpr ibClassID g_metaLanguageCLSID = metadata_to_clsid("MD_LANG");

//ADVANCED OBJECTS
constexpr ibClassID g_metaAttributeCLSID = metadata_to_clsid("MD_ATTR");
constexpr ibClassID g_metaFormCLSID = metadata_to_clsid("MD_FRM");
constexpr ibClassID g_metaTemplateCLSID = metadata_to_clsid("MD_TMPL");
constexpr ibClassID g_metaCommandCLSID       = metadata_to_clsid("MD_OCMD");   // OBJECT command (under a business object, like Form)
constexpr ibClassID g_metaModuleCLSID = metadata_to_clsid("MD_MOD");
constexpr ibClassID g_metaManagerCLSID = metadata_to_clsid("MD_MNGR");
constexpr ibClassID g_metaTableCLSID = metadata_to_clsid("MD_TBL");
constexpr ibClassID g_metaTableRefCLSID = metadata_to_clsid("MD_TBLR");   // DB-backed tabular section (reference owner); MD_TBL stays RAM-only (processors/reports)
constexpr ibClassID g_metaSubcontoKindsTableCLSID = metadata_to_clsid("MD_SKTB");
constexpr ibClassID g_metaEnumCLSID = metadata_to_clsid("MD_ENUM");
constexpr ibClassID g_metaDimensionCLSID = metadata_to_clsid("MD_DMNT");
constexpr ibClassID g_metaResourceCLSID = metadata_to_clsid("MD_RESS");

//SPECIAL OBJECTS
constexpr ibClassID g_metaPredefinedAttributeCLSID = metadata_to_clsid("MD_DATT");

//MAIN OBJECTS
constexpr ibClassID g_metaConstantCLSID = metadata_to_clsid("MD_CONS");
constexpr ibClassID g_metaCatalogCLSID = metadata_to_clsid("MD_CAT");
constexpr ibClassID g_metaDocumentCLSID = metadata_to_clsid("MD_DOC");
constexpr ibClassID g_metaEnumerationCLSID = metadata_to_clsid("MD_ENM");
constexpr ibClassID g_metaDataProcessorCLSID = metadata_to_clsid("MD_DPR");
constexpr ibClassID g_metaReportCLSID = metadata_to_clsid("MD_RPT");
constexpr ibClassID g_metaInformationRegisterCLSID = metadata_to_clsid("MD_INFR");
constexpr ibClassID g_metaAccumulationRegisterCLSID = metadata_to_clsid("MD_ACCR");

//ACCOUNTING OBJECTS
constexpr ibClassID g_metaChartOfCharacteristicTypesCLSID = metadata_to_clsid("MD_CHRC");
constexpr ibClassID g_metaChartOfAccountsCLSID = metadata_to_clsid("MD_CHOA");
constexpr ibClassID g_metaAccountingRegisterCLSID = metadata_to_clsid("MD_AREG");

// EXTERNAL
constexpr ibClassID g_metaExternalDataProcessorCLSID = metadata_to_clsid("MD_EDPR");
constexpr ibClassID g_metaExternalReportCLSID = metadata_to_clsid("MD_ERPT");

//*******************************************************************************
//*                             ibValueMetaObject                                *
//*******************************************************************************

#define defaultMetaID 1000

//flags metaobject event 
enum metaObjectFlags {

	defaultFlag = 0x0000,
	onlyLoadFlag = 0x0001,

	loadConfigFlag = 0x0002,
	saveConfigFlag = 0x0004,

	newObjectFlag = 0x0008,
	forceRunFlag = 0x0010,
	forceCloseFlag = 0x0020,

	loadFileFlag = 0x0040,
	saveToFileFlag = 0x0080,

	copyObjectFlag = 0x0100,
	pasteObjectFlag = 0x0200,
};

// (rt_ref_chunk — the reference-blob wire chunk id — moved to its only user, the L3-3 wire codec
//  in query/dataMover.cpp.)

//flags metaobject
#define metaDeletedFlag 0x0001000
#define metaCanSaveFlag 0x0002000
#define metaDisableFlag 0x0008000
// Predefined child: created in the owner's ctor via CreateMetaObjectAndSetParent
// (predefined attributes, inner modules). Such a child is bound to its parent for
// life - an in-place reset of the parent (RemoveAllChildren on a reused root:
// configuration / external report / data processor) must NOT drop it, only the
// parent's actual destruction does. Not serialized; re-set every construction.
#define metaPredefinedFlag 0x0010000

#define metaDefaultFlag metaCanSaveFlag

class ibSchemaSnapshot;   // structure snapshot — ContributeTables declares this object's tables into it

class BACKEND_API ibValueMetaObject :

	public ibValueDynamicMembers,

	public ibPropertyObjectHelper<ibValueMetaObject>,
	public ibAccessObject, public ibInterfaceObject {
	public:


public:

	// get object name as string 
	bool GetObjectNameAsString(wxString& result) const {
		return m_propertyName->GetValueAsString(result);
	}

	//system attributes
	ibMetaID GetMetaID() const { return m_metaId; }
	void SetMetaID(const ibMetaID& id) { m_metaId = id; }

	wxString GetName() const { return m_propertyName->GetValueAsString(); }
	void SetName(const wxString& strName) { m_propertyName->SetValue(strName); }

	// Typed parent — the parent metaobject cast to parentType through CastValue (the value cast):
	// a dynamic_cast that, with _USE_CONTROL_VALUECAST, RAISES immediately (ThrowErrorTypeOperation)
	// on a wrong / absent parent instead of returning null. A child writes
	// GetParentAsType<ibValueMetaObjectRecordData>() with no dynamic_cast + assert + null-check.
	// Distinct name (not GetParent) — it doesn't hide the inherited plain GetParent(), so no
	// using-declaration is needed. Parallels ibValue::ConvertToType<T>().
	template <typename parentType>
	parentType* GetParentAsType() const {
		return CastValue<parentType>(GetParent());
	}

	virtual wxString GetSynonym() const {
		return !m_propertySynonym->IsEmptyProperty() ?
			m_propertySynonym->GetValueAsTranslateString() : stringUtils::GenerateSynonym(GetName());
	}
	virtual void SetSynonym(const wxString& synonym) { m_propertySynonym->SetValue(synonym); }

	wxString GetComment() const { return m_propertyComment->GetValueAsString(); }
	void SetComment(const wxString& comment) { m_propertyComment->SetValue(comment); }

	wxString GetHelpContent() const { return m_strHelpContent; }
	void SetHelpContent(const wxString& strHelpContent) { m_strHelpContent = strHelpContent; }

	virtual void SetMetaData(ibMetaData* metaData) { m_metaData = metaData; }
	virtual const ibMetaData* GetMetaData() const override { return m_metaData; }
	// Mutable accessor - metaobjects own a mutable m_metaData. Not an override:
	// the factory root only declares the const capability (see backend_type.h).
	virtual ibMetaData* GetMetaData() { return m_metaData; }

	// Restructure-ledger facade — one short call instead of the static ledger accessor at every save /
	// validation site. STATIC (the ledger pulls the active config), so it works in this-less contexts too
	// (the static scaffold methods). Defined in metaObject.cpp.
	static void RestructureInfo   (const wxString& message);
	static void RestructureWarning(const wxString& message);
	static void RestructureError  (const wxString& message);

	void ResetGuid();
	void ResetId();

	void ResetAll() {
		ResetGuid(); ResetId();
	}

	void GenerateGuid() {
		wxASSERT(!m_metaGuid.isValid());
		if (!m_metaGuid.isValid()) {
			ResetGuid();
		}
	}

	inline bool CompareId(const ibMetaID& id) const { return m_metaId == id; }
	inline bool CompareGuid(const ibGuid& guid) const { return m_metaGuid == guid; }

	operator ibMetaID() const { return m_metaId; }

	ibBackendMetadataTree* GetMetaDataTree() const;

public:

	bool IsAllowed() const {
		return IsEnabled()
			&& !IsDeleted();
	}

	bool IsEnabled() const {
		return (m_metaFlags & metaDisableFlag) == 0;
	}

	bool IsDeleted() const {
		return (m_metaFlags & metaDeletedFlag) != 0;
	}

public:

	void MarkAsDeleted() {
		m_metaFlags |= metaDeletedFlag;
	}

public:

	void SetFlag(int flag) {
		m_metaFlags |= flag;
	}

	void ClearFlag(int flag) {
		m_metaFlags &= ~(flag);
	}

public:

	bool BuildNewName();

	ibValueMetaObject(
		const wxString& strName = wxEmptyString,
		const wxString& synonym = wxEmptyString,
		const wxString& comment = wxEmptyString
	);

	virtual ~ibValueMetaObject();

	//system override 
	virtual int GetComponentType() const final { return COMPONENT_TYPE_METADATA; }

	virtual wxString GetClassName() const final { return ibValue::GetClassName(); }
	virtual wxString GetObjectTypeName() const final { return ibValue::GetClassName(); }

	ibGuid GetGuid() const { return m_metaGuid; }

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	bool IsCopyMode() const {
		return m_metaCopyGuid.isValid();
	}

	bool IsPasteMode() const {
		return m_metaPasteGuid.isValid();
	}

	void SetCommonGuid(const ibGuid& guid) {
		m_metaCopyGuid.reset(); m_metaPasteGuid.reset(); m_metaGuid = guid;
	}

	ibGuid GetCommonGuid() const {

		if (m_metaPasteGuid.isValid())
			return m_metaPasteGuid;

		if (m_metaCopyGuid.isValid())
			return m_metaCopyGuid;

		return m_metaGuid;
	}

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	void SetCopyGuid(const ibGuid& guid) const { m_metaCopyGuid = guid; }

	// Re-arm / disarm the paste mark. A pasted OBJECT form materialises LAZILY (ibDeferredForm) — long after
	// PasteObject cleared the guard — so the deferred build re-stamps the SAME guid here for the duration of the
	// load, and clears it right after; LoadControl then sees IsPasteMode and routes the controls to PasteNode.
	void SetPasteGuid(const ibGuid& guid) const { m_metaPasteGuid = guid; }

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	wxString GetFileName() const;
	wxString GetFullName() const;

	wxString GetModuleName() const;
	wxString GetDocPath() const { return m_metaGuid.str(); }

	// Resolve a child's clsid against THIS owner: returns the canonical clsid the owner hosts
	// (possibly remapped) or 0 if it does not host this child. ibClassID is UNSIGNED, so 0 — not
	// -1 — is the "not allowed" sentinel (a real clsid is always > 0, see CreateMetaObject's
	// wxASSERT(clsid != 0)). This is the single "may I host this child, and as what" gate — it
	// replaces the old bool FilterChild + a separate remap. A tabular section comes in two clsids
	// of the same kind: RAM (MD_TBL, processors/reports) and DB-backed reference (MD_TBLR,
	// catalogs/documents); each owner maps either input to ITS variant, so a tabular section
	// copy/pastes across owner kinds. CreateMetaObject builds the RETURNED clsid. Owners override.
	virtual ibClassID ResolveChild(const ibClassID& clsid) const { return 0; }

	// Bool view of ResolveChild for the many filter-only call sites (copy/serialize walkers):
	// a resolved clsid is > 0; 0 means the child is not allowed.
	bool FilterChild(const ibClassID& clsid) const { return ResolveChild(clsid) > 0; }

	//process choice
	virtual bool ProcessChoice(ibBackendControlFrame* ownerValue,
		const wxString& strFormName, enum ibSelectMode selMode) const {
		return true;
	}

	//methods
	// DoGetPMethods (protected) + by-value m_members come from ibValueDynamicMembers.
	void FillMembers(ibMemberTable& helper) const;   // bound in ctor (was PrepareNames)

	//attributes
	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal) override;        //setting attribute
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal) override;                   //attribute value

	//support icons
	virtual wxIcon GetIcon() const override { return wxNullIcon; }
	static wxIcon GetIconGroup() { return wxNullIcon; }

	// Recursive tree (de)serialization into/from the universal ibDataNode structure
	// (serialize/dataBuilder.h). The ibMetaData containers drive the whole tree through
	// the top-level ibDataBuilder + ibBinaryProvider; this node owns only the
	// metaobject<->node mapping (each node carries its own m_metaData).
	//   ApplyDataNode - factory-create children by clsid + LoadNode(this) +
	//                   OnLoadMetaObject; `resetId` regenerates each metaId (grafting a
	//                   file subtree into a config). Throws ibBackendException on bad data.
	//   BuildDataNode - SaveNode(this) + OnSaveMetaObject + children recursed.
	//   DeleteSubtree - purge IsDeleted descendants (detach).
	bool ApplyDataNode(const ibDataNode& node, bool resetId = false);
	bool BuildDataNode(ibDataNode& node, int flags = defaultFlag);
	bool DeleteSubtree();

	// Node form, SEPARATE per direction (no flag): LoadNode reads a genuinely CONST node
	// into the object; SaveNode writes the object into the node. Both handle the common
	// header (guid/id/deleted/help -> fields, name/synonym/comment -> props, interface/
	// roles) then delegate the per-type data to ReadData / WriteData.
	bool LoadNode(const ibDataNode& node);
	bool SaveNode(ibDataNode& node) const;

	// A NESTED metaobject (module, predefined attribute, …) is embedded by its holder
	// PROPERTY like any value — m_propertyObjectModule->WriteNodeValue/ReadNodeValue
	// yields/consumes a Child sub-node that wraps this object's SaveNode/LoadNode.

public:

	// Runtime lifecycle walk over THIS node + descendants. Deleted nodes (and their
	// subtree) are skipped. The walk fires the hook on the node itself: RunSubtree is
	// top-down (self before children), CloseSubtree is bottom-up (children before self,
	// so the root closes last) — a caller just drives the two phases on the root, no
	// separate root-hook firing.
	// Lifecycle phase for RunSubtree / CloseSubtree — TWO passes over the tree.
	//   Run  : Before = register (identity), After = resolve (cross-refs, sources / forms).
	//   Close: Before = un-resolve, After = un-register (LIFO mirror of Run).
	enum class ibRunPhase : unsigned char { Before, After };
	bool RunSubtree(int flags, ibRunPhase phase);
	bool CloseSubtree(ibRunPhase phase);

	// (CreateMetaTable / UpdateMetaTable / DeleteMetaTable removed — structure DDL is the config-save
	//  differ's job; see ContributeTables below (declares both structure and seed) + query/schemaSnapshot.h.)

	// (No per-object table-data dump / restore. Row data is moved off the config's ContributeTables
	//  SNAPSHOT directly by the orchestrator (DumpDataToBuffer / RestoreDataFromBuffer) through the L3-3
	//  mover — one source of truth, the same structure that drives the DDL. See query/dataMover.h.)

	//events:
	virtual bool OnCreateMetaObject(ibMetaData* metaData, int flags);
	virtual bool OnLoadMetaObject(ibMetaData* metaData);
	virtual bool OnSaveMetaObject(int flags) { return true; }
	virtual bool OnDeleteMetaObject();
	virtual bool OnRenameMetaObject(const wxString& sNewName) { return true; }

	//for designer 
	virtual bool OnReloadMetaObject() { return true; }

	//module manager lifecycle — TWO run phases (see ibRunPhase), driven by RunSubtree:
	//  OnBeforeRun = register (identity / type ctor),
	//  OnAfterRun  = resolve (cross-refs, sources / forms — all identities now present).
	virtual bool OnBeforeRunMetaObject(int flags) { return true; }
	virtual bool OnAfterRunMetaObject(int flags) { return true; }

	// close mirrors run in reverse (LIFO): OnBeforeClose = un-resolve, OnAfterClose = un-register.
	virtual bool OnBeforeCloseMetaObject() { return true; }
	virtual bool OnAfterCloseMetaObject();

	//prepare menu for item
	virtual bool PrepareContextMenu(wxMenu* defaultMenu) { return false; }
	virtual void ProcessCommand(unsigned int id) {}

	//check is empty
	virtual bool IsEmpty() const override { return false; }

	virtual bool Init() final override;
	virtual bool Init(ibValue** paParams, const long lSizeArray) final override;

	//Is editable object? 
	virtual bool IsEditable() const override;

	//compare object 
	virtual bool CompareObject(const ibValueMetaObject* metaObject) const;

	/**
	* Property events
	*/
	virtual void OnPropertyCreated(ibProperty* property) override;
	virtual void OnPropertySelected(ibProperty* property) override;
	virtual bool OnPropertyChanging(ibProperty* property, const wxVariant& newValue) override;
	virtual void OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue) override;

	/**
	* Devuelve la posicion del hijo o GetChildCount() en caso de no encontrarlo
	*/
	bool ChangeChildPosition(ibValueMetaObject* obj, unsigned int pos);

	//copy & paste object 
	bool CopyObject(ibWriterMemory& writer) const;
	bool PasteObject(ibReaderMemory& reader);

#pragma region __array_h__

	//any
	template <typename _T1 = ibValueMetaObject>
	std::vector<_T1*> GetAnyArrayObject(
		std::vector<_T1*> array = std::vector<_T1*>()) const {
		FillArrayObjectByFilter<_T1>(array, {});
		return array;
	}

#pragma endregion
#pragma region __filter_h__

	//any
	template <typename _T1 = ibValueMetaObject, typename _T2>
	_T1* FindAnyObjectByFilter(const _T2& id) const {
		return FindObjectByFilter<_T1>(id, {});
	}

#pragma endregion

	template<typename T, typename... Args>
	T* CreateMetaObjectAndSetParent(Args&&... args) {
		T* createdObject = ibValue::CreateAndConvertObjectValueRef<T>(args...);
		wxASSERT(createdObject);
		//set child/parent - predefined child, pinned to this parent for life
		createdObject->SetParent(this);
		createdObject->SetFlag(metaPredefinedFlag);
		this->AddChild(createdObject);
		return createdObject;
	}

	// Predefined children (set in CreateMetaObjectAndSetParent) are bound to the
	// parent's lifetime: a reload reset of the parent keeps them, only the
	// parent's destruction drops them. See RemoveAllChildren(keepPinned).
	virtual bool IsPinnedToParent() const override {
		return (m_metaFlags & metaPredefinedFlag) != 0;
	}

public:

	// Declare this object's physical tables INTO a structure snapshot — the declarative replacement for
	// CreateAndUpdateTableDB. The differ (DiffSnapshots) computes create/alter/drop from two snapshots;
	// an object only declares "what I am now". (query/schemaSnapshot.h, docs/query-language-arc.md)
	//
	// Base = the CONTAINER behaviour: recurse into children, so the tree walks itself (SnapshotOf is one
	// call on the common object). A TABLE-bearing object overrides to Add its table(s) — including nested
	// tabular sections — and does NOT recurse (its children are attributes/forms, not tables). A non-table
	// container (folder / common) keeps this default and just descends.
	virtual void ContributeTables(ibSchemaSnapshot& out) const {
		for (unsigned int i = 0; i < GetChildCount(); i++)
			if (ibValueMetaObject* child = GetChild(i))
				child->ContributeTables(out);
	}


protected:

	// per-type data hook: a type reads/writes its OWN data — props / fields / Child.
	// The base has none; a type overrides. Driven only by SaveNode / LoadNode.
	virtual bool ReadData(const ibDataNode& node);
	virtual bool WriteData(ibDataNode& node) const;

protected:

#pragma region interface_h
	virtual void DoSetInterface(const ibMetaID& id, const bool& val = true) override;
#pragma endregion

#pragma region role_h
	virtual void DoSetRight(const ibRole* role, const bool& val = true) override;
#pragma endregion

	//Check is full access 
	virtual bool IsFullAccess() const override;

	//Create user info
	virtual ibRoleUserInfo GetUserRoleInfo() const override;

#pragma region __array_h__

	template <typename _T1>
	bool FillArrayObjectByFilter(
		std::vector<_T1*>& array,
		std::initializer_list<ibClassID> filter,
		const bool use_child_filter = false) const
	{
		for (ibValueMetaObject* child : m_children) {

			if (!child->IsAllowed())
				continue;

			if (filter.size() > 0) {
				bool success = false;
				ibClassID child_clsid = child->GetClassType();
				for (const auto filter_clsid : filter) {
					if (child_clsid == filter_clsid) {
						success = true;
						break;
					}
				}

				if (success)
					array.emplace_back(static_cast<_T1*>(child));
			}
			else {
				_T1* ptr = dynamic_cast<_T1*>(child);
				if (ptr != nullptr) array.emplace_back(ptr);
			}

			if (use_child_filter)
				child->FillArrayObjectByFilter<_T1>(array, filter, true);
		}

		return array.size() > 0;
	}

#pragma endregion 
#pragma region __filter_h__

	template<typename _T1>
	_T1* FindObjectByFilter(const wxString& name,
		const std::initializer_list<ibClassID> filter,
		const bool use_child_filter = false) const
	{
		if (name.IsEmpty())
			return nullptr;

		for (ibValueMetaObject* child : m_children) {

			if (child->IsDeleted())
				continue;

			if (stringUtils::CompareString(name, child->GetName())) {

				if (filter.size() > 0) {

					bool success = false;
					ibClassID child_clsid = child->GetClassType();
					for (const auto filter_clsid : filter) {
						if (child_clsid == filter_clsid) {
							success = true;
							break;
						}
					}

					return success ?
						static_cast<_T1*>(child) : nullptr;
				}

				return dynamic_cast<_T1*>(child);
			}

			if (use_child_filter) {
				_T1* founded = child->FindObjectByFilter<_T1>(name, filter, true);
				if (founded != nullptr)
					return founded;
			}
		}

		//self
		if (stringUtils::CompareString(name, GetName()))
			return dynamic_cast<_T1*>(const_cast<ibValueMetaObject*>(this));

		return nullptr;
	}

	template<typename _T1>
	_T1* FindObjectByFilter(const ibMetaID& id,
		const std::initializer_list<ibClassID> filter,
		const bool use_child_filter = false) const
	{
		if (id <= 0)
			return nullptr;

		for (ibValueMetaObject* child : m_children) {

			if (child->IsDeleted())
				continue;

			if (child->CompareId(id)) {

				if (filter.size() > 0) {

					bool success = false;
					ibClassID child_clsid = child->GetClassType();
					for (const auto filter_clsid : filter) {
						if (child_clsid == filter_clsid) {
							success = true;
							break;
						}
					}

					return success ?
						static_cast<_T1*>(child) : nullptr;
				}

				return dynamic_cast<_T1*>(child);
			}

			if (use_child_filter) {
				_T1* founded = child->FindObjectByFilter<_T1>(id, filter, true);
				if (founded != nullptr)
					return founded;
			}
		}

		//self
		if (CompareId(id))
			return dynamic_cast<_T1*>(const_cast<ibValueMetaObject*>(this));

		return nullptr;
	}

	template<typename _T1>
	_T1* FindObjectByFilter(const ibGuid& id,
		const std::initializer_list<ibClassID> filter,
		const bool use_child_filter = false) const
	{
		if (!id.isValid())
			return nullptr;

		for (ibValueMetaObject* child : m_children) {

			if (child->IsDeleted())
				continue;

			if (child->CompareGuid(id)) {

				if (filter.size() > 0) {

					bool success = false;
					ibClassID child_clsid = child->GetClassType();
					for (const auto filter_clsid : filter) {
						if (child_clsid == filter_clsid) {
							success = true;
							break;
						}
					}

					return success ?
						static_cast<_T1*>(child) : nullptr;
				}

				return dynamic_cast<_T1*>(child);
			}

			if (use_child_filter) {
				_T1* founded = child->FindObjectByFilter<_T1>(id, filter, true);
				if (founded != nullptr)
					return founded;
			}
		}

		//self
		if (CompareGuid(id))
			return dynamic_cast<_T1*>(const_cast<ibValueMetaObject*>(this));

		return nullptr;
	}

#pragma endregion 

protected:

	friend class ibMetaData;

	mutable ibGuid m_metaCopyGuid, m_metaPasteGuid;

	int m_metaFlags;
	ibMetaID m_metaId;			//type id (default is undefined)
	ibGuid m_metaGuid;

	ibMetaData* m_metaData;

	wxString m_strHelpContent;

protected:

	ibPropertyCategory* m_categoryCommon = ibPropertyObject::CreatePropertyCategory(wxT("Common"), _("Common"));
	ibPropertyUString* m_propertyName = ibPropertyObject::CreateProperty<ibPropertyUString>(m_categoryCommon, wxT("Name"), _("Name"), _("Name of metadata object"), wxEmptyString);
	ibPropertyTString* m_propertySynonym = ibPropertyObject::CreateProperty<ibPropertyTString>(m_categoryCommon, wxT("Synonym"), _("Synonym"), _("Synonym of metadata object"), wxEmptyString);
	ibPropertyString* m_propertyComment = ibPropertyObject::CreateProperty<ibPropertyString>(m_categoryCommon, wxT("Comment"), _("Comment"), _("Comment"), wxEmptyString);
	ibPropertyCategory* m_categoryContext = ibPropertyObject::CreatePropertyCategory(wxT("Context"), _("Context"));
};

#endif
