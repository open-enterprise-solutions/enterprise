#ifndef __BACKEND_TYPE_H__
#define __BACKEND_TYPE_H__

#include <vector>

#include "backend/typeDescription.h"
#include "backend/query/queryColumn.h"     // ibBackendAbstractColumn (name/synonym/comment) + ibBackendSourceColumn
#include "backend/sourceDescription.h"     // ibSourceDescription — control's bound source path (GetSourceDesc / SetDefaultSourceType)

//////////////////////////////////////////////////////////////

class BACKEND_API ibBackendTypeFactory {
public:

#pragma region clsid 

	void SetDefaultMetaType(const ibValueTypes& valType = ibValueTypes::TYPE_EMPTY) { GetTypeDesc().SetDefaultMetaType(valType); }

	void SetDefaultMetaType(const ibClassID& clsid) { GetTypeDesc().SetDefaultMetaType(clsid); }
	void SetDefaultMetaType(const ibClassID& clsid, const ibTypeDescription::ibTypeData& descr) { GetTypeDesc().SetDefaultMetaType(clsid); }

	void SetDefaultMetaType(const std::vector<ibClassID>& array) { GetTypeDesc().SetDefaultMetaType(array); }
	void SetDefaultMetaType(const std::vector<ibClassID>& array, const ibTypeDescription::ibTypeData& descr) { GetTypeDesc().SetDefaultMetaType(array, descr); }
	void SetDefaultMetaType(const std::vector<ibClassID>& array, const ibQualifierNumber& qNumber, const ibQualifierDate& qDate, ibQualifierString& qString) { GetTypeDesc().SetDefaultMetaType(array, qNumber, qDate, qString); }

	void SetDefaultMetaType(const ibTypeDescription& typeDesc) { GetTypeDesc().SetDefaultMetaType(typeDesc); }

	void ClearMetaType() { GetTypeDesc().ClearMetaType(); }

	ibClassID GetFirstClsid() const { return GetTypeDesc().GetFirstClsid(); }
	ibClassID GetByIdx(unsigned int idx) const { return GetTypeDesc().GetByIdx(idx); }

	unsigned int GetClsidCount() const { return GetTypeDesc().GetClsidCount(); }

#pragma endregion

	//Create value by selected type
	virtual ibValue CreateValue() const;
	virtual ibValue* CreateValueRef() const;

	//convert value
	template<class retType = ibValue>
	retType* CreateAndConvertValueRef() {
		ibValue* retVal = CreateValueRef();
		if (retVal != nullptr)
			return CastValue<retType>(retVal);
		return (retType*)nullptr;
	}

	//Adjust value
	virtual ibValue AdjustValue() const;
	virtual ibValue AdjustValue(const ibValue& varValue) const;

	//get type description
	virtual ibTypeDescription& GetTypeDesc() const = 0;

	// ⭐⭐ IS THE TYPE FILLED IN — asked HERE, of whoever owns a type description, and never counted
	// at a callsite.
	//
	// It belongs on this factory and nowhere else: the type description is declared here (above), so
	// this is the one place that can answer for every holder of one — an attribute, a control's
	// source, a filter's field. Put on a subclass it would answer for that subclass alone, and the
	// next holder would spell the same question its own way (`GetClsidCount() == 0`,
	// `!GetTypeDesc().IsOk()`, `GetClsidList().empty()`) — three spellings of one fact, drifting.
	//
	// An empty type description is a column no value can ever enter, and every rule that refuses such
	// a state asks exactly this: a register with no recorder, a chart of accounts with no
	// characteristic chart, an analytics slot the chart never typed.
	// NOT virtual: the answer is a function of the type description alone, and GetTypeDesc() — which
	// IS virtual — already supplies whatever each holder keeps. A virtual here would only offer
	// subclasses a chance to disagree about what "empty" means, which is the drift this exists to
	// prevent (and, being inline on a BACKEND_API class, it would demand an out-of-line definition
	// every DLL that sees the header must link against).
	bool IsEmptyTypeDesc() const { return !GetTypeDesc().IsOk(); }
};

//////////////////////////////////////////////////////////////

enum ibSelectorDataType {
	ibSelectorDataType_any,
	ibSelectorDataType_boolean,
	ibSelectorDataType_reference,
	ibSelectorDataType_table,
	ibSelectorDataType_resource,
};

//////////////////////////////////////////////////////////////

class BACKEND_API ibBackendTypeConfigFactory :
	public ibBackendTypeFactory {
public:

	virtual ibSelectorDataType GetFilterDataType() const {
		return ibSelectorDataType::ibSelectorDataType_reference;
	}

	// The default value type (clsid) for a filter kind: _boolean -> Boolean, _resource -> Number,
	// _table -> value-table, _reference / else -> String. Static, keyed on the kind — the ONE mapping both
	// ibVariantDataAttribute::DoSetDefaultMetaType and ibValueControl::AutoBindNewSource use, so they cannot drift.
	static ibClassID GetDefaultTypeByFilter(ibSelectorDataType filterDataType);

	//Create value by selected type
	virtual ibValue CreateValue() const;
	virtual ibValue* CreateValueRef() const;

	//Adjust value
	virtual ibValue AdjustValue() const;
	virtual ibValue AdjustValue(const ibValue& varValue) const;

	//get metadata
	// The universal factory capability is READ-only: every type-factory can hand
	// out a const ibMetaData*, so the container mutators (CreateMetaObject /
	// RemoveMetaObject / RenameMetaObject / RegisterCtor) are a compile error
	// from any base-factory path — runtime included.
	//
	// Mutable access is NOT universal: it is added (as a non-const overload) only
	// by classes that genuinely own a mutable ibMetaData — the metaobjects
	// (metaObject.h, metaAttributeObject.h, ...). Runtime classes such as forms /
	// controls take their metadata from a const source (GetSourceMetaObject() is
	// const) and therefore CANNOT produce a mutable pointer without a const_cast,
	// so they must not be forced to implement a non-const overload here.
	// Designer holds the metaobject non-const and resolves to its mutable overload
	// automatically.
	virtual const class ibMetaData* GetMetaData() const = 0;
};

//////////////////////////////////////////////////////////////

enum ibSourceDataType {
	ibSourceDataType_table,
	ibSourceDataType_tableColumn,
	ibSourceDataType_attribute,
};

//////////////////////////////////////////////////////////////

// The form's per-attribute HOLDER as a backend interface — it pairs an attribute
// (its DEFINITION, name/type/id) with the VALUE/source that attribute manages. Inherits
// nothing; implemented on the frontend (ibFormAttributeValue), where all the logic lives.
// GetSourceList vends THESE — attribute + value together — not bare attributes: a source
// picker reads the columns through GetSourceValue()->GetSourceExplorer(), never the concrete
// value type. The source is produced by the holder (it materialises the value from the
// attribute's Type), so the attribute itself stays value-free.
class BACKEND_API ibBackendFormAttributeValue : public ibBackendAbstractColumn {
public:
	virtual ~ibBackendFormAttributeValue() = default;
	// FAÇADE — the holder answers for its (private, internal) attribute directly; the concrete
	// description is NEVER handed out. Outside code reads name / id / type / source through here.

	// ibBackendAbstractColumn — the attribute answers Name / Synonym / Comment like a metadata
	// column, so ONE resolver returns either. The class IS an attribute, so the accessors carry no
	// "Attribute" noise: GetName / GetId / IsMain. GetName IS the attribute's name (the abstract
	// column's); GetSynonym / GetComment are overridden by the concrete holder from its properties.
	virtual wxString GetName() const override = 0;
	virtual ibMetaID GetId() const = 0;

	virtual bool IsMain() const = 0;

	virtual const ibTypeDescription& GetTypeDesc() const = 0;
	virtual class ibSourceDataObject* GetSourceValue() const = 0;
};

//////////////////////////////////////////////////////////////

class BACKEND_API ibBackendTypeSourceFactory :
	public ibBackendTypeConfigFactory {
public:

	virtual ibSourceDataType GetFilterSourceDataType() const {
		return ibSourceDataType::ibSourceDataType_attribute;
	}

	//Get source object
	virtual class ibSourceObject* GetSourceObject() const = 0;

	// This control's OWN bound source path (head attribute id + deeper hops). A column reads its PARENT
	// table's path through this to compose its own (parent path + column id). Pure MUTABLE ref — the getter
	// AND (via the SetDefaultSourceType / ClearSourceType helpers below) the setter, exactly like GetTypeDesc.
	virtual ibSourceDescription& GetSourceDesc() const = 0;

	// Source-path setter family — the twin of ibBackendTypeFactory's SetDefaultMetaType / ClearMetaType over
	// GetTypeDesc: bind THROUGH the mutable getter. Overloads for one hop or a ready DESCRIPTION (the wrapper
	// carries the whole path). Non-virtual — shared via each control's GetSourceDesc.
	void SetDefaultSourceType(const ibSourceId& id) { GetSourceDesc() = ibSourceDescription(id); }
	void SetDefaultSourceType(const ibSourceDescription& desc) { GetSourceDesc() = desc; }

	void ClearSourceType() { GetSourceDesc() = ibSourceDescription(); }

	// Available source HOLDERS of the owning context (default: none — filled via the out-param).
	// Each holder pairs the attribute (definition) with its value/source, so the picker reads
	// columns through GetSourceValue()->GetSourceExplorer() without the concrete value type.
	virtual bool GetSourceList(std::vector<ibBackendFormAttributeValue*>& out) const { return false; }

	// The "dot": feed a source DESCRIPTION (the binding path wrapper), get back the leaf COLUMN. Its head
	// (path[0]) gates to one of THIS context's source attributes (GetSourceList); deeper hops resolve as its
	// fields. The result is the neutral ibBackendSourceColumn (a metaobject attribute OR a queryable column),
	// so the caller never sees the concrete class. Null = a whole-attribute binding (length 1) or a BROKEN
	// path (a hop no longer resolves). `valid`/`outText` (optional) report resolvability and the dotted
	// display "Attr.Field.Sub". Takes the wrapper (not a bare vector) — resolution is source-explorer-driven.
	const ibBackendSourceColumn* WalkSource(const ibSourceDescription& desc,
		bool* valid = nullptr, wxString* outText = nullptr) const;

	// The source HOLDER whose attribute id matches — the ONE lookup shared by the dot-walk and the
	// path / type resolvers (instead of re-scanning GetSourceList at every site). Null = no such
	// attribute in this context.
	ibBackendFormAttributeValue* FindSourceHolder(const ibMetaID& id) const;
};

#endif
