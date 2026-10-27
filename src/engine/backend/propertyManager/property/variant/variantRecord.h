#ifndef __RECORD_VARIANT_H__
#define __RECORD_VARIANT_H__

#include "backend/propertyManager/property/variant/variantMetaDesc.h"

class BACKEND_API ibVariantRecord : public ibVariantMetaDesc {
public:

	ibValue GetDataValue() const;

	ibVariantRecord(const ibPropertyObject* prop, const ibMetaDescription& typeDesc) : ibVariantMetaDesc(prop, typeDesc) {}
	ibVariantRecord(const ibVariantRecord &src) : ibVariantMetaDesc(src.m_ownerProperty, src.m_metaDesc) {}

	virtual ibString GetString() const override;

};

#endif // !_DOCUMENT_VARIANT_
