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

	// Identity key = type + guid — the metaObject's metaID (the _RTRef column) plus the pure _RRRef guid.
	// NOT the guid alone — a reference COLUMN can target several types, so the type disambiguates: two refs
	// with the same guid but a different target type are distinct, exactly as in the database (_RTRef + _RRRef).
	// This aligns runtime grouping / join / dedup over a reference with the DB key. See ibValue::GetHashKey.
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

	// Reconstruct from a stored row: `refClsid` is the _RTRef target type, `ptr` the pure _RRRef guid blob.
	// The type comes from the column (clsid -> metaObject), NOT from the key bytes.
	static ibValueReferenceDataObject* Create(const ibMetaData* metaData, const ibClassID& refClsid, void* ptr);
	static ibValueReferenceDataObject* CreateFromPtr(const ibMetaData* metaData, const ibClassID& refClsid, void* ptr);

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
};

#endif 