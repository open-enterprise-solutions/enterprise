#ifndef __TYPE_VARIANT_H__
#define __TYPE_VARIANT_H__

#include "backend/propertyManager/propertyValue.h"
#include "backend/metaCollection/metaObject.h"
#include "backend/backend_type.h"

#include <atomic>
#include <mutex>

class BACKEND_API ibPropertyValueAttribute : public ibPropertyValue {
protected:
	virtual void DoSetDefaultMetaType();
	virtual void DoSetFromMetaId(const ibMetaID& id);
	virtual void DoSetFromTypeId(const ibTypeDescription& td);
	virtual void DoRefreshTypeDesc();
public:

	ibSelectorDataType GetFilterDataType() const { return m_ownerProperty->GetFilterDataType(); }

	/////////////////////////////////////////////////////////////////////////////////////////

	ibTypeDescription& GetTypeDesc() {
		if (m_typeDesc.GetClsidCount() > 0) 
			RefreshTypeDesc();
		return m_typeDesc;
	}

	ibTypeDescription& GetTypeValueDesc() {
		GetTypeDesc();
		return m_typeValueDesc.IsOk() ? m_typeValueDesc : m_typeDesc;
	}

	/////////////////////////////////////////////////////////////////////////////////////////

	void SetDefaultMetaType() { DoSetDefaultMetaType(); }

	/////////////////////////////////////////////////////////////////////////////////////////

	void SetFromMetaDesc(const ibMetaID& id) { DoSetFromMetaId(id); }
	void SetFromTypeDesc(const ibTypeDescription& td) { DoSetFromTypeId(td); }

	/////////////////////////////////////////////////////////////////////////////////////////

	void RefreshTypeDesc() { DoRefreshTypeDesc(); }

	/////////////////////////////////////////////////////////////////////////////////////////

	ibPropertyValueAttribute(const ibBackendTypeConfigFactory* prop) : ibPropertyValue(ibValueTypes::TYPE_VALUE), m_ownerProperty(prop) {}

	ibPropertyValueAttribute(const ibBackendTypeConfigFactory* prop, const ibValueTypes type) : ibPropertyValue(ibValueTypes::TYPE_VALUE), m_ownerProperty(prop) { m_typeDesc.SetDefaultMetaType(type); }
	ibPropertyValueAttribute(const ibBackendTypeConfigFactory* prop, const ibClassID& clsid) : ibPropertyValue(ibValueTypes::TYPE_VALUE), m_ownerProperty(prop) { m_typeDesc.SetDefaultMetaType(clsid); }

	ibPropertyValueAttribute(const ibBackendTypeConfigFactory* prop, const ibMetaID& id) : ibPropertyValue(ibValueTypes::TYPE_VALUE), m_ownerProperty(prop) { DoSetFromMetaId(id); }
	ibPropertyValueAttribute(const ibBackendTypeConfigFactory* prop, const ibTypeDescription& typeDesc) : ibPropertyValue(ibValueTypes::TYPE_VALUE), m_ownerProperty(prop), m_typeDesc(typeDesc) {}

	ibPropertyValueAttribute(const ibPropertyValueAttribute& list) : ibPropertyValue(ibValueTypes::TYPE_VALUE), m_ownerProperty(list.m_ownerProperty), m_typeDesc(list.m_typeDesc) {}

	virtual ibPropertyValueAttribute* Clone() const {
		return new ibPropertyValueAttribute(*this);
	}

	virtual bool CompareValueEQ(const ibPropertyValue& data) const override {
		const ibPropertyValueAttribute* srcAttr = dynamic_cast<const ibPropertyValueAttribute*>(data.GetRef());
		if (srcAttr != nullptr)
			return srcAttr->m_typeDesc == m_typeDesc;
		return false;
	}

	virtual ibString GetString() const override;

protected:

	const ibBackendTypeConfigFactory* m_ownerProperty = nullptr;
	// ⚠ THE TYPES ARE REFRESHED ONE AT A TIME. A configuration's attribute is read by every session of the base, and
	// the first read after the registered types changed refreshes it — four sessions doing so at once freed the same
	// vector twice (2026-10-06). The load refreshes it before any session reads (ibValueMetaObject::RunSubtree); what
	// changes the types later is refreshed under the lock, the version asked again inside it.
	std::atomic<unsigned int> m_object_version { 0 };
	std::mutex m_refreshMutex;
	ibTypeDescription m_typeDesc;
	ibTypeDescription m_typeValueDesc;   // m_typeDesc with each barrier replaced by its members — empty when none was
};

#endif // !__TYPE_VARIANT_H__
