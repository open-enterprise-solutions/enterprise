#ifndef __TYPE_VARIANT_H__
#define __TYPE_VARIANT_H__

#include "backend/metaCollection/metaObject.h"
#include "backend/backend_type.h"

class BACKEND_API ibVariantDataAttribute : public wxVariantData {
	wxString MakeString() const;
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

	ibVariantDataAttribute(const ibBackendTypeConfigFactory* prop) : wxVariantData(), m_ownerProperty(prop) {}

	ibVariantDataAttribute(const ibBackendTypeConfigFactory* prop, const ibValueTypes type) : wxVariantData(), m_ownerProperty(prop) { m_typeDesc.SetDefaultMetaType(type); }
	ibVariantDataAttribute(const ibBackendTypeConfigFactory* prop, const ibClassID& clsid) : wxVariantData(), m_ownerProperty(prop) { m_typeDesc.SetDefaultMetaType(clsid); }

	ibVariantDataAttribute(const ibBackendTypeConfigFactory* prop, const ibMetaID& id) : wxVariantData(), m_ownerProperty(prop) { DoSetFromMetaId(id); }
	ibVariantDataAttribute(const ibBackendTypeConfigFactory* prop, const ibTypeDescription& typeDesc) : wxVariantData(), m_ownerProperty(prop), m_typeDesc(typeDesc) {}

	ibVariantDataAttribute(const ibVariantDataAttribute& list) : wxVariantData(), m_ownerProperty(list.m_ownerProperty), m_typeDesc(list.m_typeDesc) {}

	virtual ibVariantDataAttribute* Clone() const {
		return new ibVariantDataAttribute(*this);
	}

	virtual bool Eq(wxVariantData& data) const {
		ibVariantDataAttribute* srcAttr = dynamic_cast<ibVariantDataAttribute*>(&data);
		if (srcAttr != nullptr)
			return srcAttr->m_typeDesc == m_typeDesc;
		return false;
	}

#if wxUSE_STD_IOSTREAM
	virtual bool Write(wxSTD ostream& str) const {
		str << MakeString();
		return true;
	}
#endif
	virtual bool Write(wxString& str) const {
		str = MakeString();
		return true;
	}

	virtual wxString GetType() const {
		return wxT("ibVariantDataAttribute");
	}

protected:

	const ibBackendTypeConfigFactory* m_ownerProperty = nullptr;
	unsigned int m_object_version = 0;
	ibTypeDescription m_typeDesc;
	ibTypeDescription m_typeValueDesc;   // m_typeDesc with each barrier replaced by its members — empty when none was
};

#endif // !__TYPE_VARIANT_H__
