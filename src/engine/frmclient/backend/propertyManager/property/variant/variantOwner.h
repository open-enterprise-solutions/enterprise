#ifndef _FRMCLIENT_BACKEND_VARIANT_OWNER_H__
#define _FRMCLIENT_BACKEND_VARIANT_OWNER_H__

#include "frmclient/backend/propertyManager/property/variant/variantMetaDesc.h"

// A RELATIONSHIP AS A PROPERTY HOLDS IT — the engine's ibVariantDataOwner: the metaobjects it names. The value an owner's,
// a chart's binding is listed with (ibBackendProperty::CreateValueList) and what its cell edits (advpropOwner.cpp,
// advpropChartBinding.cpp).
class ibVariantDataOwner : public ibVariantDataMetaDesc {
public:

	ibVariantDataOwner(const ibPropertyObject* prop, const ibMetaDescription& typeDesc) : ibVariantDataMetaDesc(prop, typeDesc) {}
	ibVariantDataOwner(const ibVariantDataOwner& src) : ibVariantDataMetaDesc(src.m_ownerProperty, src.m_metaDesc) {}

	virtual ibVariantDataOwner* Clone() const override {
		return new ibVariantDataOwner(*this);
	}

	virtual wxString GetType() const override { return wxT("ibVariantDataOwner"); }
};

#endif
