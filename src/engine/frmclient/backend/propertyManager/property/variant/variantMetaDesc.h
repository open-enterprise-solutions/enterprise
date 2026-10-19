#ifndef _FRMCLIENT_BACKEND_VARIANT_METADESC_H__
#define _FRMCLIENT_BACKEND_VARIANT_METADESC_H__

#include <wx/variant.h>

#include "frmclient/backend/backend_core.h"
#include "core/description/typeDescription.h"   // ibMetaDescription — the metaobjects a relationship names, by their ids

class FRMCLIENT_API ibPropertyObject;

// THE VALUE OF A RELATIONSHIP — the engine's ibVariantDataMetaDesc (backend/propertyManager/property/variant/
// variantMetaDesc.h) as the client's inspector holds it: the metaobjects it names, by their ids, and the owner whose
// property holds it. Owner, Record, Generation (and the charts, which reuse Owner's) are one value written three times
// over a base, as there.
//
// ⚠ NOT WRITTEN AS TEXT. The engine's three name their ids through the owner's configuration; the client holds none —
// the cell names them by the choices its row brought, as the server named them (advpropOwner.cpp).
class ibVariantDataMetaDesc : public wxVariantData {
public:

	ibMetaDescription& GetMetaDesc() { return m_metaDesc; }
	const ibMetaDescription& GetMetaDesc() const { return m_metaDesc; }

	const ibPropertyObject* GetOwnerProperty() const { return m_ownerProperty; }

	// The value IS the metadescription: the same metaIDs in the same order is the same relationship. GetType() is
	// compared too, so an owner binding and a generation binding holding the same ids are still not the same value.
	virtual bool Eq(wxVariantData& data) const override
	{
		const ibVariantDataMetaDesc* other = dynamic_cast<const ibVariantDataMetaDesc*>(&data);
		return other != nullptr
			&& GetType() == other->GetType()
			&& m_metaDesc.m_listMetaClass == other->m_metaDesc.m_listMetaClass;
	}

protected:

	ibVariantDataMetaDesc(const ibPropertyObject* prop, const ibMetaDescription& typeDesc)
		: wxVariantData(), m_ownerProperty(prop), m_metaDesc(typeDesc) {}

	const ibPropertyObject* m_ownerProperty;
	ibMetaDescription       m_metaDesc;
};

#endif
