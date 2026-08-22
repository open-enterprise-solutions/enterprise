////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : reference object
////////////////////////////////////////////////////////////////////////////

#include "reference.h"
#include "backend/system/value/valuePointInTime.h"   // the moment a reference can be asked for

#include "backend/metaData.h"
#include "backend/objCtor.h"   // ibCtorMetaValueType::GetMetaTypeCtor / ibCtorObjectMetaType_Reference — ConvertToMetaIds
#include "backend/metaCollection/partial/commonObject.h"
#include "backend/metaCollection/partial/tabularSection/tabularSection.h"

#include <vector>
#include <algorithm>
#include <utility>

#include "backend/session/session.h"   // ibSession::Current — the register lives on the session
#include "backend/diagnostics/journal.h"   // a read refused across sessions is said out loud
#include "backend/utils/debugTrace.h"      // ibDebugTraceEnabled — the register measurement is opt-in
#include <unordered_map>
#include <cstring>
//////////////////////////////////////////////////////////////////////
// ⭐⭐ ONE REFERENCE PER OBJECT, FOR AS LONG AS SOMEBODY HOLDS IT
//////////////////////////////////////////////////////////////////////
//
// A reference is an IDENTITY — this catalogue, this row — so two of them naming the same object are
// not two things, they are one thing counted twice. Before this they really were two: every cell of
// every list built its own, and each one went to the database for its own copy of the same row. The
// same nomenclature printed on forty lines was read forty times.
//
// So the creation door checks first: is this object already here? If it is, that one is returned and
// nothing is built — the row it already read serves every later holder for free.
//
// LIFETIME NEEDS NO POLICY, which is the part that makes this worth doing rather than a cache. A
// reference is reference-counted already; when the last holder lets go, its destructor runs and the
// object strikes itself from the registry. So the table holds exactly the LIVE references and never
// one more. A base with a billion rows does not mean a billion entries — only what a form, a report
// or a script is holding at this moment, which is the same population that would have existed
// anyway. The registry adds a key and a pointer per object, and nothing else.
//
// ⚠ HASHED, NOT SCANNED — and this is the whole difference from the attempt that stood here
// commented out for a year. That one walked the array on every creation: a thousand live references
// and a thousand more being made is a million comparisons, which is slower than reading the database
// it was meant to save. Looked up by key, the cost does not depend on how many are alive.
//
// ⚠ PER SESSION, NOT PER PROCESS. Two sessions read two different databases: sharing objects between
// them would hand one session another's row. Being per-session also means each thread works in its
// own table, so nothing here takes a lock — which is what would have made a shared registry cost
// more than it saves on the very path it exists for.
namespace {

// ⚠ THE KEY IS THE RAW IDENTITY, NOT ITS TEXT. Rendering the guid to a string would allocate on
// EVERY lookup — and a lookup happens wherever a reference is built, which is the busiest path in
// the engine. A register that allocates to decide whether it can save you a database read is a
// register that costs more than it saves. Sixteen bytes and a number, compared as they lie.
struct ibRefKey {
	ibMetaID    m_metaId;
	ibGuidImpl  m_guid;

	bool operator==(const ibRefKey& other) const {
		return m_metaId == other.m_metaId
		    && m_guid.m_data1 == other.m_guid.m_data1
		    && m_guid.m_data2 == other.m_guid.m_data2
		    && m_guid.m_data3 == other.m_guid.m_data3
		    && std::memcmp(m_guid.m_data4, other.m_guid.m_data4, sizeof(m_guid.m_data4)) == 0;
	}
};

struct ibRefKeyHash {
	std::size_t operator()(const ibRefKey& k) const {
		// The same fields, combined the same way ibValueReferenceDataObject::GetValueHash uses — one
		// notion of "which object is this" rather than two that could drift apart.
		std::uint64_t h = ibHashCombine(kIbHashBasis, static_cast<std::uint64_t>(k.m_metaId));
		h = ibHashCombine(h, k.m_guid.m_data1);
		h = ibHashCombine(h, k.m_guid.m_data2);
		h = ibHashCombine(h, k.m_guid.m_data3);
		for (const unsigned char byte : k.m_guid.m_data4)
			h = ibHashCombine(h, byte);
		return static_cast<std::size_t>(h);
	}
};

// Does this key name an object at all? An all-zero guid is the EMPTY reference — "a Catalogue.Goods,
// but no particular one" — and there is nothing to share about it: it has no row, every holder wants
// its own blank, and two of them are interchangeable anyway. So empty references never enter the
// table, which is also why the table's population is "objects being looked at", not "references alive".
//
// Read off the bytes rather than through ibGuid::isValid, which builds a guid to compare against.
// ibGuidImpl is a pinned 16-byte POD with no padding (see its static_assert), so this is exact.
bool NamesAnObject(const ibGuidImpl& guid)
{
	static const ibGuidImpl s_empty = {};
	return std::memcmp(&guid, &s_empty, sizeof(ibGuidImpl)) != 0;
}

// The table itself — one per session, created on first use and destroyed with the session.
struct ibReferenceTable {
	std::unordered_map<ibRefKey, ibValueReferenceDataObject*, ibRefKeyHash> m_live;
};

// The current session's table, made if this is the first reference it holds. Null when there is no
// session at all — bring-up, a tool, a unit test — and then every reference is built as it always
// was. A register that only sometimes exists is fine; a register that sometimes lies is not.
std::shared_ptr<ibReferenceTable> TableOfCurrentSession(bool createIfMissing)
{
	ibSession* const session = ibSession::Current();
	if (session == nullptr)
		return nullptr;
	return session->Local<ibReferenceTable>(createIfMissing);
}

// Is this identity already being read further up the stack? Defined further down with the read
// guard's own storage — the same anonymous namespace, split only by where that storage sits.
bool IsBeingRead(const ibMetaID& metaId, const ibGuid& guid);

}   // namespace

ibValueReferenceDataObject* ibReferenceRegistry::Find(const ibMetaID& id, const ibGuidImpl& objGuid)
{
	if (!NamesAnObject(objGuid))
		return nullptr;
	const std::shared_ptr<ibReferenceTable> table = TableOfCurrentSession(/*createIfMissing*/false);
	if (!table)
		return nullptr;
	// ⚠ NOT WHILE THIS IDENTITY IS BEING READ. A row's own attribute can name the row it belongs to —
	// a Parent pointing at itself, or A -> B -> A, the shapes ibRefReadGuard exists for. Materialising
	// that attribute asks here, and answering with the very object doing the reading would have it
	// store a strong pointer to itself: the count never reaches zero, the destructor never runs, and
	// the entry never leaves this table. The register's whole claim — it holds what is alive and not
	// one entry more — would fail on exactly the data it was written to survive.
	if (IsBeingRead(id, objGuid))
		return nullptr;

	const auto it = table->m_live.find(ibRefKey{ id, objGuid });
	if (it == table->m_live.end())
		return nullptr;

	// ⭐ A REUSE, SAID OUT LOUD. Beside the "read" line this is the whole measurement of the register:
	// reads are rows fetched, hits are asks answered by an object somebody already had. A burst with
	// many reads and no hits means nothing was being shared and the register is buying nothing there —
	// which is a fact worth having rather than an argument about how the mechanism ought to behave.
	//
	// ⚠ BEHIND A GATE, because this is the busiest path there is: unconditional, it renders a guid and
	// flushes a line to disk on every hit — measuring the thing by making it slower than it was.
	static const bool s_traceRefs = ibDebugTraceEnabled("OES_TRACE_REFS");
	if (s_traceRefs)
		ibJournalInfo(wxT("reference"), wxT("hit %s <%i>"), ibGuid(objGuid).str(), static_cast<int>(id));
	return it->second;
}

ibValueReferenceDataObject* ibReferenceRegistry::Find(const ibValueMetaObjectRecordDataRef* metaObject,
                                                       const ibGuidImpl& objGuid)
{
	if (metaObject == nullptr)
		return nullptr;
	return Find(static_cast<const ibValueMetaObject*>(metaObject)->GetMetaID(), objGuid);
}

void ibReferenceRegistry::Remember(ibValueReferenceDataObject* ref)
{
	// ⚠ THE FIELDS, NOT THE ACCESSORS. This runs from the CONSTRUCTOR, where a virtual call answers
	// for the class being built rather than for a derived one — so a future subclass overriding
	// GetMetaObject / GetGuid would be filed under the base's answer and found under its own, which
	// is a twin that never gets reused and never gets struck out. Reading the members is exact at
	// every point in the object's life. The register is a friend for this reason.
	const ibGuidImpl key = ref != nullptr ? ref->m_objGuid : ibGuidImpl{};
	if (ref == nullptr || ref->m_metaObject == nullptr || !NamesAnObject(key))
		return;
	const std::shared_ptr<ibReferenceTable> table = TableOfCurrentSession(/*createIfMissing*/true);
	if (!table)
		return;
	// ⚠ THE FIRST ONE KEEPS THE SLOT. A second object for one identity is rare but reachable — the
	// cycle case above builds one deliberately — and overwriting would unregister a reference that is
	// still alive, leaving it findable by nobody and its own Forget a no-op. The newcomer simply goes
	// unregistered, which is the ordinary state for a reference built before there was a session.
	table->m_live.emplace(ibRefKey{ ref->m_metaObject->GetMetaID(), key }, ref);

	// ⭐ THE REFERENCE KEEPS ITS OWN TABLE, not a way to find one later. A value can travel — into a
	// background job, into another session's call — and be released there; asking "which session is
	// current?" at that moment would erase from the wrong table and leave this one pointing at freed
	// memory. Holding the table (not the session) also means it cannot vanish underneath: the last
	// reference out keeps it alive to be struck from.
	ref->m_registryTable = table;
}

void ibReferenceRegistry::Forget(const ibValueReferenceDataObject* ref)
{
	// Fields again, and here it is not a precaution but a requirement: this runs from the DESTRUCTOR,
	// where the derived part is already gone and a virtual call is undefined behaviour.
	if (ref == nullptr || !ref->m_registryTable || ref->m_metaObject == nullptr)
		return;
	const std::shared_ptr<ibReferenceTable> table =
		std::static_pointer_cast<ibReferenceTable>(ref->m_registryTable);

	// ⚠ ONLY IF IT IS STILL MINE. A second reference to the same object can exist beside this one: one
	// born before there was a session registered nowhere, and a later one, made once a session existed,
	// holds the entry. Erasing by key alone would then remove the LIVING one and leave the table
	// pointing at freed memory — the very failure the register is here to make impossible.
	const auto it = table->m_live.find(ibRefKey{ ref->m_metaObject->GetMetaID(), ref->m_objGuid });
	if (it != table->m_live.end() && it->second == ref)
		table->m_live.erase(it);
}
// Re-entrancy guard for the eager reference read. A self / cyclic reference (a document
// field pointing at the same record, or A -> B -> A) makes ReadData recurse: it reads a
// reference field, whose eager PrepareRef calls ReadData again, forever -> stack overflow.
// While a ref identity (type + guid) is already being read up the stack, the nested re-read
// is skipped (the field keeps its key, loads lazily on demand). A self-reference is a LEGAL
// config, so this must terminate rather than crash.
//
// A thread-local stack of identities (each worker reads on its own call stack): push on
// entry, pop on exit (RAII — correct across the FB exceptions ReadData can throw). A linear
// scan over a depth that is tiny in practice — negligible next to the DB query the read
// itself issues.
//
// A FIXED array, not a vector, and the difference is not micro-optimisation: a thread-local
// vector allocates once per thread and hands the block back only when the thread ends
// normally. A thread still running at process exit is killed without TLS teardown, so it
// keeps the block — measured 2026-07-30 as six leaked buffers, one per surviving thread.
// A fixed buffer has nothing to hand back. It is also trivially destructible, so no TLS
// destructor is registered at all.
namespace {
	// Depth beyond this means a reference graph nested deeper than any real config; the guard
	// then stops tracking rather than growing. Cycle detection degrades to "not detected" for
	// those levels, which is the same answer it gave before any of them were pushed.
	constexpr std::size_t kRefReadDepthMax = 64;

	thread_local std::pair<ibMetaID, ibGuid> g_refReadStack[kRefReadDepthMax];
	thread_local std::size_t                 g_refReadDepth = 0;

	struct ibRefReadGuard {
		bool m_cycle = false;
		bool m_pushed = false;

		ibRefReadGuard(const ibMetaID& metaId, const ibGuid& guid) {
			const std::pair<ibMetaID, ibGuid> key{ metaId, guid };
			m_cycle = std::find(g_refReadStack, g_refReadStack + g_refReadDepth, key)
				!= g_refReadStack + g_refReadDepth;
			if (g_refReadDepth < kRefReadDepthMax) {
				g_refReadStack[g_refReadDepth++] = key;
				m_pushed = true;
			}
		}
		~ibRefReadGuard() { if (m_pushed) --g_refReadDepth; }
		bool Cycle() const { return m_cycle; }   // true == this identity is already being read up the stack
	};

	bool IsBeingRead(const ibMetaID& metaId, const ibGuid& guid) {
		const std::pair<ibMetaID, ibGuid> key{ metaId, guid };
		return std::find(g_refReadStack, g_refReadStack + g_refReadDepth, key)
			!= g_refReadStack + g_refReadDepth;
	}
}


//**********************************************************************************************
//*                                     reference                                              *        
//**********************************************************************************************
//**********************************************************************************************

void ibValueReferenceDataObject::PrepareRef(bool createData)
{
	wxASSERT(m_metaObject != nullptr);

	if (m_initializedRef)
		return;

	// ⭐⭐ ONLY ITS OWN SESSION MAY READ INTO IT. There is one reference object per identity per
	// session, and what it has read is subject to THAT user's rights: a row he may not see reads as
	// "not found", which is deliberate and indistinguishable from a deleted one.
	//
	// A value can still carry the OBJECT across in-process — into a background job's closure, say —
	// and then one object would serve two sets of rights. The session that may not see the row would
	// read `false` into it, and the session that MAY would afterwards show "not found" for something
	// plainly in front of it. One borrowed pointer breaks the reference for the user who is entitled
	// to it, which is the worse of the two outcomes by far, so the read is refused rather than shared.
	//
	// Refusing costs the borrower nothing it is owed: the road for a reference between sessions is
	// serialisation, where it travels as type + guid and is REBUILT on the far side as that session's
	// own object, read under that session's rights. Only a raw pointer handed over lands here, and
	// that is a defect at the handing-over — said out loud, with the object it happened on.
	if (m_registryTable && m_registryTable.get() != TableOfCurrentSession(false).get()) {
		ibJournalInfo(wxT("reference"), wxT("refused: read attempted from a session other than the one it belongs to <%i:%s>"),
			m_metaObject->GetMetaID(), m_objGuid.str());
		return;
	}

	if (ibValueReferenceDataObject::IsEmpty()) {
		//attrbutes can refValue 
		for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
			if (object->IsDeleted())
				continue;
			if (!m_metaObject->IsDataReference(object->GetMetaID())) {
				m_listObjectValue.insert_or_assign(object->GetMetaID(), object->CreateValue());
			}
		}
		// table is collection values
		for (const auto object : m_metaObject->GetTableArrayObject()) {
			if (object->IsDeleted())
				continue;
			m_listObjectValue.insert_or_assign(object->GetMetaID(),
				new ibValueTabularSectionDataObjectRef(this, object));
		}
	}
	else {
		// Break self / cyclic references: read this identity only if it is not already
		// being read up the stack (otherwise leave it lazy — no infinite recursion).
		ibRefReadGuard guard(m_metaObject->GetMetaID(), m_objGuid);
		if (!guard.Cycle() && ibValueReferenceDataObject::ReadData(createData)) {
			m_foundedRef = true; m_newObject = false;
		}
	}

	if (createData) {
		m_initializedRef = true;
	}
	// Name surface is lazy (FillMembers built on first GetPMethods).
}

ibValueReferenceDataObject::ibValueReferenceDataObject(const ibValueMetaObjectRecordDataRef* metaObject, const ibGuid& objGuid) : ibValueDynamicMembers(ibValueTypes::TYPE_VALUE, true), ibValueDataObject(objGuid, !objGuid.isValid()),
m_initializedRef(false), m_metaObject(metaObject), m_reference_impl(nullptr), m_foundedRef(false)
{
	m_members.Bind(this, &ibValueReferenceDataObject::FillMembers);
	// The stored key (_RRRef) is the pure object guid; the type is carried separately (metaObject / _RTRef).
	// An unset reference is simply an empty guid — no normalization needed.
	m_reference_impl = new ibReference(m_objGuid);

	// ⭐ REGISTERED HERE, not in the doors. Every reference is born through this constructor —
	// all three Create overloads, and the value-ctor registry through them — so one line covers every
	// way of making one, and there is never a second object for an identity the register already
	// holds. Registered per door instead, one door forgotten is a twin nobody ever finds again.
	ibReferenceRegistry::Remember(this);
}

// GetHashKey is gone (2026-08-15). A reference's identity is carried by CompareValueLS (guid, then
// metaID) and GetValueHash (the guid's bytes) — the same (metaID + guid) the database keys by, said
// once, in the two methods every hash container already asks. See the note in reference.h.

// Ordering: GUID first, then TYPE (metaID) as the tiebreak — see the header. m_metaObject is complete in this
// TU, so GetMetaID() resolves. The tiebreak only fires on equal guids (in practice: empty references, which all
// share the all-zero guid), keeping LS==0 exactly when CompareValueEQ is true.
int ibValueReferenceDataObject::CompareValueLS(const ibValue& cParam) const
{
	ibValueReferenceDataObject* rhs = dynamic_cast<ibValueReferenceDataObject*>(cParam.GetRef());
	if (rhs == nullptr)
		return 0;
	if (m_objGuid < rhs->m_objGuid) return -1;
	if (rhs->m_objGuid < m_objGuid) return 1;
	const ibMetaID lm = m_metaObject      != nullptr ? m_metaObject->GetMetaID()      : 0;
	const ibMetaID rm = rhs->m_metaObject != nullptr ? rhs->m_metaObject->GetMetaID() : 0;
	if (lm < rm) return -1;
	if (rm < lm) return 1;
	return 0;   // same guid AND same type -> the same reference
}

ibValueReferenceDataObject::~ibValueReferenceDataObject()
{
	wxDELETE(m_reference_impl);
	ibReferenceRegistry::Forget(this);   // the last owner let go — see the note above the registry
}

// THE UPCAST, WHERE BOTH TYPES ARE COMPLETE. In the header they are not (see the note on the
// declaration), and a C-style cast there was a reinterpret_cast wearing a plainer suit — no base
// adjustment, correct only while RecordData stays the FIRST base of RecordDataRef. Here the compiler
// computes the offset, so the base order is free to change without silently returning a wrong object.
const ibValueMetaObjectRecordData* ibValueReferenceDataObject::GetMetaObject() const
{
	return static_cast<const ibValueMetaObjectRecordData*>(m_metaObject);
}

// Read as much of the row as the caller asked for, and no more. The whole of what the three old
// extra names encoded, now that the axis has one.
//
// ⚠ ON A LIVE REFERENCE THIS IS BEST-EFFORT, and correctly so. Ask for Unlatched and get one that is
// already latched, and PrepareRef returns at once: somebody else settled it, and there is one of it.
// That is the register working — not a mode being ignored — because "unlatched" was never a property
// of a request, only of an object, and the object has an answer already.
static ibValueReferenceDataObject* ReadAsAsked(ibValueReferenceDataObject* reference, ibReferenceLoad load)
{
	if (reference != nullptr) {
		if (load == ibReferenceLoad::Unlatched)
			reference->PrepareRef(false);
		else if (load == ibReferenceLoad::Latched)
			reference->PrepareRef(true);
	}
	return reference;
}

// THE BODY. Everything else resolves a type and comes here.
ibValueReferenceDataObject* ibValueReferenceDataObject::Create(const ibValueMetaObjectRecordDataRef* metaObject,
                                                               const ibGuid& objGuid, ibReferenceLoad load)
{
	if (metaObject == nullptr)
		return nullptr;

	// Already alive in this session? Then it IS the reference to this object, row and all.
	if (ibValueReferenceDataObject* const live = ibReferenceRegistry::Find(metaObject, objGuid))
		return ReadAsAsked(live, load);

	// The constructor puts it in the register — see the note there.
	return ReadAsAsked(new ibValueReferenceDataObject(metaObject, objGuid), load);
}

ibValueReferenceDataObject* ibValueReferenceDataObject::Create(const ibMetaData* metaData, const ibMetaID& id,
                                                               const ibGuid& objGuid, ibReferenceLoad load)
{
	// ⭐ ASK BEFORE RESOLVING. The table is keyed by the identifier, which is what this caller holds,
	// so a hit answers without touching the metadata at all. Searching for the metaobject first would
	// spend a metadata lookup to obtain something the live reference is already holding.
	if (ibValueReferenceDataObject* const live = ibReferenceRegistry::Find(id, objGuid))
		return ReadAsAsked(live, load);

	if (metaData == nullptr)
		return nullptr;
	return Create(metaData->FindAnyObjectByFilter<ibValueMetaObjectRecordDataRef>(id), objGuid, load);
}

// clsid (the _RTRef target type) -> its reference metaObject, through the class factory. The type comes
// from the column, never from the key bytes (the _RRRef blob is pure identity now).
static const ibValueMetaObjectRecordDataRef* MetaObjectFromClsid(const ibMetaData* metaData, const ibClassID& clsid)
{
	const ibCtorMetaValueType* typeCtor = metaData != nullptr ? metaData->GetTypeCtor(clsid) : nullptr;
	if (typeCtor == nullptr || typeCtor->GetMetaTypeCtor() != ibCtorObjectMetaType::ibCtorObjectMetaType_Reference)
		return nullptr;
	return dynamic_cast<const ibValueMetaObjectRecordDataRef*>(typeCtor->GetMetaObject());
}

ibValueReferenceDataObject* ibValueReferenceDataObject::Create(const ibMetaData* metaData, const ibClassID& refClsid,
                                                               void* ptr, ibReferenceLoad load)
{
	const ibReference* const reference = static_cast<const ibReference*>(ptr);
	if (reference == nullptr)
		return nullptr;

	// ⭐⭐ THE CHEAPEST QUESTION THIS DOOR CAN ASK, and it is the one that runs once per reference cell
	// of every list and report. A reference clsid is CONSTRUCTIVE — its body IS the metaID — and the
	// _RRRef blob IS the raw key. So both halves of the table's key are already in hand, straight off
	// the stored row: a hash probe, with no ibMetaData search and no type-ctor lookup. Only a miss
	// pays for resolving the metaobject, and only a miss needs one.
	if (::IsReference(refClsid))   // the free clsid classifier — ibValue has a same-named member that hides it
		if (ibValueReferenceDataObject* const live = ibReferenceRegistry::Find(
				static_cast<ibMetaID>(metaID_from_clsid(refClsid)), reference->m_guid))
			return ReadAsAsked(live, load);

	return Create(MetaObjectFromClsid(metaData, refClsid), reference->m_guid, load);
}

bool ibValueReferenceDataObject::SetValueByMetaID(const ibMetaID& id, const ibValue& varMetaVal)
{
	return false;
}

bool ibValueReferenceDataObject::GetValueByMetaID(const ibMetaID& id, ibValue& pvarMetaVal) const
{
	if (m_metaObject->IsDataReference(id)) {
		if (!ibValueReferenceDataObject::IsEmpty()) {
			pvarMetaVal = ibValueReferenceDataObject::Create(m_metaObject, m_objGuid);
			return true;
		}
		pvarMetaVal = ibValueReferenceDataObject::Create(m_metaObject);
		return true;
	}

	auto it = m_listObjectValue.find(id);
	//wxASSERT(it != m_listObjectValue.end());
	if (it != m_listObjectValue.end()) {
		pvarMetaVal = it->second;
		return true;
	}
	return false;
}

// The reference's own hop gate — OUT-OF-LINE because it needs the referenced metaobject COMPLETE to look up the
// field's live type. Reads the id, then validates the pin against that type via CoerceHopType — a composite
// field's UNDEFINED resolves to the pinned twin, a field retyped away from the pin would not.
bool ibValueReferenceDataObject::GetValueBySourceHop(const ibSourceHop& hop, ibValue& out) const
{
	const bool got = GetValueByMetaID(hop.m_id, out);
	const ibValueMetaObjectAttributeBase* attribute = GetMetaObject()->FindAnyAttributeObjectByFilter(hop.m_id);
	return CoerceHopType(hop, out, attribute != nullptr ? attribute->GetTypeDesc() : ibTypeDescription(), GetSourceMetaData()) || got;
}

// The pinned-type twin materialiser — STATIC (see the header). `metaData` comes from the CALLER's own source
// (GetSourceMetaData), so ibSourceDataObject stays metadata-free; the reference — already metadata-bound —
// owns the creation. A live value already of the pinned type passes through untouched.
bool ibValueReferenceDataObject::CoerceHopType(const ibSourceHop& hop, ibValue& out, const ibTypeDescription& filter, const ibMetaData* metaData)
{
	if (!::IsReference(hop.m_type))
		return false;   // no pinned reference branch — keep whatever the id primitive gave
	// STALE-pin guard: if the field carries a type filter, the pin must be among its clsids. A value-table
	// column RETYPED in the designer leaves an OLD pin on a bound path — do NOT fabricate the old twin (else the
	// dead path keeps resolving as a phantom reference). An EMPTY filter (metadata-fixed field) skips the check.
	if (filter.GetClsidCount() > 0 && !filter.ContainType(hop.m_type))
		return false;
	ibSourceDataObject* live = nullptr;
	out.ConvertToValue<ibSourceDataObject>(live);
	if (live != nullptr && live->GetSourceClassType() == hop.m_type)
		return false;   // already the pinned type — never fabricate over a real value
	const std::vector<ibMetaID> pin = ConvertToMetaIds({ hop.m_type }, metaData);   // metadata decode, NOT a body-mask
	if (pin.empty())
		return false;   // pin is not a resolvable reference (no metaData / bad pin)
	ibValue twin = ibValueReferenceDataObject::Create(metaData, pin.front());
	ibSourceDataObject* tw = nullptr;
	twin.ConvertToValue<ibSourceDataObject>(tw);
	if (tw == nullptr)
		return false;   // couldn't build the twin
	out = twin;
	return true;
}

// Reference clsids → their TARGET metaobject ids, resolved through the class factory: a clsid's type ctor must
// be a REFERENCE ctor (GetMetaTypeCtor == _Reference); its metaobject's metaID is the target. metaData-driven —
// the kind-byte shortcut mis-classified composite branches (a clsid that is not a constructive reference id).
// Non-reference clsids (a list / object / primitive branch) are skipped. Pickers call it to enumerate a
// COMPOSITE reference's branches.
std::vector<ibMetaID> ibValueReferenceDataObject::ConvertToMetaIds(const std::vector<ibClassID>& clsids, const ibMetaData* metaData)
{
	std::vector<ibMetaID> targets;
	if (metaData == nullptr)
		return targets;
	for (const ibClassID& clsid : clsids) {
		const ibCtorMetaValueType* typeCtor = metaData->GetTypeCtor(clsid);
		if (typeCtor == nullptr || typeCtor->GetMetaTypeCtor() != ibCtorObjectMetaType::ibCtorObjectMetaType_Reference)
			continue;
		const ibValueMetaObject* metaObj = typeCtor->GetMetaObject();
		if (metaObj != nullptr)
			targets.push_back(metaObj->GetMetaID());
	}
	return targets;
}


void ibValueReferenceDataObject::ShowValue()
{
	ibValueMetaObjectRecordDataMutableRef* metaObject = nullptr;
	if (m_metaObject->ConvertToValue(metaObject)) {
		ibValueRecordDataObject* objValue = nullptr;
		if (metaObject != nullptr && m_objGuid.isValid())
			objValue = metaObject->CreateObjectValue(m_objGuid);
		else
			objValue = metaObject->CreateObjectValue();
		if (objValue != nullptr)
			objValue->ShowFormValue();
	}
}

ibValueRecordDataObjectRef* ibValueReferenceDataObject::GetObject() const
{
	ibValueMetaObjectRecordDataMutableRef* metaObject = nullptr;
	if (m_metaObject->ConvertToValue(metaObject)) {
		if (m_newObject)
			return metaObject->CreateObjectValue();
		return metaObject->CreateObjectValue(m_objGuid);
	}
	return nullptr;
}

#include "backend/objCtor.h"

ibClassID ibValueReferenceDataObject::GetClassType() const
{
	const ibCtorMetaValueType* clsFactory =
		m_metaObject->GetTypeCtor(ibCtorObjectMetaType::ibCtorObjectMetaType_Reference);
	wxASSERT(clsFactory);
	return clsFactory->GetClassType();
}

const ibValueMetaObjectGenericData* ibValueReferenceDataObject::GetSourceMetaObject() const
{
	return GetMetaObject();   // RecordData* -> GenericData* (RecordData : GenericData; complete types in this TU)
}

const ibMetaData* ibValueReferenceDataObject::GetSourceMetaData() const
{
	const ibValueMetaObjectRecordData* mo = GetMetaObject();
	return mo != nullptr ? mo->GetMetaData() : nullptr;
}

// A reference vends its TARGET type's columns into the inherited owner-bound m_sourceExplorer — the
// recursion FUEL: hop into a reference VALUE, get THIS explorer, descend by id. Built from the
// referenced metaobject (no DB read; the referenced record's own attributes — scalar dot-walk targets).
const ibSourceExplorer* ibValueReferenceDataObject::GetSourceExplorer() const
{
	const ibValueMetaObjectRecordData* metaObject = GetMetaObject();
	if (metaObject == nullptr)
		return nullptr;   // unresolved / empty reference — no target type to describe, so the hop stops here
	m_sourceExplorer.Reset(wxT("Ref"), _("Ref"), metaObject->GetMetaID(), GetClassType(), false, false);
	for (const auto object : metaObject->GetGenericAttributeArrayObject())
		m_sourceExplorer.AppendColumn(object);
	return &m_sourceExplorer;
}

wxString ibValueReferenceDataObject::GetString() const
{
	if (m_newObject)
		return wxEmptyString;
	else if (!m_foundedRef)   // deleted, or refused by access policy — the same answer on purpose
		return wxString::Format(wxT("%s <%i:%s>"), _("Not found"), m_metaObject->GetMetaID(), m_objGuid.str());

	wxASSERT(m_metaObject);
	return m_metaObject->GetDataPresentation(this);
}

wxString ibValueReferenceDataObject::GetClassName() const
{
	const ibCtorMetaValueType* clsFactory =
		m_metaObject->GetTypeCtor(ibCtorObjectMetaType::ibCtorObjectMetaType_Reference);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

//****************************************************************************
//*                              Support methods                             *
//****************************************************************************

enum Func {
	enIsEmpty = 0,
	enGetMetadata,
	enGetObject,
	enGetGuid,
	enPointInTime
};

void ibValueReferenceDataObject::FillMembers(ibMemberTable& helper) const
{
	ibValueMetaObjectRecordDataMutableRef* metaObject = nullptr;
	if (m_metaObject->ConvertToValue(metaObject)) {

		helper.AppendFunc(wxT("IsEmpty"), wxT("IsEmpty()"));
		helper.AppendFunc(wxT("GetMetadata"), wxT("GetMetadata()"));
		helper.AppendFunc(wxT("GetObject"), wxT("GetObject()"));
		helper.AppendFunc(wxT("GetGuid"), wxT("GetGuid()"));
		// ⭐ THE MOMENT COMES WITH THE REFERENCE, for the families that can HAVE one: a catalog
		// element, a document, a chart. It arrives already assembled, so nobody writes
		// `New PointInTime(doc.Date, doc.Ref)` by hand and gets the pair wrong. An enumeration value
		// is not offered it at all — it has no place in the data's history to point at.
		helper.AppendFunc(wxT("PointInTime"), wxT("PointInTime()"));

		wxString objectName;

		//fill custom attributes
		for (const auto object : metaObject->GetGenericAttributeArrayObject()) {
			if (object->IsDeleted())
				continue;
			if (!object->GetObjectNameAsString(objectName))
				continue;
			helper.AppendProp(
				objectName,
				true,
				false,
				object->GetMetaID(),
				eProperty
			);
		}

		//fill custom tables
		for (const auto object : metaObject->GetTableArrayObject()) {
			if (object->IsDeleted())
				continue;
			if (!object->GetObjectNameAsString(objectName))
				continue;
			helper.AppendProp(
				objectName,
				true,
				false,
				object->GetMetaID(),
				eTable
			);
		}
	}
	else {
		helper.AppendFunc(wxT("IsEmpty"), wxT("IsEmpty()"));
		helper.AppendFunc(wxT("GetMetadata"), wxT("GetMetadata()"));
	}
}

bool ibValueReferenceDataObject::SetPropVal(const long lPropNum, const ibValue& value)
{
	return false;
}

bool ibValueReferenceDataObject::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	const long lPropAlias = m_members.GetPropAlias(lPropNum);
	ibValueReferenceDataObject::PrepareRef();
	const ibMetaID& id = m_members.GetPropData(lPropNum);
	if (!m_metaObject->IsDataReference(id)) {
		if (lPropAlias == eTable && !GetValueByMetaID(id, pvarPropVal)) {
			m_listObjectValue.insert_or_assign(id,
				new ibValueTabularSectionDataObjectRef(this, m_metaObject->FindTableObjectByFilter(id), !m_newObject)
			);
		}
		if (lPropAlias == eTable && GetValueByMetaID(id, pvarPropVal)) {
			ibValueTabularSectionDataObjectRef* tabularSection = nullptr;
			if (pvarPropVal.ConvertToValue(tabularSection)) {
				if (tabularSection->IsReadAfter()) {
					if (!tabularSection->LoadData(m_objGuid, true)) {
						pvarPropVal.Reset();
						return false;
					}
				}
				return true;
			}
			pvarPropVal.Reset();
			return false;
		}
		return GetValueByMetaID(id, pvarPropVal);
	}

	if (!ibValueReferenceDataObject::IsEmpty()) {
		pvarPropVal = ibValueReferenceDataObject::Create(m_metaObject, m_objGuid);
		return true;
	}

	pvarPropVal = ibValueReferenceDataObject::Create(m_metaObject);
	return true;
}

bool ibValueReferenceDataObject::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case enIsEmpty:
		pvarRetValue = IsEmptyRef();
		return true;
	case enGetMetadata:
		pvarRetValue = GetMetaObject();
		return true;
	case enGetObject:
		pvarRetValue = GetObject();
		return true;
	case enGetGuid:
		pvarRetValue = new ibValueGuid(m_objGuid);
		return true;
	case enPointInTime: {
		// A REFERENCE'S MOMENT IS ITS IDENTITY; the DATE is added by the family that has one (the
		// document overrides this method with its own date). No virtual asking every class "do you
		// have a date" and no list of the ones that do -- the two classes each answer for themselves.
		//
		// It carries THIS reference, not a copy: `GetValue(true)` is the verb for handing out this
		// very value, where a second ibValueReferenceDataObject over the same (type, guid) would be
		// a second object for one identity -- the thing a reference exists to prevent.
		pvarRetValue = new ibValuePointInTime(wxDateTime(), GetValue(true));
		return true;
	}
	}

	return false;
}
////////////////////////////////////////////////////////////////////////////
// Serialization — a reference travels as IDENTITY
////////////////////////////////////////////////////////////////////////////
//
// The header already carries the type, so the contents are just the guid. NOT
// the object: an object belongs to a session and a connection, and the far side
// re-reads it under its OWN rights — which is exactly why a job is handed a
// reference rather than a loaded object.
//
// An empty reference writes an empty guid and comes back as an empty reference
// OF THAT TYPE, not as an untyped nothing: a reference that has not been filled
// in is still a reference, and the far side must be able to compare and assign
// it without a type error.

#include "backend/serialize/dataBuilder.h"

bool ibValueReferenceDataObject::DoSerialize(ibDataNode& node) const
{
	// THE metaID, not only the class id.
	//
	// A reference's class id is DERIVED from the metaobject's metaID, so within
	// one configuration either identifies the type. Across configurations they
	// part company: the same catalog can carry a different id in a copy of the
	// base, and a stored setting or an exchange parcel then restores a reference
	// pointing at whatever type happens to hold that id — silently, and to the
	// wrong table.
	//
	// Writing the metaID as well makes the identity say what it means: this type,
	// of this metaobject. The class id stays in the header for the fast path;
	// this is what a reader consults when the fast path is not enough.
	if (m_metaObject != nullptr)
		node.SetValue(wxT("m"), (s32)m_metaObject->GetMetaID());

	node.SetValue(wxT("g"), m_reference_impl != nullptr
		? ibGuid(m_reference_impl->m_guid).str()
		: wxString());
	return true;
}

bool ibValueReferenceDataObject::DoDeserialize(const ibDataNode& node)
{
	if (m_reference_impl == nullptr)
		return false;

	// THE metaID DECIDES, when it was written. We were created from the class id
	// in the header, which is the fast path and is enough within one base; this
	// is the check that the type we were created as is the type that was stored.
	// A mismatch means the id landed on a different metaobject in this base —
	// refuse rather than hand back a reference into the wrong table.
	//
	// Absent (0) is not a mismatch: it is a value written before the metaID went
	// into the payload, and the class id is all it ever had.
	const ibMetaID storedMetaId = (ibMetaID)node.GetValue<s32>(wxT("m"));
	if (storedMetaId != 0 && m_metaObject != nullptr
		&& storedMetaId != m_metaObject->GetMetaID())
		return false;

	// A malformed guid reads as the empty one rather than raising: the header
	// was well formed, so this is data from a base that knew something we do
	// not — degrade, do not fail the whole read.
	const ibGuid restored(node.GetValue<wxString>(wxT("g")));
	m_reference_impl->m_guid = restored;

	// AND THE IDENTITY THE REST OF THE CLASS READS. Writing only the impl left the object
	// still calling itself NEW (it was constructed empty, from the class id in the header,
	// and m_newObject was decided there): the guid was right, so the reference filtered,
	// compared and saved correctly — and presented as an EMPTY STRING, because GetString
	// answers "" for a new object before it ever looks anything up. A stored list filter
	// came back with its value invisible while plainly still in force.
	//
	// The init flags are cleared first: this object may already have "prepared" itself as the
	// empty one it was a moment ago, and PrepareRef returns early on that flag. Then it prepares
	// for real — the same step Create(metaObject, guid) takes, and the step that decides whether
	// the identity is FOUND. Without it the value would read "Not found" instead of its name,
	// which is the same defect wearing different clothes.
	m_objGuid = restored;
	m_newObject = !restored.isValid();
	m_initializedRef = false;
	m_foundedRef = false;
	PrepareRef(true);
	return true;
}
