#ifndef __TYPE_VARIANT_H__
#define __TYPE_VARIANT_H__

#include "frmclient/backend/compiler/value.h"   // ibValue::GetAvailableCtor — a type's name, as the server named it
#include "core/description/typeDescription.h"

// The cell that holds a TYPE DESCRIPTION — the engine's ibVariantDataAttribute (backend/propertyManager/property/variant/
// variantType.h) as the client's inspector holds it: a description, and nothing more than that. Its default, its
// refresh against the configuration, its barriers are the server's — the description comes back refreshed
// (ibPropertyGate::SetValue).
class FRMCLIENT_API ibVariantDataAttribute : public wxVariantData {
public:

	/////////////////////////////////////////////////////////////////////////////////////////

	ibTypeDescription& GetTypeDesc() { return m_typeDesc; }
	const ibTypeDescription& GetTypeDesc() const { return m_typeDesc; }

	/////////////////////////////////////////////////////////////////////////////////////////

	ibVariantDataAttribute(const ibTypeDescription& typeDesc = ibTypeDescription()) : wxVariantData(), m_typeDesc(typeDesc) {}
	ibVariantDataAttribute(const ibVariantDataAttribute& list) : wxVariantData(), m_typeDesc(list.m_typeDesc) {}

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

private:

	// The names of the types it holds, as the server named them.
	wxString MakeString() const {
		wxString strDescr;
		for (const auto clsid : m_typeDesc.GetClsidList()) {
			const ibCtorAbstractType* typeCtor = ibValue::GetAvailableCtor(clsid);
			if (typeCtor == nullptr)
				continue;
			strDescr = strDescr.IsEmpty() ? typeCtor->GetClassName() : strDescr + wxT(", ") + typeCtor->GetClassName();
		}
		return strDescr;
	}

	ibTypeDescription m_typeDesc;
};

#endif // !__TYPE_VARIANT_H__
