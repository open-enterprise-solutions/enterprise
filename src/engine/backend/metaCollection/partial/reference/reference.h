#ifndef _REFERENCE_H__
#define _REFERENCE_H__

#include "backend/compiler/value.h"
#include "backend/valueInfo.h"
#include "backend/srcDataObject.h"   // ibSourceDataObject base — a reference IS a source object (vends its target's explorer)
#include "backend/typeDescription.h"   // ibTypeDescription — CoerceHopType validates the pin against a field's CURRENT type filter

//********************************************************************************************
//*                                     Defines                                              *
//********************************************************************************************

class BACKEND_API ibMetaData;

//********************************************************************************************

class BACKEND_API ibValueMetaObjectRecordDataRef;
class BACKEND_API ibValueRecordDataObjectRef;

//********************************************************************************************

// ⭐ HOW MUCH OF THE ROW IS WANTED NOW — the one axis the creation doors actually differ on, said as
// a type instead of as a function name and a bool. Three names used to carry this: `CreateRaw` read
// nothing, `CreateFromPtr` read without settling, `Create` read and settled — and none of the three
// names mentioned reading at all.
enum class ibReferenceLoad {
	// ⭐⭐ THE DEFAULT: read nothing now; the row loads the first time somebody asks what this
	// reference IS (Max, 2026-08-24: "we made every reference read nothing by default"). Creating a
	// reference is stating an IDENTITY — this catalogue, this row — and an identity costs no query.
	//
	// It was `Latched` until then, so everyone paid for a read whether or not they wanted the row:
	// the field pickers create a reference PER TARGET TYPE purely to ask what fields it has, with an
	// EMPTY guid, and every one of those went to the database for a row that does not exist. That is
	// what "the picker lags" was.
	//
	// REQUIRED on the value-ctor path (ibCtorMetaValueTypeReference::CreateObject): reading there
	// recurses — the row's attributes are themselves references, re-entering CreateObject -> stack
	// overflow.
	OnDemand,

	// Read the row now, but do not mark the reference settled: a later PrepareRef reads it again.
	Unlatched,

	// Read the row now and mark it settled — later PrepareRef calls are no-ops.
	Latched
};


//********************************************************************************************


//********************************************************************************************

class BACKEND_API ibValueReferenceDataObject : public ibValueDynamicMembers,
	public ibValueDataObject, public ibSourceDataObject {
	public:
private:
	enum helperAlias {
		eProperty,
		eTable
	};
private:
	ibValueReferenceDataObject() : ibValueDynamicMembers(ibValueTypes::TYPE_VALUE, true), m_initializedRef(false) {
		m_members.Bind(this, &ibValueReferenceDataObject::FillMembers);
	}
	ibValueReferenceDataObject(const ibValueMetaObjectRecordDataRef* metaObject, const ibGuid& objGuid = wxNullGuid);
public:

	// A NEW reference value's key is a PLAIN unique guid — no encoding, no type baked in. The type is carried
	// separately (the _RTRef column / this reference's metaObject), so the key is pure identity: an all-zero
	// guid is an unset reference, a full random guid is a real one. (Minted in commonObject.cpp's new-object
	// branch via wxNewUniqueGuid — see ibValueRecordDataObjectRef.)

	ibReference* GetReferenceData() const {
		return m_reference_impl;
	}

	// IDENTITY = type + guid, and it is expressed by the COMPARISON, not by a rendered key.
	// CompareValueLS below orders by guid then by metaID (the _RTRef + _RRRef pair the database uses),
	// and GetValueHash buckets by the guid — so grouping, join and dedup over a reference line up with
	// the DB key exactly as they did through the old GetHashKey string, without building one.
	// NOT the guid alone: a reference COLUMN can target several types, so the type disambiguates two
	// refs that share a guid — which the comparison keeps and the hash resolves in the bucket.

	void PrepareRef(bool createData = true);

	virtual ~ibValueReferenceDataObject();

	// ⭐ ONE WAY TO MAKE A REFERENCE, ASKED THREE WAYS. The overloads differ ONLY in how the caller
	// happens to name the type — a metaobject in hand, a metaID, or the clsid stored in a row — and
	// they all end in the same body. How much is READ is the `load` argument, never the function name.
	//
	// There were five names before, and they encoded the two axes on top of each other: `CreateRaw`
	// and `CreateFromPtr` were named after WHERE THEIR ARGUMENTS CAME FROM while actually differing
	// in HOW DEEPLY THEY READ, and plain `Create` meant "read and settle" in two overloads and "read
	// nothing" in a third. The verb said nothing about what happened, so the only way to know was to
	// open the body — which is what a name is for.
	//
	// The row is read HERE, at creation, and the getters find it already in hand — which is what lets
	// GetString and GetValueByMetaID stay const without casting anything away. OnDemand is for the
	// callers that must not read yet (the value-ctor path, which would recurse) and for the ones that
	// only ever wanted the key.
	static ibValueReferenceDataObject* Create(const ibValueMetaObjectRecordDataRef* metaObject,
	                                          const ibGuid& objGuid = wxNullGuid,
	                                          ibReferenceLoad load = ibReferenceLoad::OnDemand);

	static ibValueReferenceDataObject* Create(const ibMetaData* metaData, const ibMetaID& id,
	                                          const ibGuid& objGuid = wxNullGuid,
	                                          ibReferenceLoad load = ibReferenceLoad::OnDemand);

	// Reconstruct from a stored row: `refClsid` is the _RTRef target type, `ptr` the pure _RRRef guid blob.
	// The type comes from the column (clsid -> metaObject), NOT from the key bytes.
	//
	// ⚠ NO DEFAULT LOAD HERE, deliberately. This overload used to read nothing while its two siblings
	// read everything, all four under the name `Create` — so a caller reading the header saw one verb
	// and got two behaviours. Its three callers now say which they want.
	static ibValueReferenceDataObject* Create(const ibMetaData* metaData, const ibClassID& refClsid,
	                                          void* ptr, ibReferenceLoad load);

	// The pinned-type twin materialiser for the dot-walk — STATIC, on the REFERENCE (the side that knows how to
	// build one, and where the metaData coupling BELONGS). If `out` is not already the pinned reference type (a
	// composite field's UNDEFINED, or a different target), replace it with an empty typed twin of the pin, built
	// from `metaData`. Returns true iff a twin was substituted. A source's own GetValueBySourceHop calls this
	// with its GetSourceMetaData() — so ibSourceDataObject itself stays free of metadata / reference creation.
	// `filter`: the field's CURRENT declared type. If non-empty, the pin must be among its clsids (ContainType) —
	// a value-table column RETYPED in the designer leaves a stale pin, and CoerceHopType must NOT fabricate the
	// old twin over a now-dead path. A METADATA-fixed field (record / reference gate — its type cannot be
	// retyped at runtime) passes an EMPTY filter and skips the check.
	static bool CoerceHopType(const ibSourceHop& hop, ibValue& out, const ibTypeDescription& filter, const ibMetaData* metaData);

	// Reference clsids → their TARGET metaobject ids, resolved through the class factory (each clsid's type
	// ctor must be a REFERENCE ctor; its metaobject's metaID is the target). metaData-driven — the kind-byte
	// shortcut mis-classified composite branches. Non-reference clsids are skipped. Pickers call it to
	// enumerate a COMPOSITE reference's branches.
	static std::vector<ibMetaID> ConvertToMetaIds(const std::vector<ibClassID>& clsids, const ibMetaData* metaData);

	// Ordering primitive (three-way) — the base virtual GT/GE/LE and operator< all derive from this, so this
	// ONE method tunes all four for references. Orders by the GUID first, then the TYPE (metaID) as a tiebreak.
	// The type is NEEDED again now the guid is pure: without it two references with the same guid but a
	// different target type order-equal — and EVERY empty reference has an all-zero guid, so type is the ONLY
	// thing telling an empty ref of type A from one of type B. Keeping LS==0 in lockstep with CompareValueEQ
	// (type + guid) is what dedup / merge rely on. Out-of-line (reference.cpp): m_metaObject is incomplete here.
	virtual int CompareValueLS(const ibValue& cParam) const override;

	//operator '=='
	virtual bool CompareValueEQ(const ibValue& cParam) const {
		ibValueReferenceDataObject* rhs = dynamic_cast<ibValueReferenceDataObject*>(cParam.GetRef());
		if (rhs != nullptr)
			return m_metaObject == rhs->m_metaObject && m_objGuid == rhs->m_objGuid;
		return false;
	}

	// Hashes by the GUID — the order's FIRST key, and the whole of identity for a
	// reference that points at something. The type is deliberately left out: it
	// only ever separates references that share a guid, and leaving it out merely
	// puts those in one bucket for the comparison to sort out (see the contract
	// on ibValue::GetValueHash — coarser is safe, finer is a bug).
	//
	// It also means every EMPTY reference lands in one bucket, all-zero guid
	// being what empty IS. That is the right trade: they are few, and the
	// alternative is reaching for m_metaObject, which is incomplete in this
	// header — the reason CompareValueLS itself lives out of line.
	virtual size_t GetValueHash() const override {
		const ibGuidImpl raw = m_objGuid;
		std::uint64_t h = ibHashCombine(kIbHashBasis, raw.m_data1);
		h = ibHashCombine(h, raw.m_data2);
		h = ibHashCombine(h, raw.m_data3);
		for (const unsigned char byte : raw.m_data4)
			h = ibHashCombine(h, byte);
		return (size_t)h;
	}

	//operator '!='
	virtual bool CompareValueNE(const ibValue& cParam) const {
		ibValueReferenceDataObject* rhs = dynamic_cast<ibValueReferenceDataObject*>(cParam.GetRef());
		if (rhs != nullptr)
			return m_metaObject != rhs->m_metaObject || m_objGuid != rhs->m_objGuid;
		return true;
	}

	virtual bool FindValue(const wxString& findData, std::vector<ibValue>& listValue) const;

	//support source set/get data — ibValueDataObject value primitive (presentation + runtime read/copy)
	virtual bool SetValueByMetaID(const ibMetaID& id, const ibValue& varMetaVal) override;
	virtual bool GetValueByMetaID(const ibMetaID& id, ibValue& pvarMetaVal) const override;
	
	// ibSourceDataObject hop gate. Set just writes the id slot. Get honours the PINNED type via CoerceHopType:
	// a COMPOSITE reference field resolves to UNDEFINED, so the shared helper hands back an empty typed TWIN of
	// the pin (this reference's own metaData) — a reference returns a TYPE, not undefined, so the hop steps on.
	virtual bool SetValueBySourceHop(const ibSourceHop& hop, const ibValue& value) override { return SetValueByMetaID(hop.m_id, value); }
	virtual bool GetValueBySourceHop(const ibSourceHop& hop, ibValue& out) const override;   // out-of-line (reference.cpp): needs the referenced metaobject COMPLETE to filter the pin by the field's live type

	// ⚠ OUT OF LINE, AND THAT IS THE POINT. The body was `(const ibValueMetaObjectRecordData*)m_metaObject`
	// — a C-style cast between two types that are both INCOMPLETE here (this header deliberately does not
	// reach commonObject.h; the note on GetValueBySourceHop above says the same thing about its own body).
	// A C-style cast cannot be a static_cast on an incomplete type, so it degraded to reinterpret_cast:
	// the base adjustment is SKIPPED, and the result is right only while RecordData happens to be the
	// first base of RecordDataRef. Reordering that base clause — a cosmetic edit — would have returned a
	// wrong pointer from every reference's GetMetaObject. Defined in reference.cpp, where both types are
	// complete and the compiler does the arithmetic.
	virtual const ibValueMetaObjectRecordData* GetMetaObject() const;

	// --- ibSourceDataObject: a reference IS a source object. It vends its TARGET type's explorer,
	// fueling the recursive dot-walk hop (value -> ConvertToValue<ibSourceDataObject> -> next explorer).
	// GetValueByMetaID / SetValueByMetaID above already satisfy both bases; these resolve the rest.
	// GetGuid overrides BOTH ibValueDataObject's (concrete) and ibSourceDataObject's (pure).
	virtual ibUniqueKey GetGuid() const override { return m_objGuid; }

protected:

	// Packing — identity only: the header carries the type, so the contents are
	// the guid (reference.cpp). The OBJECT never travels.
	virtual bool DoSerialize(class ibDataNode& node) const override;
	virtual bool DoDeserialize(const class ibDataNode& node) override;

public:
	virtual ibClassID GetSourceClassType() const override { return GetClassType(); }
	virtual wxString GetSourceCaption() const override { return GetString(); }
	virtual void SourceIncrRef() override { ibValue::IncrRef(); }
	virtual void SourceDecrRef() override { ibValue::DecrRef(); }
	// Out-of-line (reference.cpp): the explorer build needs the COMPLETE metaobject types. Covariant
	// GenericData* (matching ibSourceDataObject — the precise type, no cast): GenericData is complete via
	// genericData.h, pulled in through srcDataObject.h.
	virtual const ibValueMetaObjectGenericData* GetSourceMetaObject() const override;
	virtual const ibMetaData* GetSourceMetaData() const override;
	virtual const ibSourceExplorer* GetSourceExplorer() const override;

	//support show 
	virtual void ShowValue();

	//check is empty
	virtual bool IsEmpty() const {
		return !m_objGuid.isValid();
	}

	//****************************************************************************
	//*                              Reference methods                           *
	//****************************************************************************

	bool IsEmptyRef() const {
		return IsEmpty();
	}

	const ibValueMetaObjectRecordDataRef* GetMetaObjectRef() const {
		return m_metaObject;
	}

	ibValueRecordDataObjectRef* GetObject() const;

	//****************************************************************************
	//*                              Support methods                             *
	//****************************************************************************

	void FillMembers(ibMemberTable& helper) const;   // bound in ctor (was PrepareNames)

	//****************************************************************************
	//*                              Override attribute                          *
	//****************************************************************************
	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);       
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);                   

	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);

	//****************************************************************************
	//*                              Override type                               *
	//****************************************************************************

	//Get ref class 
	virtual ibClassID GetClassType() const;

	virtual wxString GetString() const;
	virtual wxString GetClassName() const;

	friend class ibValue;

private:

	bool ReadData(bool createData = true);

protected:

	bool m_initializedRef;

	const ibValueMetaObjectRecordDataRef* m_metaObject;
	ibReference* m_reference_impl;

	bool m_foundedRef;

	// The session register this reference put itself into, held so it can strike itself out of THAT
	// one — see ibReferenceRegistry::Remember. Opaque: its type belongs to reference.cpp. Empty for
	// a reference built with no session at all (bring-up, a tool, a test), which registers nowhere.
	std::shared_ptr<void> m_registryTable;

	friend class ibReferenceRegistry;
};

//********************************************************************************************

// ⭐⭐ ONE REFERENCE PER OBJECT, FOR AS LONG AS SOMEBODY HOLDS IT.
//
// A reference is an IDENTITY — this catalogue, this row — so two of them naming the same object are
// not two things; they are one thing counted twice. Before this they really were two: every cell of
// every list built its own, and each went to the database for its own copy of the same row. The same
// nomenclature printed on forty lines was read forty times.
//
// So the creation door asks first. If the object is already here, that one is returned and nothing is
// built — the row it has already read serves every later holder for nothing.
//
// LIFETIME NEEDS NO POLICY, which is what makes this a register rather than a cache. A reference is
// reference-counted already: when the last holder lets go, its destructor runs and it strikes itself
// out. The table therefore holds exactly what is alive and never one entry more. A base with a
// billion rows does not mean a billion entries — only what a form, a report or a script is holding
// at this moment, which is the population that existed anyway.
//
// ⚠ HASHED, NOT SCANNED. The earlier attempt walked an array on every creation — a thousand live
// references and a thousand more being built is a million comparisons, slower than the database read
// it was meant to save. Looked up by key, the cost does not depend on how many are alive.
//
// ⚠ PER SESSION, AND THAT IS NOT CAUTION — IT IS RIGHTS. Row-level access decides what a given user
// may read, and a row he may not read comes back as "not found". Sharing OBJECTS between sessions
// would be harmless (a reference is only a type and a guid); sharing what they have READ is not, and
// the read row is the entire point of the register. So it lives on the session that read it.
//
// ⚠ AND THEREFORE NO LOCK ON THE HOT PATH. A session is worked by one thread at a time — the worker
// pool leases it and other workers skip a leased session — so the table has no concurrent reader by
// construction, not by discipline. A lock here would sit on the busiest path in the engine and cost
// more than the read it saves.
class BACKEND_API ibReferenceRegistry {
public:
	// The live reference for this object in the CURRENT session, or null. Asked by the creation
	// doors before they build anything. No session (bring-up, a tool, a test) — no register, and
	// every reference is built as it always was.
	//
	// ⭐ THE RAW KEY, not an ibGuid. This is what the table is keyed by, and it is what the callers
	// already hold: a row's _RRRef blob IS an ibGuidImpl (ibReference::m_guid). Taking the rich type
	// here made the stored-row door build a whole ibGuid out of bytes that were then taken apart
	// again to form the key. Callers holding an ibGuid still just pass it — the conversion to the
	// storage form is implicit and is the same 16 bytes.
	// ⭐ THE IDENTIFIER IS THE KEY, so this is the primitive and the metaobject overload forwards to
	// it. It matters which way round that is: a caller holding a metaID or a reference clsid used to
	// have to SEARCH THE METADATA for the metaobject before it could ask a question the table answers
	// off the identifier alone. On a hit the metaobject is never needed — the live reference already
	// has it — so resolving it first was a metadata lookup spent to find something already in hand.
	static ibValueReferenceDataObject* Find(const ibMetaID& id, const ibGuidImpl& objGuid);

	static ibValueReferenceDataObject* Find(const ibValueMetaObjectRecordDataRef* metaObject,
	                                        const ibGuidImpl& objGuid);

	// Take note of a newly built one. Called by the CONSTRUCTOR, not by the doors: every reference
	// is born through it, so one call covers every way of making one — including the raw door and
	// the value-ctor path, which a per-door call would have missed, leaving exactly the twin the
	// register exists to prevent.
	static void Remember(ibValueReferenceDataObject* ref);

	// The last holder let go — called from ~ibValueReferenceDataObject, and nowhere else.
	//
	// ⚠ It strikes itself from the session it REGISTERED in, which need not be the current one: a
	// value can travel and be released elsewhere. Erasing from "whatever session is current now"
	// would leave the original table pointing at freed memory.
	static void Forget(const ibValueReferenceDataObject* ref);
};

#endif

