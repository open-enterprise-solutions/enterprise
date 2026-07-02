#ifndef _REFERENCE_H__
#define _REFERENCE_H__

#include "backend/compiler/value.h"
#include "backend/valueInfo.h"
#include "backend/srcDataObject.h"   // ibSourceDataObject base — a reference IS a source object (vends its target's explorer)

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

	// Ordering primitive (three-way) — the base virtual GT/GE/LE and operator< all derive from this, so this
	// ONE method tunes all four for references. Orders by GUID first (value order, via ibGuid's operators),
	// then by the type's metaID — so a mixed-type (variant) reference stream forms a deterministic sequence,
	// mirroring the physical _RRRef blob's [guid][metaID] layout. A mixed stream carries distinct guids, so the
	// guid already fully orders it; metaID is the tiebreak keeping the order total. metaID comes from
	// GetClassType() & kIbClsidBodyMask — the SAME source the DB _RRRef codec uses, and it sidesteps the
	// incomplete m_metaObject here (a non-reference operand is order-incomparable -> 0).
	virtual int CompareValueLS(const ibValue& cParam) const {
		ibValueReferenceDataObject* rhs = dynamic_cast<ibValueReferenceDataObject*>(cParam.GetRef());
		if (rhs == nullptr)
			return 0;
		if (m_objGuid < rhs->m_objGuid) return -1;
		if (rhs->m_objGuid < m_objGuid) return 1;
		const ibMetaID l = static_cast<ibMetaID>(GetClassType()      & kIbClsidBodyMask);
		const ibMetaID r = static_cast<ibMetaID>(rhs->GetClassType() & kIbClsidBodyMask);
		return l < r ? -1 : (r < l ? 1 : 0);
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

	//support source set/get data 
	virtual bool SetValueByMetaID(const ibMetaID& id, const ibValue& varMetaVal);
	virtual bool GetValueByMetaID(const ibMetaID& id, ibValue& pvarMetaVal) const;

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