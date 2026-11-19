#ifndef __QUERYABLE_FACTORY_H__
#define __QUERYABLE_FACTORY_H__

// Queryable-source FACTORY — a NON-OWNING registry of source DESCRIPTORS, owned by
// ibApplicationInstance (GetQueryableFactory(); the `query_sources` macro; nullptr
// pre/post-appData — the ibLockManager ownership pattern, token-gated ctor).
//
// The query language covers ONLY the relational metaclasses: records with a data-reference
// (catalogs / documents / charts of characteristic types & accounts / enumerations),
// registers, and constants. Reports & data tools register no descriptor → a query
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
#include "backend/standardCommand.h"        // ibCommandItem / ibFormID / ibActionID — the descriptor's command-surface signatures
#include "backend/srcDataObject.h"     // ibSourceDataObject::ibSourceExplorer — the columns the source fills
#include "backend/uniqueKey.h"         // ibUniqueKey — GetItemKey returns one BY VALUE (default = empty)

#include "queryable.h"                 // ibQueryPredicatePtr — a condition the source consumes itself
#include "core/query/querySourceParameter.h"   // ibQuerySourceParameter — what DescribeParameters declares

#include <map>
#include <memory>
#include <vector>

class ibBackendQueryable;
class ibBackendQueryableHolder;
class ibBackendQueryColumn;
class ibBackendValueForm;

// ⭐⭐ WHICH COLUMNS OF A CALLED TABLE THE QUERY READS — by the names it writes them with.
//
// A virtual table groups by the columns it publishes, and one of them may cut every row finer than the
// question: the correspondent of an accounting turnover splits "the turnover of 62" into one row per
// account it moved against. The reference groups only by what the query names, so the reading has to be
// told what that is. `m_all` when nobody could say — `SELECT *`, a check that runs nothing, a script —
// and then every column counts as read.
struct ibQueryReadColumns
{
	bool                  m_all = true;
	std::vector<ibString> m_names;

	bool Reads(const ibString& name) const
	{
		if (m_all)
			return true;
		for (const ibString& read : m_names)
			if (read.IsSameAs(name, false))
				return true;
		return false;
	}
};

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

	virtual ibString GetNamespace() const = 0;
	virtual ibString GetName() const = 0;
	// WHAT IT IS DRAWN WITH — by default its kind's: the class registered under its namespace (a catalog's, a
	// register's), so a table and its kind look alike. A source no class stands behind answers its own (the
	// registration journal). 0 = none.
	virtual ibPictureID GetPicture() const;

	// ⭐⭐ THE READING OF THIS SOURCE, MADE FROM THESE ARGUMENTS — the one entrance.
	//
	// (The arguments are a count + pointer-to-pointer of ibValue — the ibValue::Init idiom.) A virtual table — a
	// register's balance or turnovers, an information register's slice — first SELECTS, by its period and its
	// conditions, and only then shares its rows, so every reading of it is its own: made here from ITS arguments
	// (`std::make_shared`) and owned by whoever asked — the query reading it, whose builder and result keep it for
	// as long as they name it; a list, for as long as it reads. Nothing of a reading stays in the descriptor, and
	// the factory holds descriptors only. A source that takes no arguments — a catalog, a tabular section — hands
	// out its own queryable, shared without owning. "The same source" is the same DESCRIPTOR, never the same
	// pointer: two readings of one table are two objects.
	//
	// `conditions` is parallel to the declared parameter list: an entry is non-null exactly where the declaration
	// said `m_consumedBySource` and the author wrote a predicate there. ⚠ ALREADY LOWERED, against
	// `GetConditionScope()`: a condition names columns, and the reading it is about does not exist yet. `read` is
	// what the query reads of the table (ibQueryReadColumns) — for a source whose rows depend on it.
	//
	// (It was two entrances, a raw pointer the descriptor kept and one with the conditions, and the descriptor
	// kept EVERY reading, keyed by its arguments, until the application closed. A sale's FIFO read has its own
	// date and its own items: the web server kept two readings a sale — 8 017 after 4 000 sales — and four sales
	// posting at once reallocated that list under each other (load-million-rows.md § 16).)
	virtual std::shared_ptr<const ibBackendQueryable> CreateQueryable(ibValue** paParams, long lSizeArray,
		const std::vector<ibQueryPredicatePtr>& conditions = {}, const ibQueryReadColumns& read = {}) = 0;

	// WHAT A CONSUMED CONDITION IS RESOLVED AGAINST — null (the default) means this source consumes
	// none, and the lowering leaves every condition where it was: in the WHERE around the reading.
	virtual std::shared_ptr<const ibBackendQueryable> GetConditionScope() const { return nullptr; }

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
	// The row's STATE PICTURE (what a list's first column shows at its left), read PER SOURCE off the row's cells —
	// a record by its deletion mark, a document by that and its posting, a register's record by its activity. The
	// kind knows which cells say it, as it knows which cell is the key. 0 = this source's rows carry none.
	virtual ibPictureID GetRowPicture(const ibRowMetaValues& /*rowValues*/) const { return 0; }
	// The restore ROW-KEY (primary-key column VALUES) from a row's identity VALUE — the INVERSE of what the fetch
	// stamps into a node's m_rowKey. The list's FindRowValue selection-restore forwards here: the stub carries this
	// key, matched against the freshly-fetched batch by m_rowKey. Base (no queryable) = the value IS the key
	// ({value}); ibMetaQueryDescriptor reads the PK columns off the value (a register decomposes its composite key).
	virtual std::vector<ibValue> GetRowKeyByValue(const ibValue& value) const {
		return value.IsEmpty() ? std::vector<ibValue>{} : std::vector<ibValue>{ value };
	}
	// THE ARGUMENTS THIS SOURCE TAKES, IN ORDER. Empty = takes none (an ordinary table). The order
	// IS the call: Balance(moment, condition); Turnovers(begin, end, periodicity, condition).
	virtual void DescribeParameters(std::vector<ibQuerySourceParameter>& /*out*/) const {}

	// ⭐⭐ WHAT COLUMNS THIS TABLE HAS, ASKED WITHOUT RUNNING IT — the QUERY's own columns, for the readers that
	// want the shape rather than the rows (the constructor's catalogue, a report's Fields page). Each answers
	// for itself: its name, type, picture, availability, caption and what it is in a balance. A virtual table's
	// columns live here and only here — a balance or a turnover is not a source anybody binds a form to.
	//
	// ASKED WITH THE CALL'S ARGUMENTS, because for some sources the ARGUMENTS DECIDE THE COLUMNS. A register's
	// turnovers is the case: its periodicity says at what granularity the interval is read, and that is not a
	// filter over a fixed set of columns — it IS which columns exist. Ask for months and there is one period
	// column, rolled to the month; ask for the recorder and the document comes with it; leave it out and the
	// table offers every projection it can make, for the author to choose from.
	//
	// ⚠ ONLY WHAT IS KNOWN AT DESIGN TIME reaches here. An argument written as `&Period` has no value
	// until the query runs, and arrives empty — which is right: the shape is then whatever the table
	// can offer, exactly as with no argument at all. (None → no columns.)
	virtual std::vector<std::shared_ptr<const ibBackendQueryColumn>> GetQueryColumns(const std::vector<ibValue>& /*args*/) const { return {}; }

	// WHICH COLUMNS A CONDITION PARAMETER MAY NAME. Defaults to everything the source has, which is
	// right for a slice — it carries its dimensions, resources and attributes, and any of them can
	// be filtered. A folded table overrides: a balance is filtered by the dimensions it is grouped
	// by, never by a resource it summed, and offering the resource would be offering a filter the
	// engine cannot honour. A separate question from GetQueryColumns on purpose: "what do you RETURN" and
	// "what may I FILTER BY" are different sets the moment a table folds anything.
	//
	// ⭐⭐ ASKED ABOUT ONE SLOT — because a source may admit different fields in different condition slots,
	// and answering with their union is wrong in BOTH directions. An accounting register is the case:
	// `AccountCondition` (and its Dr / Cr / Corr siblings) admit ACCOUNTS and nothing else — the source
	// consumes them and folds by them, and a leaf about anything else is silently dropped, i.e. reports
	// more than was asked for. The general `Condition` admits the ordinary fields — dimensions and the
	// analytics slots — and offering it accounts instead hides everything a filter is normally written with.
	// `slot` empty = the general condition.
	virtual std::vector<std::shared_ptr<const ibBackendQueryColumn>> GetConditionColumns(const ibString& /*slot*/) const {
		return GetQueryColumns({});
	}

	// FILL the list's columns into the source explorer — the SOURCE's side, which a dynamic list and a form bind
	// to: real columns, never a virtual table's. The whole append loop (system columns hidden) lives on the
	// SOURCE; the dynamic list just resets the explorer and hands it here. (None → left empty.)
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

// WHOSE SOURCES a factory is asked for (GetDescriptors) — the application's own, the platform's, or both.
enum class ibQueryableSourceKind { Application, Platform, Any };

class BACKEND_API ibQueryableFactory
{
public:
	// The GLOBAL factory's construction is restricted to ibApplicationInstance via the ib::AppDataCtorToken gate
	// (mirrors ibLockManager); reached through ibApplicationInstance::GetQueryableFactory(). The per-application subclass
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
	// Drop ALL references (application teardown). Does NOT delete — descriptors are owned elsewhere.
	void Clear();

	bool HasNamespace(const ibString& ns) const;

	// ⭐ DESCRIPTORS, NEVER QUERYABLES. The factory maps names and table ids to descriptors and holds nothing else;
	// a queryable is the asker's — asked of the descriptor (CreateQueryable) and held for as long as it is read.

	// Enumerate registered source descriptors — the dynamic-list source picker lists
	// them by GetNamespace()/GetName(). Non-owning pointers, valid for owners' life. Whose (ibQueryableSourceKind): the
	// application's — an application's factory gives them in the order the application is read; the PLATFORM's
	// (ibPlatformSourceRegister) — `Data.Platform`, the constructors' Platform: the per-application factory makes and holds
	// them, this one has none; Any — both, the application's first.
	virtual std::vector<ibQueryableSourceDescriptor*> GetDescriptors(ibQueryableSourceKind kind) const;

	// THE DESCRIPTOR ITSELF, not the queryable it builds. The lowering needs it to ask what the
	// source's arguments MEAN (DescribeParameters) before evaluating them — a condition argument
	// must not be evaluated as a value at all. Null when nothing answers to that name.
	virtual ibQueryableSourceDescriptor* FindDescriptor(const ibString& ns, const ibString& objectName) const;

	// Resolve the DESCRIPTOR by its table id (queryable->GetQueryTableId()) — the stable id the dynamic-source
	// property serializes; the dynamic list re-resolves this LIVE to reach the source's table (CreateQueryable) and
	// its COMMAND surface (GetCommandCollection / CallAsCommand / ShowValueByKey / GetSelectValue / FillSourceExplorer).
	// Non-owning, valid for the owner's life. Null when none — and 0 names none (a reading with no table answers it).
	// VIRTUAL — the per-application subclass descends to the global factory on a local miss.
	virtual ibQueryableSourceDescriptor* ResolveDescriptorById(ibMetaID tableId) const;

protected:
	ibQueryableFactory() = default;   // per-application subclass (ibMetaQueryableFactory) — no appData token

private:
	ibQueryableFactory(const ibQueryableFactory&) = delete;
	ibQueryableFactory& operator=(const ibQueryableFactory&) = delete;

	static ibString Key(const ibString& ns, const ibString& name);   // upper("ns|name")
	std::map<ibString, ibQueryableSourceDescriptor*> m_descriptors;  // non-owning

	// ⭐ THE SAME DESCRIPTORS, BY TABLE ID — so a lookup by id is a lookup and not a walk. Kept by Register /
	// Unregister / Clear alongside m_descriptors: for each id, its descriptors in KEY order (the first one is what
	// the walk found first), and for each key the id it was filed under — so a key is taken out of the index
	// without asking the descriptor that stood under it, which may already be gone.
	void IndexById(const ibString& key, ibQueryableSourceDescriptor* descriptor);
	void UnindexById(const ibString& key);
	std::map<ibMetaID, std::map<ibString, ibQueryableSourceDescriptor*>> m_byTableId;   // non-owning
	std::map<ibString, ibMetaID> m_tableIdOfKey;
};

// ⭐ A SOURCE THE PLATFORM DECLARES IN EVERY APPLICATION — no metaobject's: the registration journal's events
// today, an external table or another reporting engine tomorrow. It is made FOR the application it is read in
// (the journal's Object is a reference of THAT application), so each application's factory makes its own,
// holds it and registers it as it is made. A provider adds its source with one static ibPlatformSourceRegister in
// its own .cpp — the type registers' idiom (value_register) — and nothing outside the provider names it.
using ibPlatformSourceMaker = std::unique_ptr<ibQueryableSourceDescriptor> (*)(const class ibMetaData* metaData);

class BACKEND_API ibPlatformSourceRegister
{
public:
	explicit ibPlatformSourceRegister(ibPlatformSourceMaker maker);
};

// The PER-APPLICATION source factory — one per open snapshot (ibMetaImage), holding that application's OWN metadata-backed
// source descriptors and the platform's sources made for it. On a LOCAL miss the resolve DESCENDS to the global
// factory (plugin / system / common sources), so an application sees its own sources FIRST and the shared ones as a
// fallback. A copy / a second open application therefore resolves ITS source (its columns), never whatever registered
// globally last — the fix for copied dynamic lists.
class BACKEND_API ibMetaQueryableFactory : public ibQueryableFactory
{
public:
	// Owned by the snapshot; no appData token. Makes every platform source for `metaData` and registers it.
	explicit ibMetaQueryableFactory(const class ibMetaData* metaData);

	// Its own: the application's in the order the application is read — the kinds as its tree stands them, a kind's
	// tables by name; the platform's made for it (m_platformSources); Any — the application's, then the platform's.
	// Whoever lists them (the constructors, a list's source) takes them as they come.
	// (Body in provider/database/dbTableProvider.cpp, beside GetSourceFactory: the order is the application's.)
	virtual std::vector<ibQueryableSourceDescriptor*> GetDescriptors(ibQueryableSourceKind kind) const override;
	virtual ibQueryableSourceDescriptor* ResolveDescriptorById(ibMetaID tableId) const override;
	// Own registry first, then the global one — the same order ResolveDescriptorById keeps, for the same reason:
	// an application's sources are its own, and a miss falls through to what is shared.
	virtual ibQueryableSourceDescriptor* FindDescriptor(const ibString& ns, const ibString& objectName) const override;

private:
	// The platform's sources made for this application — HELD here, unlike a metaobject's descriptor, which its
	// metaobject owns: nothing else stands behind them. Registered by the ctor, for the factory's life.
	std::vector<std::unique_ptr<ibQueryableSourceDescriptor>> m_platformSources;
};

// The factory a query written on behalf of `metaData` resolves its sources through — ibMetaData::GetSourceFactory, asked
// without the metadata header: an application is the provider's to know, never the lowering's. Null for no metadata.
// (Body in provider/database/dbTableProvider.cpp, beside the provider's other answers by an application.)
BACKEND_API ibQueryableFactory* GetSourceFactory(const class ibMetaData* metaData);

// The tables a reference of this TYPE points at, in the application `metaData` — one per reference type the type
// admits (an "any" reference is every one it admits). For a holder of fields that has a type with no column behind
// it — a setting's field, walked by its type (ibQueryConstructorModel::WalkFrom); over a column the provider answers
// (ResolveReferenceTargets), with this. (Body in provider/database/dbTableProvider.cpp, beside GetSourceFactory.)
BACKEND_API std::vector<std::shared_ptr<const ibBackendQueryable>> ReferenceTargetsOf(const class ibMetaData* metaData,
                                                                                      const ibTypeDescription& type);

#endif
