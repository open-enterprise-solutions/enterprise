#ifndef __GEN_VARIANT_H__
#define __GEN_VARIANT_H__

#include "backend/propertyManager/property/variant/variantMetaDesc.h"

class BACKEND_API ibVariantGeneration : public ibVariantMetaDesc {
public:

	ibValue GetDataValue() const;

	ibVariantGeneration(const ibPropertyObject* prop, const ibMetaDescription& typeDesc) : ibVariantMetaDesc(prop, typeDesc) {}
	ibVariantGeneration(const ibVariantGeneration& src) : ibVariantMetaDesc(src.m_ownerProperty, src.m_metaDesc) {}

	virtual ibString GetString() const override;

};

#endif