#ifndef __QUERYABLE_FACTORY_H__
#define __QUERYABLE_FACTORY_H__

// Queryable-source FACTORY — a NON-OWNING registry of source DESCRIPTORS, owned by
// ibApplicationData (GetQueryableFactory(); the `query_sources` macro; nullptr
// pre/post-appData — the ibLockManager ownership pattern, token-gated ctor).
//
// The query language covers ONLY the relational metaclasses: records with a data-reference
// (catalogs / documents / charts of characteristic types & accounts / enumerations),
// registers, and constants. Reports & data processors register no descriptor → a query
// against them simply fails to resolve.
//
// A DESCRIPTOR is OWNED BY THE METAOBJECT (a member, like m_queryable), registered with the
// factory by POINTER when the object runs and unregistered by pointer when it closes — no
// name / clsid round-trip. The factory just maps (namespace, name) -> descriptor* and asks
// it to CREATE the queryable.
//
// The standard descriptor is the TEMPLATE ibMetaCommandDescriptor<TQueryable, TMeta> — it
// CONTAINS the metaobject's queryable and replaces its former plain `m_queryable` field. A
// separate table (a register's balance / turnover / slice) or an external source uses its OWN
// descriptor subclass whose CreateQueryable BUILDS a fresh configured queryable from the call
// params (these are registered per concrete register / per external source). (docs §22.0 / §23.)

#include "backend/backend_core.h"     // core prelude (wx set up first) — provides ibClassID + BACKEND_API in order
#include "backend/appDataCtorToken.h" // ib::AppDataCtorToken (owner-only construction)
#include "backend/compiler/value.h"   // ibValue::GetNameObjectFromID (the metaobject's registered name = the namespace token)
#include "backend/actionInfo.h"        // ibCommandItem / ibFormID / ibActionID — the descriptor's command-surface signatures
#include "backend/srcDataObject.h"     // ibSourceDataObject::ibSourceExplorer — the columns the source fills
#include "backend/uniqueKey.h"         // ibUniqueKey — GetItemKey returns one BY VALUE (default = empty)

#include <wx/string.h>
#include <map>
#include <vector>

class ibBackendQueryable;
class ibBackendQueryableHolder;
class ibBackendQueryColumn;
class ibBackendValueForm;

// A source DESCRIPTION, identified by (namespace, name): it CREATES the queryable from the
// metadata it stores + the call-scoped params (count + pointer-to-pointer of ibValue — the
// ibValue::Init idiom). No separate setup step — construction happens in CreateQueryable.
class BACKEND_API ibQueryableSourceDescriptor
{
public:
	// A source descriptor is `this`-BOUND (it holds a back-pointer to its metaobject) — it must NEVER be copied,
	// else a copy would keep the ORIGINAL's identifiers. So copy is DELETED: a metaobject holding one is thereby
	// non-copy-constructible, which forces the correct paste-via-factory path (a FRESH object, freshly bound with
	// its own new metaID / guid) — copying identifiers can never go wrong silently. (Max: verify id copy on paste.)
	ibQueryableSourceDescriptor() = default;
	ibQueryableSourceDescriptor(const ibQueryableSourceDescriptor&) = delete;
	ibQueryableSourceDescriptor& operator=(const ibQueryableSourceDescriptor&) = delete;
	virtual ~ibQueryableSourceDescriptor() = default;

	virtual wxString GetNamespace() const = 0;
	virtual wxString GetName() const = 0;

	// CREATE the queryable from the call-scoped params (count + pointer-to-pointer of ibValue —
	// the ibValue::Init idiom). NON-const: a parameterized source (a register's balance / slice)
	// builds + configures its call-scoped companion HERE from the params and OWNS the result; a
	// standard source returns its stable contained member. The returned pointer is owned by the
	// descriptor (borrowed by the caller) — valid for the descriptor's life.
	virtual const ibBackendQueryable* CreateQueryable(ibValue** paParams, long lSizeArray) = 0;

	// ---- The source's COMMAND surface, held PARALLEL to the queryable (queryable = data; these = how a LIST
	// acts on a row). This is the SINGLE bridge a metadata-blind dynamic list talks to: it fills the columns,
	// lists + runs the commands, opens a row, and resolves the picker select. The BASE is neutral (an enum /
	// custom source carries no behaviour — an honest no-op); the templated ibMetaCommandDescriptor below simply
	// FORWARDS each call to its metaobject (which it knows by type), so the polymorphism lives in the metadata's
	// own inheritance — no mixin, no separate command interface, nothing pushed onto the generic metaobject base.

	// ---- ROW DATA / presentation: what a row IS and how it shows (read off the row's cells). ----
	// The row's SELECT value (what a picker returns on Choose), chosen PER SOURCE from the row's cells (a record →
	// its reference cell, a register → its composite record key). Given the row's value map. Empty = no select.
	virtual ibValue GetSelectValue(const ibRowMetaValues& /*rowValues*/) const { return ibValue(); }
	// The row's IDENTITY key (what ShowValueByKey / CallAsCommand consume), decoded PER SOURCE from the row's cells:
	// a record → its reference guid, a register → its COMPOSITE record key (registers have several key columns).
	// The node carries no metaobject id, so only the metaobject can shape it. Given the row's value map. Empty = none.
	virtual ibUniqueKey GetItemKey(const ibRowMetaValues& /*rowValues*/) const { return ibUniqueKey(); }
	// The restore ROW-KEY (primary-key column VALUES) from a row's identity VALUE — the INVERSE of what the fetch
	// stamps into a node's m_rowKey. The list's FindRowValue selection-restore forwards here: the stub carries this
	// key, matched against the freshly-fetched batch by m_rowKey. Base (no queryable) = the value IS the key
	// ({value}); ibMetaQueryDescriptor reads the PK columns off the value (a register decomposes its composite key).
	virtual std::vector<ibValue> GetRowKeyByValue(const ibValue& value) const {
		return value.IsEmpty() ? std::vector<ibValue>{} : std::vector<ibValue>{ value };
	}
	// FILL the list's columns into the source explorer — the whole append loop (system columns hidden) lives on
	// the SOURCE. The dynamic list just resets the explorer and hands it here. (None → left empty.)
	virtual void FillSourceExplorer(ibSourceDataObject::ibSourceExplorer& /*explorer*/) const {}

	// ---- COMMAND INTERFACE: the command band (list the commands, run one by id). ----
	// LIST the commands this source offers (Add / Copy / Edit / … — the TableBox merges them into its bar).
	virtual void GetCommandCollection(const ibFormID& /*formType*/, std::vector<ibCommandItem>& /*commands*/) const {}
	// RUN a command by id; `key` = the selected row (delete / edit target, and the parent source for create when
	// present), `anchor` = where the user stands in the tree (the create fallback when nothing is selected so a
	// new element lands in the browsed folder); `srcForm` is the form to open under / refresh.
	virtual void CallAsCommand(ibActionID /*id*/, const ibUniqueKey& /*anchor*/, const ibUniqueKey& /*key*/, ibBackendValueForm* /*srcForm*/) const {}

	// ---- ENTRY: open a row's value directly (the double-click / "enter" affordance, no command id). ----
	virtual void ShowValueByKey(const ibUniqueKey& /*key*/, ibBackendValueForm* /*srcForm*/) const {}
};

// (The metaobject-coupled source-descriptor templates — ibMetaQueryDescriptor / ibMetaCommandDescriptor — moved to
//  metaCollection/partial/commonObject.h, where the metaobjects that instantiate them live. This L4 header keeps only
//  the metadata-agnostic base ibQueryableSourceDescriptor above and the factory below.)

class BACKEND_API ibQueryableFactory
{
public:
	// The GLOBAL factory's construction is restricted to ibApplicationData via the ib::AppDataCtorToken gate
	// (mirrors ibLockManager); reached through ibApplicationData::GetQueryableFactory(). The per-config subclass
	// (ibMetaQueryableFactory, on the snapshot) uses the protected default ctor. Virtual dtor — the image owns the
	// subclass through a base unique_ptr.
	explicit ibQueryableFactory(ib::AppDataCtorToken);
	virtual ~ibQueryableFactory() = default;

	// Register / unregister a descriptor BY POINTER (the descriptor is owned by the metaobject
	// / external source — the factory only references it). Register keys on the descriptor's
	// (namespace, name); Unregister drops it only if it is the SAME pointer (a baseline object
	// closing can't drop the active one's same-named descriptor).
	void Register(ibQueryableSourceDescriptor* descriptor);
	void Unregister(ibQueryableSourceDescriptor* descriptor);
	// Drop ALL references (config teardown). Does NOT delete — descriptors are owned elsewhere.
	void Clear();

	bool HasNamespace(const wxString& ns) const;

	// Resolve <ns>.<objectName> (+ call-scoped params) -> queryable: find the (ns,name)
	// descriptor and ask it to CREATE the queryable. Null when unknown. VIRTUAL — the per-config subclass
	// descends to the global factory on a local miss.
	virtual const ibBackendQueryable* Resolve(const wxString& ns, const wxString& objectName,
	                                  ibValue** paParams = nullptr, long lSizeArray = 0) const;

	// Enumerate registered source descriptors — the dynamic-list source picker lists
	// them by GetNamespace()/GetName(). Non-owning pointers, valid for owners' life.
	std::vector<ibQueryableSourceDescriptor*> GetDescriptors() const;

	// Resolve a source by its table id (queryable->GetQueryTableId()) — the stable id the
	// dynamic-source property serializes. Null when none matches. VIRTUAL (see Resolve).
	virtual const ibBackendQueryable* ResolveById(ibMetaID tableId) const;

	// Resolve the DESCRIPTOR by the same table id — the dynamic list re-resolves this LIVE to reach the
	// source's COMMAND surface (GetCommandCollection / CallAsCommand / ShowValueByKey / GetSelectValue /
	// FillSourceExplorer) parallel to its queryable. Non-owning, valid for the owner's life. Null when none.
	virtual ibQueryableSourceDescriptor* ResolveDescriptorById(ibMetaID tableId) const;

protected:
	ibQueryableFactory() = default;   // per-config subclass (ibMetaQueryableFactory) — no appData token

private:
	ibQueryableFactory(const ibQueryableFactory&) = delete;
	ibQueryableFactory& operator=(const ibQueryableFactory&) = delete;

	static wxString Key(const wxString& ns, const wxString& name);   // upper("ns|name")
	std::map<wxString, ibQueryableSourceDescriptor*> m_descriptors;  // non-owning
};

// The PER-CONFIG source factory — one per open snapshot (ibMetaImage), holding that config's OWN metadata-backed
// source descriptors. On a LOCAL miss the resolve DESCENDS to the global factory (plugin / system / common sources),
// so a config sees its own sources FIRST and the shared ones as a fallback. A copy / a second open config therefore
// resolves ITS source (its columns), never whatever registered globally last — the fix for copied dynamic lists.
class BACKEND_API ibMetaQueryableFactory : public ibQueryableFactory
{
public:
	ibMetaQueryableFactory() = default;   // owned by the snapshot; no appData token

	const ibBackendQueryable* Resolve(const wxString& ns, const wxString& objectName,
	                                  ibValue** paParams = nullptr, long lSizeArray = 0) const override;
	const ibBackendQueryable* ResolveById(ibMetaID tableId) const override;
	ibQueryableSourceDescriptor* ResolveDescriptorById(ibMetaID tableId) const override;
};

#endif
