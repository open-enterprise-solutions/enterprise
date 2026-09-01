#ifndef __META_OBJECT_H__
#define __META_OBJECT_H__

#include "backend/propertyManager/propertyManager.h"

#include <vector>   // ibMetaMenuItem arrives as a vector of these — see CollectContextMenu

#include "backend/backend_form.h"
#include "backend/metaCtor.h"

#include "backend/restructureInfo.h"

#include "backend/interfaceHelper.h"
#include "backend/compositionHelper.h"
#include "backend/roleHelper.h"

#include "backend/metaCollection/metaObjectEnum.h"   // ibSelectMode — ProcessChoice takes it

//*******************************************************************************
class BACKEND_API ibMetaData;
class BACKEND_API ibValueMetaObject;
class BACKEND_API ibDataNode;   // serialize/dataBuilder.h — universal structure node
class BACKEND_API ibDataValue;  // serialize/dataBuilder.h — a node value (Child for a nested object)
//*******************************************************************************

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
constexpr ibClassID g_metaScheduledJobCLSID = metadata_to_clsid("MD_SJOB");   // PREDEFINED scheduled job — serves the configuration, one of it (docs/scheduled-jobs.md)
// A SESSION PARAMETER — an attribute whose owner is the session rather than a table. Declared
// here beside the jobs because that is where it sits in the tree: configuration-level, no data
// of its own, set once per session by the session module (docs/access-policy-rls.md).
constexpr ibClassID g_metaSessionParameterCLSID = metadata_to_clsid("MD_SPRM");

constexpr ibClassID g_metaRoleCLSID = metadata_to_clsid("MD_ROLE");
constexpr ibClassID g_metaSectionCLSID = metadata_to_clsid("MD_SSYST");
constexpr ibClassID g_metaPictureCLSID = metadata_to_clsid("MD_PICTR");
constexpr ibClassID g_metaLanguageCLSID = metadata_to_clsid("MD_LANG");

//ADVANCED OBJECTS
constexpr ibClassID g_metaAttributeCLSID = metadata_to_clsid("MD_ATTR");
constexpr ibClassID g_metaFormCLSID = metadata_to_clsid("MD_FRM");
constexpr ibClassID g_metaTemplateCLSID = metadata_to_clsid("MD_TMPL");
constexpr ibClassID g_metaCommandCLSID       = metadata_to_clsid("MD_OCMD");   // OBJECT command (under a business object, like Form)
// A COMPOSER — what a report READS and how it is LAID OUT, declared inside the report the way a
// form, a template or a tabular section is. It is not an attribute somebody adds by hand: it lives
// in the object, so declaring it is what makes `Object.<Name>` exist (Max, 2026-08-20). The report's
// DEFAULT composer is what the generated form is built from — a report with one needs no form at all.
constexpr ibClassID g_metaComposerCLSID      = metadata_to_clsid("MD_CMPS");
constexpr ibClassID g_metaModuleCLSID = metadata_to_clsid("MD_MOD");
constexpr ibClassID g_metaManagerCLSID = metadata_to_clsid("MD_MNGR");
constexpr ibClassID g_metaTableCLSID = metadata_to_clsid("MD_TBL");
constexpr ibClassID g_metaTableRefCLSID = metadata_to_clsid("MD_TBLR");   // DB-backed tabular section (reference owner); MD_TBL stays RAM-only (processors/reports)
// The KEY stays "MD_SKTB" deliberately: it is an opaque body key that stored configurations and
// DB rows already carry, not a name. Renaming it would change every id derived from it.
constexpr ibClassID g_metaAccountDimensionKindsTableCLSID = metadata_to_clsid("MD_SKTB");

// EVERY id whose class derives from ibValueMetaObjectTableData — the RAM and DB-backed variants plus
// each PREDEFINED section registered under an id of its own. The metadata walks filter tabular
// sections by THIS list and then static_cast, because that walk runs per child of every traversal and
// a type test there would be RTTI on a hot path. The price is that a new predefined section has to be
// named here — so it is named ONCE, and not spelled out at each call site (missing it is invisible:
// the section simply stops being a table — no physical table, no node on the form, a null queryable
// at write time, which is how the analytics-kinds table crashed the save).
//
// A constant and not a macro: it sits among the constexpr ids it is made of, it has a type, and it
// cannot be redefined out from under a translation unit. It IS the walks' parameter type, so every
// call site keeps its braces and this one passes by name — the backing array of a namespace-scope
// initializer_list has static storage duration, so there is nothing to outlive.
inline constexpr std::initializer_list<ibClassID> g_tabularSectionCLSIDs = {
	g_metaTableCLSID, g_metaTableRefCLSID, g_metaAccountDimensionKindsTableCLSID
};
constexpr ibClassID g_metaEnumCLSID = metadata_to_clsid("MD_ENUM");
constexpr ibClassID g_metaDimensionCLSID = metadata_to_clsid("MD_DMNT");
constexpr ibClassID g_metaResourceCLSID = metadata_to_clsid("MD_RESS");

//SPECIAL OBJECTS
constexpr ibClassID g_metaPredefinedAttributeCLSID = metadata_to_clsid("MD_DATT");

// A COMMON ATTRIBUTE — declared once under Common, carried by many objects. Two ids, because
// there are two things: the DECLARATION a person writes, and the COPY it puts inside every
// object that is checked into its composition. The copy is a real child with its own metaID —
// that is what gives it a column of its own (fld<metaId>) and takes it through restructuring
// with its owner, exactly as any attribute goes.
constexpr ibClassID g_metaCommonAttributeCLSID = metadata_to_clsid("MD_CATT");
constexpr ibClassID g_metaCommonAttributeColumnCLSID = metadata_to_clsid("MD_CATC");

//MAIN OBJECTS
constexpr ibClassID g_metaConstantCLSID = metadata_to_clsid("MD_CONS");
constexpr ibClassID g_metaCatalogCLSID = metadata_to_clsid("MD_CAT");
constexpr ibClassID g_metaDocumentCLSID = metadata_to_clsid("MD_DOC");
constexpr ibClassID g_metaEnumerationCLSID = metadata_to_clsid("MD_ENM");
constexpr ibClassID g_metaDataProcessorCLSID = metadata_to_clsid("MD_DPR");
constexpr ibClassID g_metaReportCLSID = metadata_to_clsid("MD_RPT");
constexpr ibClassID g_metaInformationRegisterCLSID = metadata_to_clsid("MD_INFR");
constexpr ibClassID g_metaAccumulationRegisterCLSID = metadata_to_clsid("MD_ACCR");
// PARAMETERIZED scheduled job — serves the DATA: a reference object whose ROWS are its instances,
// beside the predefined kind above (docs/scheduled-jobs.md § 3). A main-branch object, not a
// common one, precisely because it has a table, a reference and a card.
constexpr ibClassID g_metaParameterizedJobCLSID = metadata_to_clsid("MD_PJOB");

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

// WHAT KIND OF THING AN ITEM OPENS — and the LINE between groups follows from it.
//
// ⭐ Max, 2026-09-01: *"the argument is a vector of these, separated by kind."* The metaobject used
// to append a separator itself — `defaultMenu->AppendSeparator()` — which is a drawing instruction
// living in the backend: it said WHERE TO PUT A LINE. It says what its entries ARE now, and whoever
// draws puts a line wherever the kind changes.
//
// ⚠ AND THAT REPRODUCES THE OLD MENUS EXACTLY. Every internal separator in the twenty-two menus this
// replaced fell on a kind change and nowhere else — modules, then the object, then a modal editor.
// The trailing one was never about the items at all: it was the boundary to the tree's own
// New / Edit / Remove block, which is the tree's to draw.
enum class ibMetaMenuKind {
	Module,   // a module — the code editors group together
	Object,   // a metaobject, this one or another
	Editor,   // a modal editor over this object, with no metaobject of its own
};

// ONE THING A METAOBJECT OFFERS TO OPEN — see ibValueMetaObject::CollectContextMenu.
//
// Max, 2026-09-01, naming the unit: *"name, caption, id, metaobject."*
struct ibMetaMenuItem {

	ibMetaMenuKind     m_kind = ibMetaMenuKind::Object;

	// ⭐ A NAME IS NOT A CAPTION, and both are here for the same reason they are on a select field
	// (ibSelectDescription): the NAME is what a script or the assistant addresses — stable, English,
	// never translated — and the CAPTION is what a person reads, which is translated and may be
	// reworded any day. One field for both means either the assistant addresses a translation or the
	// person reads an identifier.
	wxString           m_name;                   // "ObjectModule"
	wxString           m_caption;                // "Open object module"

	// The remainder — see ProcessCommand. An item with an id and no metaobject stands for one of the
	// two modal editors that have no metaobject to name. wxNOT_FOUND otherwise.
	int                m_id = wxNOT_FOUND;

	ibValueMetaObject* m_metaObject = nullptr;   // what "open" means here

	// ⭐ THE PICTURE IS ASKED OF THE METAOBJECT, OR GIVEN BY HAND (Max, 2026-09-01). An item that
	// names one has nothing to state — GetIcon() is right there and cannot go stale; an item that
	// names none says which picture it wants, so an icon is never lost for want of somewhere to put
	// it. Empty means "ask the metaobject", which is the ordinary case.
	ibClassID          m_picture = 0;

	ibMetaMenuItem() = default;

	// The id is optional here and required on the editor form below: an item that names a
	// metaobject is opened BY the caller and needs no number, while one that stands for a modal
	// editor has nothing but its number. It is given anyway where a caller has to pick ONE item
	// out of the list — a toolbar button with a single meaning — because matching the name would
	// be matching a string.
	ibMetaMenuItem(ibMetaMenuKind kind, const wxString& name, const wxString& caption,
		ibValueMetaObject* metaObject, const ibClassID& picture = 0, int id = wxNOT_FOUND)
		: m_kind(kind), m_name(name), m_caption(caption), m_id(id), m_metaObject(metaObject), m_picture(picture) {}
	ibMetaMenuItem(const wxString& name, const wxString& caption, int id, const ibClassID& picture = 0)
		: m_kind(ibMetaMenuKind::Editor), m_name(name), m_caption(caption), m_id(id), m_picture(picture) {}
};

class BACKEND_API ibValueMetaObject :

	public ibValueDynamicMembers,

	public ibPropertyObjectHelper<ibValueMetaObject>,
	public ibAccessObject, public ibInterfaceObject, public ibCompositionObject {
	public:

	// WHAT THIS METATYPE HAS — one set of flags, declared by the class itself.
	//
	// A catalog has references, objects and a manager; a register has a manager
	// and record sets; an enumeration has references and a manager and no object
	// at all. Each of those used to be (or would have become) its own boolean
	// constant — s_hasReference, s_hasObject, s_hasManager — every one of them a
	// separate name a new metatype could forget to override. One set says it all,
	// and a class that adds something ORs it onto its base's set, so "a document
	// is a reference plus an object" is written exactly that way.
	//
	// It says what the metatype HAS, not what it can do — the moment it starts
	// meaning the second thing it becomes a bag of unrelated bits.
	//
	// constexpr: the whole thing is answered at compile time (metaCtor.h asks it
	// with `if constexpr`), so no byte of this reaches the running program.
	enum ibMetaFeature : unsigned {
		ibMetaFeature_None      = 0,
		ibMetaFeature_Reference = 1u << 0,   // has references  → `<Name>Ref` family
		ibMetaFeature_Object    = 1u << 1,   // has data objects
		ibMetaFeature_Manager   = 1u << 2,   // has a manager
		ibMetaFeature_RecordSet = 1u << 3,   // has record sets (the registers)
		// A SELECTION — a cursor over the stored rows. Deliberately its own bit
		// and not derived from anything: an ENUMERATION has references and yet no
		// selection (its values are written in the configuration, not rows to walk),
		// while a REGISTER has no reference and does have one. Either derivation
		// would have been wrong for one of them.
		ibMetaFeature_Selection = 1u << 4,
	};

	// Nothing by default — a form, a template, a role has none of it.
	static constexpr unsigned s_features = ibMetaFeature_None;

public:

	// get object name as string 
	bool GetObjectNameAsString(wxString& result) const {
		return m_propertyName->GetValueAsString(result);
	}

	//system attributes
	ibMetaID GetMetaID() const { return m_metaId; }
	void SetMetaID(const ibMetaID& id) { m_metaId = id; }

	wxString GetName() const { return m_propertyName->GetValueAsString(); }
	// THE ONE DOOR THE STORED NAME CHANGES THROUGH — so it is where the metadata registry is told
	// its by-name cache no longer matches what the ctors compute. Out of line: it reaches into
	// ibMetaData, which is not complete here.
	void SetName(const wxString& strName);

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

	// ⭐ TWO TEXTS, TWO AUDIENCES, AND THAT IS WHY THERE ARE TWO.
	//
	// HELP is what the PERSON USING THE APPLICATION reads — the F1 text. It answers "what is this
	// and what do I put in it", it is written in their words, and it ships with the product.
	//
	// NOTES are the ENGINEERING INTENT — why this object exists, what it was decided to be, what
	// was tried and rejected. Markdown, written and read by whoever is building the configuration
	// (a developer, or the assistant), and read FIRST when work resumes: without it the modelling
	// drifts between sessions, because the reasons live only in whoever was there.
	//
	// Folding them into one field would force one of the two to be written wrong: user help
	// carrying design arguments, or design notes shipped to a user who wanted to know what to type.
	wxString GetHelpContent() const { return m_strHelpContent; }
	void SetHelpContent(const wxString& strHelpContent) { m_strHelpContent = strHelpContent; }

	wxString GetNoteContent() const { return m_strNoteContent; }
	void SetNoteContent(const wxString& strNoteContent) { m_strNoteContent = strNoteContent; }

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


public:

	// VIRTUAL, because a metaobject's liveness is not always its own. A common attribute's
	// copy is allowed only while the declaration that put it there is (metaCommonAttributeObject.h),
	// and everything that builds anything filters on this — FillArrayObjectByFilter tests it
	// first, so it is the one gate that makes a dropped declaration drop its columns.
	//
	// It was non-virtual, and the override further down (ibValueMetaObjectAttributeBase, from
	// the column base) only caught calls made THROUGH a column pointer. Every metadata walk
	// holds ibValueMetaObject*, so those calls went to this body and the override never ran —
	// caught by CommonAttribute.CopyStopsBeingAllowedWithItsDeclaration, and the same shape as
	// GetName, which is still non-virtual and is why the copy has to STORE its name.
	virtual bool IsAllowed() const {
		return IsEnabled()
			&& !IsDeleted();
	}

	// Virtual for the same reason as IsAllowed below: these three answer "does this object
	// count right now", and for an object whose existence depends on another one the honest
	// answer is not in its own flags. Non-virtual, an override here would be invisible to
	// every metadata walk — they all hold ibValueMetaObject*.
	//
	// ⚠ HOT PATH. All three are called per child by every metadata walk
	// (FillArrayObjectByFilter tests IsAllowed before anything else), so these are now
	// virtual calls inside the tightest loop in the metadata layer. Measured cost: none
	// observed — the walks allocate vectors anyway, which dwarfs a vtable hop. If a profile
	// ever points here, the fix is to cache the derived answer on the deriving object
	// (invalidated when its source changes) rather than to make these non-virtual again:
	// the correctness they buy is not optional.
	virtual bool IsEnabled() const {
		return (m_metaFlags & metaDisableFlag) == 0;
	}

	virtual bool IsDeleted() const {
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

	// (May this object be part of a composition? — ibCompositionObject::IsCompositionAllowed,
	// compositionHelper.h. The question lives with the mechanism that asks it.)

	// DOES THIS SHOW UP UNDER ITS OWNER? Not a new rule — FilterChild above already knows:
	// a child the owner does not accept is one it never created and cannot host, so it has
	// no business being listed under it. Predefined attributes, object modules and manager
	// modules are exactly that set.
	//
	// This is only the convenience form, asked from the child's side. Before it, the
	// designer tree spelled `GetClassType() == g_metaPredefinedAttributeCLSID` out TEN
	// times, once per metatype branch, and configuration-compare kept a second list of
	// clsids whose own comment admitted it was mirroring the first.
	// DELETED IS PART OF THE SAME ANSWER. Every caller paired this with an `IsDeleted()` line of its
	// own — four trees, the compare walker — because a child that is gone shows up under its owner
	// exactly as little as one the owner never accepted. Two spellings of one question is how the
	// copies drift: adding a branch, you remember the question you came for and not its neighbour.
	bool IsAcceptedByParent() const {
		if (IsDeleted())
			return false;
		const ibValueMetaObject* const owner = GetParent();
		return owner == nullptr || owner->FilterChild(GetClassType());
	}

	//process choice
	virtual bool ProcessChoice(ibBackendControlFrame* ownerValue,
		const wxString& strFormName, ibSelectMode selMode) const {
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

	// ⭐⭐ WHAT THIS METAOBJECT OFFERS TO OPEN — SAID AS DATA, DRAWN BY WHOEVER HAS A SCREEN.
	//
	// Max, 2026-09-01: *"you pass it a structure — the metaobject, its caption, its picture, a
	// separator between them — and you get data out. Opening you can do yourself, right there."*
	//
	// It used to be `PrepareContextMenu(wxMenu*)`: the metaobject built a WIDGET, in a header the
	// whole backend includes, against the two-DLL rule that says backend.dll names no GUI. And the
	// answer came back as `ProcessCommand(unsigned int id)` — a bare number whose meaning only the
	// object that emitted it knew.
	//
	// 🛑 AND THE NUMBERS WERE A HAND-KEPT LIST THAT HAD ALREADY FIRED. Twenty enumerations, nearly
	// every one of them starting at 19000, unique only WITHIN one metaobject — see ID_METATREE_LAST
	// in treeConfiguration.h, where two appended entries landed on top of Insert and Replace and
	// asking for Help opened the "replace report" file dialog. Two enums, no compiler on earth to
	// notice. With the item carrying the metaobject there is no number to collide: the ids are
	// handed out by the ONE place that draws the menu, in order, and thrown away with it.
	//
	// The census that decided the shape: of 39 branches across 22 metaobjects, 35 were
	// `OpenObjectForm(<a metaobject>)` and nothing else. The remaining 4 are the two modal editors
	// below, which have no metaobject to name — see ibMetaMenuItem.
	//
	// Returns TRUE when the standard tree commands — New / Edit / Remove / Properties — do NOT apply
	// to this row (the configuration root, a common-attribute copy: neither can be created or
	// deleted where it sits). It is not "I drew my own menu"; it never was.
	virtual bool CollectContextMenu(std::vector<ibMetaMenuItem>& items) { return false; }

	// ⚠ THE REMAINDER, AND IT IS TWO. `EditPredefinedValues` and `EditHomePage` open a MODAL DIALOG
	// rather than a document, so there is no metaobject for an item to carry — the module in
	// "open object module" is a real metaobject, which is exactly why it needs no verb of its own.
	// Those two surfaces have no identity, and giving them one is a metatype decision, not a
	// refactor. Until it is made, these four entries keep a command id, and the ids live in ONE
	// enum on the tree instead of twenty in the backend.
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

	// ⭐⭐ COPY & PASTE ARE NOT HERE ANY MORE — they are ibMetaData::CopyMetaObject and
	// ibMetaData::PasteMetaObject (Max, 2026-09-01: *"take them out of there altogether and move
	// them over to the metadata"*).
	//
	// They were public methods of the object, and so a paste was a thing an OBJECT did to itself
	// while reaching back for the metadata on its first line to create every child. Ten call sites
	// each made a shell by hand, filled it, announced the result or forgot to, and removed it on
	// failure or forgot that too. All of that is one of the five doors the metadata owns — create,
	// rename, copy, paste, remove — each raising its own event, and there is no longer a way round
	// them, because there is no longer a method here to call.
	//
	// The friendship is what the walkers need: they read m_metaGuid / m_metaCopyGuid, mark the
	// paste, walk m_children and run the per-aspect halves (CopyProperty / PasteProperty,
	// Save/LoadInterface, Save/LoadRole) that stay the object's own.
	friend class ibMetaData;

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
	virtual void ContributeTables(class ibSchemaSnapshot& out) const {
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
	wxString m_strNoteContent;  // the engineering intent, in markdown — see the accessors above

protected:

	ibPropertyCategory* m_categoryCommon = ibPropertyObject::CreatePropertyCategory(wxT("Common"), _("Common"));
	ibPropertyUString* m_propertyName = ibPropertyObject::CreateProperty<ibPropertyUString>(m_categoryCommon, wxT("Name"), _("Name"), _("Name of metadata object"), wxEmptyString);
	ibPropertyTString* m_propertySynonym = ibPropertyObject::CreateProperty<ibPropertyTString>(m_categoryCommon, wxT("Synonym"), _("Synonym"), _("Synonym of metadata object"), wxEmptyString);
	ibPropertyString* m_propertyComment = ibPropertyObject::CreateProperty<ibPropertyString>(m_categoryCommon, wxT("Comment"), _("Comment"), _("Comment"), wxEmptyString);
	ibPropertyCategory* m_categoryContext = ibPropertyObject::CreatePropertyCategory(wxT("Context"), _("Context"));
};

#endif
