#ifndef _FRMCLIENT_BACKEND_VARIANT_GEN_H__
#define _FRMCLIENT_BACKEND_VARIANT_GEN_H__

#include "frmclient/backend/propertyManager/property/variant/variantMetaDesc.h"

// WHAT AN OBJECT IS ENTERED ON THE BASIS OF — the engine's ibVariantDataGeneration: the metaobjects it names
// (advpropGeneration.cpp).
class ibVariantDataGeneration : public ibVariantDataMetaDesc {
public:

	ibVariantDataGeneration(const ibPropertyObject* prop, const ibMetaDescription& typeDesc) : ibVariantDataMetaDesc(prop, typeDesc) {}
	ibVariantDataGeneration(const ibVariantDataGeneration& src) : ibVariantDataMetaDesc(src.m_ownerProperty, src.m_metaDesc) {}

	virtual ibVariantDataGeneration* Clone() const override {
		return new ibVariantDataGeneration(*this);
	}

	virtual wxString GetType() const override { return wxT("ibVariantDataGeneration"); }
};

#endif
