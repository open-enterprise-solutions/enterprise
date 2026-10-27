#ifndef __GEN_VARIANT_H__
#define __GEN_VARIANT_H__

#include "backend/propertyManager/property/variant/variantMetaDesc.h"

class BACKEND_API ibPropertyValueGeneration : public ibPropertyValueMetaDesc {
public:

	ibValue GetDataValue() const;

	ibPropertyValueGeneration(const ibPropertyObject* prop, const ibMetaDescription& typeDesc) : ibPropertyValueMetaDesc(prop, typeDesc) {}
	ibPropertyValueGeneration(const ibPropertyValueGeneration& src) : ibPropertyValueMetaDesc(src.m_ownerProperty, src.m_metaDesc) {}

	virtual ibPropertyValueGeneration* Clone() const {
		return new ibPropertyValueGeneration(*this);
	}

	virtual ibString GetString() const override;

};

#endif