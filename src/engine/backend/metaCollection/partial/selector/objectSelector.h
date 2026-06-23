#ifndef _SELECTOR_H__
#define _SELECTOR_H__

#include "backend/metaCollection/partial/commonObject.h"
#include "backend/query/dataQueryBuilder.h"   // L3 door — the shared keyset-cursor engine

// ibValueSelectorDataObject — a script-facing forward cursor (the selection surface)
// over ONE queryable. Pure keyset: the statement is built ONCE (lazily, on the first
// fetch), and every Next() fetches the single row after the anchor and re-anchors on
// it; Reset() rewinds the anchor to the start. No buffer, no full scan. The base owns
// the whole cursor drive; a subclass supplies only the anchor transport + the value
// surface (3 small hooks).
class BACKEND_API ibValueSelectorDataObject : public ibValueDynamicMembers {
	public:

	ibValueSelectorDataObject();
	virtual ~ibValueSelectorDataObject();

	// Advance to the next row after the anchor (false = exhausted / designer mode).
	bool Next();

	//is empty
	virtual bool IsEmpty() const {
		return false;
	}

	//get metaData from object
	virtual const ibValueMetaObjectGenericData* GetMetaObject() const = 0;

	//Get ref class
	virtual ibClassID GetClassType() const;

	//types
	virtual wxString GetClassName() const;
	virtual wxString GetString() const;

protected:

	// Rewind the cursor to the start (anchor cleared). Per-subclass: clears its anchor
	// + current row (and, in designer mode, populates designer values).
	virtual void Reset() = 0;

	// The shared keyset-cursor step: build the statement once (From + page cache +
	// effective sort), then fetch the ONE row after the anchor, re-anchor on it, and
	// materialise it. The SQL renders once per shape and only the anchor params rebind,
	// so the build-once page cache hits across calls. False = exhausted.
	bool FetchNext();

	// --- per-metatype hooks (the only parts that differ) ---------------------
	virtual const ibBackendQueryable* GetQueryable() const = 0;          // the source to page
	virtual bool ApplyAnchor(ibReadPageRequest& page) const = 0;         // anchor -> page; false = no anchor (first row)
	virtual void CaptureAnchor(const ibDataQueryResult& selection) = 0;  // current row -> anchor
	virtual void MaterialiseRow(const ibDataQueryResult& selection) = 0; // current row -> value surface

	// --- shared cursor state (built once on the first FetchNext) -------------
	std::unique_ptr<ibDataQueryBuilder>  m_query;       // the statement (null = not built yet)
	std::vector<ibQuerySortItem>         m_effective;   // effective ORDER BY (identity tail)
};

class BACKEND_API ibValueSelectorRecordDataObject : public ibValueSelectorDataObject,
	public ibValueDataObject {
	public:

	ibValueSelectorRecordDataObject(const ibValueMetaObjectRecordDataMutableRef* metaObject);

	virtual ibValueRecordDataObjectRef* GetObject(const ibGuid& guid) const;

	//get metaData from object
	virtual const ibValueMetaObjectRecordData* GetMetaObject() const {
		return m_metaObject;
	}

	void FillMembers(ibMemberTable& helper) const;   // bound in ctor (was PrepareNames)
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);//method call

	//attribute
	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);        //setting attribute
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);                   //attribute value

protected:

	virtual void Reset();

	// The anchor IS m_objGuid (catalog row-key tiebreaker; invalid = from the start).
	virtual const ibBackendQueryable* GetQueryable() const { return m_metaObject->GetQueryable(); }
	virtual bool ApplyAnchor(ibReadPageRequest& page) const;
	virtual void CaptureAnchor(const ibDataQueryResult& selection);
	virtual void MaterialiseRow(const ibDataQueryResult& selection);

protected:

	const ibValueMetaObjectRecordDataMutableRef* m_metaObject;
};

/////////////////////////////////////////////////////////////////////////////

class BACKEND_API ibValueSelectorRegisterDataObject :
	public ibValueSelectorDataObject {
	public:
	ibValueSelectorRegisterDataObject(const ibValueMetaObjectRegisterData* metaObject);

	virtual ibValueRecordManagerObject* GetRecordManager(const ibRowMetaValues& keyValues) const;

	//get metaData from object
	virtual const ibValueMetaObjectRegisterData* GetMetaObject() const {
		return m_metaObject;
	}

	void FillMembers(ibMemberTable& helper) const;   // bound in ctor (was PrepareNames)
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);//method call

	//attribute
	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);        //setting attribute
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);                   //attribute value

protected:

	virtual void Reset();

	// A register has no single row-key, so the anchor is the row's value at every
	// effective-sort column (m_anchorSort; empty = from the start).
	virtual const ibBackendQueryable* GetQueryable() const { return m_metaObject->GetQueryable(); }
	virtual bool ApplyAnchor(ibReadPageRequest& page) const;
	virtual void CaptureAnchor(const ibDataQueryResult& selection);
	virtual void MaterialiseRow(const ibDataQueryResult& selection);

protected:

	const ibValueMetaObjectRegisterData* m_metaObject;

	ibRowMetaValues      m_keyValues;    // current row identity (GetRecordManager / GetPropVal)
	ibRowMetaValues      m_current;      // current row attributes — GetPropVal's SSOT
	std::vector<ibValue> m_anchorSort;   // keyset anchor (effective-sort values; empty = start)
};

#endif
