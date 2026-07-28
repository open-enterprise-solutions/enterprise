#ifndef _REFERENCE_H__
#define _REFERENCE_H__

#include "backend/compiler/value.h"
#include "backend/valueInfo.h"
#include "backend/srcDataObject.h"   // ibSourceDataObject base — a reference IS a source object (vends its target's explorer)
#include "backend/typeDescription.h"   // ibTypeDescription — CoerceHopType validates the pin against a field's CURRENT type filter

#include <chrono>   // reference-key timestamp (MakeNewGuid)

//********************************************************************************************
//*                                     Defines                                              *
//********************************************************************************************

class BACKEND_API ibMetaData;

//********************************************************************************************

class BACKEND_API ibValueMetaObjectRecordDataRef;
class BACKEND_API ibValueRecordDataObjectRef;

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

	// ─── Reference-key encoder ─────────────────────────────────────────────────────────────────
	// THE place a NEW reference value's key is minted — the reference mechanism owns it, so it lives on
	// the reference. A time-ordered, self-describing 16-byte guid, laid out through ibGuidImpl fields so
	// it lines up with the guid comparator / stored-blob order:
	//     Data1 = metaID (type)  |  Data2+Data3 = second  |  Data4 = 64-bit random
	// metaID leads → type grouping / SQL-slice (the same slot the reader extracts metaID from); the second
	// orders inserts (~136 yr range, index locality); the 64-bit random tail makes concurrent same-second
	// same-type keys unique (uniqueness lives here — the timestamp only orders). Random comes from a v4
	// guid — never a v1 time_low we'd overwrite (that destroyed uniqueness under concurrency).
	static ibGuid MakeNewGuid(ibMetaID metaID) {
		// A zero metaID means the type isn't set — the key would encode a 0 type (Data1). Catch it.
		wxASSERT_MSG(metaID != 0, wxT("MakeNewGuid: reference key minted with a zero (unset) metaID"));

		ibGuidImpl impl = ibGuid::newGuid(GUID_RANDOM);
		impl.m_data1 = (uint32_t)metaID;                                     // Data1 : metaID (type)
		const uint32_t sec = (uint32_t)std::chrono::duration_cast<std::chrono::seconds>(
			std::chrono::system_clock::now().time_since_epoch()).count();
		impl.m_data2 = (unsigned short)((sec >> 16) & 0xFFFF);              // Data2 : seconds, high 16
		impl.m_data3 = (unsigned short)( sec        & 0xFFFF);              // Data3 : seconds, low 16
		// Data4 stays the OS-random 64-bit tail
		return impl;
	}

	ibReference* GetReferenceData() const {
		return m_reference_impl;
	}

	// Identity key = the DB reference key: metaID + guid (the _RRRef storage is [guid 16][metaID 4],
	// and the runtime ibReference is keyed by (metaID, guid)). NOT the guid alone — a reference COLUMN
	// can target several types, so the metaID disambiguates: two refs with the same guid but different
	// type are distinct, exactly as in the database. This aligns runtime grouping / join / dedup over a
	// reference with the DB key (one identity for both runtime and push-down). See ibValue::GetHashKey.
	// Defined in reference.cpp — m_metaObject (ibValueMetaObjectRecordDataRef) is incomplete here.
	virtual wxString GetHashKey() const override;

	void PrepareRef(bool createData = true);

	virtual ~ibValueReferenceDataObject();

	static ibValueReferenceDataObject* Create(const ibMetaData* metaData, const ibMetaID& id, const ibGuid& objGuid = wxNullGuid);
	static ibValueReferenceDataObject* Create(const ibValueMetaObjectRecordDataRef* metaObject, const ibGuid& objGuid = wxNullGuid);

	// Like Create(metaObject, guid) but WITHOUT the PrepareRef call. The
	// value-ctor registry path (ibCtorMetaValueTypeReference::CreateObject)
	// must use this: PrepareRef there recurses — it materialises the
	// reference's attribute values, one of which is itself a reference,
	// re-entering CreateObject -> stack overflow. PrepareRef runs later, on
	// first real use. (Member, so it reaches the private ctor.)
	static ibValueReferenceDataObject* CreateRaw(const ibValueMetaObjectRecordDataRef* metaObject, const ibGuid& objGuid = wxNullGuid);

	static ibValueReferenceDataObject* Create(const ibMetaData* metaData, void* ptr);
	static ibValueReferenceDataObject* CreateFromPtr(const ibMetaData* metaData, void* ptr);

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
	// ONE method tunes all four for references. Orders by the GUID alone (value order, via ibGuid's
	// operators): the guid now CARRIES the metaID in its Data1 (the reference-key encoder), so it already
	// orders by type first and then by identity, matching the physical _RRRef blob byte-for-byte. Same guid
	// ⟹ same metaID, so no separate metaID tiebreak is needed. A non-reference operand is incomparable -> 0.
	virtual int CompareValueLS(const ibValue& cParam) const {
		ibValueReferenceDataObject* rhs = dynamic_cast<ibValueReferenceDataObject*>(cParam.GetRef());
		if (rhs == nullptr)
			return 0;
		if (m_objGuid < rhs->m_objGuid) return -1;
		if (rhs->m_objGuid < m_objGuid) return 1;
		return 0;   // guids equal ⟹ same metaID (in Data1) ⟹ same key
	}

	//operator '=='
	virtual bool CompareValueEQ(const ibValue& cParam) const {
		ibValueReferenceDataObject* rhs = dynamic_cast<ibValueReferenceDataObject*>(cParam.GetRef());
		if (rhs != nullptr)
			return m_metaObject == rhs->m_metaObject && m_objGuid == rhs->m_objGuid;
		return false;
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

	//get metaData from object
	virtual const ibValueMetaObjectRecordData* GetMetaObject() const {
		return (const ibValueMetaObjectRecordData *)m_metaObject;
	}

	// --- ibSourceDataObject: a reference IS a source object. It vends its TARGET type's explorer,
	// fueling the recursive dot-walk hop (value -> ConvertToValue<ibSourceDataObject> -> next explorer).
	// GetValueByMetaID / SetValueByMetaID above already satisfy both bases; these resolve the rest.
	// GetGuid overrides BOTH ibValueDataObject's (concrete) and ibSourceDataObject's (pure).
	virtual ibUniqueKey GetGuid() const override { return m_objGuid; }
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
};

#endif 