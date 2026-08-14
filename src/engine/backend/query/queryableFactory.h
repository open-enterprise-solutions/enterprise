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
#include "backend/standardCommand.h"        // ibCommandItem / ibFormID / ibActionID — the descriptor's command-surface signatures
#include "backend/srcDataObject.h"     // ibSourceDataObject::ibSourceExplorer — the columns the source fills
#include "backend/uniqueKey.h"         // ibUniqueKey — GetItemKey returns one BY VALUE (default = empty)

#include "queryable.h"                 // ibQueryPredicatePtr — a condition the source consumes itself

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
// ONE PARAMETER OF A SOURCE CALL — `Balance(&Period, Warehouse = &Store)`.
//
// A virtual table takes its arguments IN ORDER and, until now, read them positionally out of
// paParams[i] with nobody else knowing what `i` meant: not the constructor (which therefore had no
// way to offer them), not the name check (which could not see a missing or surplus argument), not a
// person reading the text. The source is the only one who knows; this is it saying so.
//
// EVERY ARGUMENT IS AN EXPRESSION — `&Period`, a literal, `BegOfMonth(&Date)`, `Warehouse = &Store`.
// The kind does not say "expression or not"; it says WHERE ITS NAMES ARE LOOKED UP and WHAT SORT OF
// ANSWER is expected:
//
//   * VALUE     — a scalar, and it is settled BEFORE THE QUERY RUNS. That is the whole reason no
//                 column is in scope here — not this table's, and certainly not another table of the
//                 query. The order is: the arguments are evaluated, THEN the virtual table produces
//                 its rows from them, THEN the engine joins and filters. A parameter that named a
//                 column would be asking for a value from rows that do not exist yet.
//                 So what may stand here is a parameter, a literal, or a computation over them
//                 (`BegOfMonth(&Date)`). `m_type` says what it holds, and that also decides how it
//                 is edited: a date gets a date box, a registered enumeration (a turnover table's
//                 periodicity: second … year) gets its own list. Deliberately NOT a separate
//                 "periodicity" kind — that would restate the enumeration in a widget, and the next
//                 member added to it would have to be added twice.
//   * CONDITION — a predicate. Identifiers in it are THIS TABLE'S columns; `&name` is still an
//                 ordinary query parameter, so the two cannot be confused (the ampersand is the
//                 distinction). Which columns are admissible is a question of its own —
//                 FillConditionExplorer below — because a balance may be filtered by its dimensions
//                 but not by a resource it folded, while a slice admits every attribute it carries.
struct ibQuerySourceParameter
{
	wxString          m_name;                 // Period / Begin / End / Periodicity / Condition
	bool              m_condition = false;    // false = value expression, true = predicate
	ibTypeDescription m_type;                 // VALUE only: what it holds — and therefore how it is edited
	bool              m_required = false;

	// ⭐ A CLOSED SET OF VALUES, WHEN THE ARGUMENT HAS ONE — the periodicity a turnover rolls up to,
	// the way a balance-and-turnovers table is filled. Declared BY THE SOURCE, because the source is
	// the only one that knows what it accepts; a window that carried the list would be holding a
	// second copy of the source's own vocabulary, and the two would part company on the first change.
	//
	// Empty means "any expression the language can write" — a moment, a parameter, a computation.
	// Non-empty makes the argument a CHOICE, and the editor shows exactly these and nothing else.
	std::vector<wxString> m_choices;

	// ⭐⭐ THE SOURCE CONSUMES THIS CONDITION ITSELF — it is not folded into the WHERE around it.
	//
	// An ordinary condition slot is sugar: the predicate written there is ANDed into the query's own
	// WHERE, and the source never learns of it. That is right when the condition only SELECTS rows.
	// It is wrong when the source has to ACT on it — an accounting register asked for accounts «in
	// hierarchy» reports the subordinate accounts UNDER the one that was named, and a filter applied
	// around the reading cannot fold anything: it can only remove rows the reading already produced.
	//
	// Set, the lowering resolves the predicate against `GetConditionScope()` and hands it to
	// `CreateQueryable` by slot position instead of into the WHERE. Nothing else changes: the source
	// is then the only place that condition exists, which is what makes the fold possible at all.
	bool m_consumedBySource = false;

	// AND WHAT THE SOURCE USES WHEN THE ARGUMENT IS LEFT OUT. Shown to the author rather than left to
	// be guessed: an empty box that quietly means "Auto" teaches nothing, and the first surprise
	// arrives when somebody sets it explicitly and the result changes.
	wxString              m_default;

};

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

private:
	// The queryables this descriptor has built for parameterized calls — see MakeCompanion below.
	// `shared_ptr<void>` so this header needs no complete type; the raw pointer is the same object,
	// kept because a void one cannot be cast back without knowing what it was.
	struct Companion
	{
		std::vector<ibValue>  m_call;
		std::shared_ptr<void> m_owned;
		const void*           m_q = nullptr;
	};
	std::vector<Companion> m_companions;

	// TWO CALLS ARE THE SAME CALL when they have the same arguments. Comparison goes through
	// ibValue's own equality, and it is allowed to REFUSE (two values of unrelated types are not
	// comparable) — a refusal means "not the same", never an exception out of a lifetime helper.
	static bool SameCall(const std::vector<ibValue>& a, const std::vector<ibValue>& b)
	{
		if (a.size() != b.size())
			return false;
		for (size_t i = 0; i < a.size(); ++i) {
			bool equal = false;
			try { equal = (a[i] == b[i]); }
			catch (...) { return false; }
			if (!equal)
				return false;
		}
		return true;
	}

public:

	virtual wxString GetNamespace() const = 0;
	virtual wxString GetName() const = 0;

	// CREATE the queryable from the call-scoped params (count + pointer-to-pointer of ibValue —
	// the ibValue::Init idiom). NON-const: a parameterized source (a register's balance / slice)
	// builds + configures its call-scoped companion HERE from the params and OWNS the result; a
	// standard source returns its stable contained member. The returned pointer is owned by the
	// descriptor (borrowed by the caller) — valid for the descriptor's life.
	virtual const ibBackendQueryable* CreateQueryable(ibValue** paParams, long lSizeArray) = 0;

	// ⭐⭐ THE SAME CALL, WITH THE CONDITIONS THE SOURCE CONSUMES ITSELF.
	//
	// `conditions` is parallel to the declared parameter list: an entry is non-null exactly where the
	// declaration said `m_consumedBySource` and the author wrote a predicate there. Everything else is
	// unchanged, which is why the default forwards — a source that consumes nothing never sees this.
	//
	// ⚠ The predicates are ALREADY LOWERED, against `GetConditionScope()`. They have to be: a
	// condition names columns, and the companion this call is about does not exist yet — resolving
	// against it would be resolving against the thing being built. The scope is the source's stable
	// side (a register's movements table), where the account column lives in any case.
	virtual const ibBackendQueryable* CreateQueryable(ibValue** paParams, long lSizeArray,
	                                                  const std::vector<ibQueryPredicatePtr>& conditions)
	{
		(void)conditions;
		return CreateQueryable(paParams, lSizeArray);
	}

	// WHAT A CONSUMED CONDITION IS RESOLVED AGAINST — null (the default) means this source consumes
	// none, and the lowering leaves every condition where it was: in the WHERE around the reading.
	virtual const ibBackendQueryable* GetConditionScope() const { return nullptr; }

	// ⭐⭐ THE CALL-SCOPED COMPANION, AND ITS LIFETIME — here, once, so no descriptor has to remember.
	//
	// A parameterized source (a register's balance / turnovers, an information register's slice)
	// BUILDS a queryable from the arguments of the call. Every one of them held it in a single
	// `std::unique_ptr` member and assigned over it — which quietly means "the last call's object is
	// the only one alive", and that is not what the contract two lines above promises.
	//
	// It is not a corner case. A query may read the SAME virtual table twice — two slices as of two
	// dates is the ordinary way to compare them — and the lowering resolves each source in turn,
	// keeping the pointers side by side while it walks the columns. The second resolve destroyed the
	// first companion under the binding that already pointed at it, and the walk then read freed
	// memory: an access violation at `0xdddddddd`, from a query that is perfectly legal.
	//
	// So: what is handed out STAYS handed out. `std::shared_ptr<void>` owns it without this header
	// ever needing the complete type — the deleter is captured where T is known.
	//
	// AND THE SAME CALL GIVES BACK THE SAME OBJECT. Without that this would grow without bound: the
	// query constructor re-asks the engine after every keystroke, always with the same arguments.
	// With it, the store holds one entry per DISTINCT parameterisation — which is exactly what a
	// query needs alive at once.
	template <typename TCompanion, typename... TArgs>
	const TCompanion* MakeCompanion(ibValue** paParams, long lSizeArray, TArgs&&... args)
	{
		std::vector<ibValue> call;
		for (long i = 0; i < lSizeArray; ++i)
			call.push_back(paParams != nullptr && paParams[i] != nullptr ? *paParams[i] : ibValue());

		for (const Companion& made : m_companions)
			if (SameCall(made.m_call, call))
				return static_cast<const TCompanion*>(made.m_q);

		std::shared_ptr<TCompanion> built = std::make_shared<TCompanion>(std::forward<TArgs>(args)...);
		const TCompanion* borrowed = built.get();
		m_companions.push_back({ std::move(call), std::move(built), borrowed });
		return borrowed;
	}

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
	// THE ARGUMENTS THIS SOURCE TAKES, IN ORDER. Empty = takes none (an ordinary table). The order
	// IS the call: Balance(moment, condition); Turnovers(begin, end, periodicity, condition).
	virtual void DescribeParameters(std::vector<ibQuerySourceParameter>& /*out*/) const {}

	// WHICH COLUMNS A CONDITION PARAMETER MAY NAME. Defaults to everything the source has, which is
	// right for a slice — it carries its dimensions, resources and attributes, and any of them can
	// be filtered. A folded table overrides: a balance is filtered by the dimensions it is grouped
	// by, never by a resource it summed, and offering the resource would be offering a filter the
	// engine cannot honour.
	//
	// A separate question from FillSourceExplorer on purpose: "what do you RETURN" and "what may I
	// FILTER BY" are different sets the moment a table folds anything.
	virtual void FillConditionExplorer(ibSourceDataObject::ibSourceExplorer& explorer) const {
		FillSourceExplorer(explorer);
	}

	// ⭐⭐ THE SAME QUESTION, ASKED ABOUT ONE SLOT — because a source may admit different fields in
	// different condition slots, and answering with their union is wrong in BOTH directions.
	//
	// An accounting register is the case: `AccountCondition` (and its Dr / Cr / Corr siblings) admit
	// ACCOUNTS and nothing else — the source consumes them and folds by them, and a leaf about
	// anything else is silently dropped, i.e. reports more than was asked for. The general
	// `Condition` admits the ordinary fields — dimensions and the analytics slots — and offering it
	// accounts instead hides everything a filter is normally written with.
	//
	// `slot` empty = the general condition, which is what the plain overload above answers.
	virtual void FillConditionExplorer(ibSourceDataObject::ibSourceExplorer& explorer,
	                                   const wxString& slot) const {
		FillConditionExplorer(explorer);
	}

	// ⭐⭐ THE SAME QUESTION, ASKED WITH THE CALL'S ARGUMENTS — because for some sources the ARGUMENTS
	// DECIDE THE COLUMNS.
	//
	// A register's turnovers is the case: its periodicity says at what granularity the interval is
	// read, and that is not a filter over a fixed set of columns — it IS which columns exist. Ask for
	// months and there is one period column, rolled to the month; ask for the recorder and the
	// document comes with it; leave it out and the table offers every projection it can make, for the
	// author to choose from.
	//
	// The default forwards to the parameterless one: a source whose shape does not depend on its call
	// (every ordinary table, a slice) needs to know nothing about this.
	//
	// ⚠ ONLY WHAT IS KNOWN AT DESIGN TIME reaches here. An argument written as `&Period` has no value
	// until the query runs, and arrives empty — which is right: the shape is then whatever the table
	// can offer, exactly as with no argument at all.
	virtual void FillSourceExplorer(ibSourceDataObject::ibSourceExplorer& explorer,
	                                const std::vector<ibValue>& /*args*/) const {
		FillSourceExplorer(explorer);
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

	// THE DESCRIPTOR ITSELF, not the queryable it builds. The lowering needs it to ask what the
	// source's arguments MEAN (DescribeParameters) before evaluating them — a condition argument
	// must not be evaluated as a value at all. Null when nothing answers to that name.
	virtual ibQueryableSourceDescriptor* FindDescriptor(const wxString& ns, const wxString& objectName) const;

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
	// Own registry first, then the global one — the same order Resolve keeps, for the same reason:
	// a configuration's sources are its own, and a miss falls through to what the platform declares.
	ibQueryableSourceDescriptor* FindDescriptor(const wxString& ns, const wxString& objectName) const override;
};

#endif
