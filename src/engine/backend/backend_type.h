#ifndef __BACKEND_TYPE_H__
#define __BACKEND_TYPE_H__

#include <vector>

#include "backend/typeDescription.h"

struct ibSourceDescription;   // control's bound source path (GetSourceDesc)

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

// Backend-visible view of a form source attribute (the concrete value lives in
// the frontend ibValueFormAttribute, which implements this). IS-A type-config
// factory, so it carries the attribute's Type (GetTypeDesc) + metadata directly
// — backend code (the picker, the binding resolve) reads them with NO cross-cast.
// Only the gate identity lives here (name + id); the source view / kind are
// runtime concerns the FORM reads off the concrete attribute, not via this.
class BACKEND_API ibBackendFormAttribute : public ibBackendTypeConfigFactory {
public:
	virtual wxString GetAttributeName() const = 0;
	virtual ibMetaID GetAttributeId() const = 0;
	virtual bool IsMainAttribute() const = 0;
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

	// This control's OWN bound source path (head attribute id + deeper hops). A column
	// reads its PARENT table's path through this to compose its own (parent path + column
	// id). Pure — every source factory defines its own path (controls do so via the
	// control base / their source property).
	virtual ibSourceDescription GetSourceDesc() const = 0;

	// filter data
	virtual bool FilterSource(const class ibSourceExplorer& src, const ibMetaID& id) const;

	// Available source attributes of the owning context (default: none — filled
	// via the out-param). The control's type-factory overrides this to expose its
	// form's attribute table; the picker / ibPropertySource enumerate these.
	virtual bool GetSourceList(std::vector<ibBackendFormAttribute*>& out) const { return false; }
};

#endif