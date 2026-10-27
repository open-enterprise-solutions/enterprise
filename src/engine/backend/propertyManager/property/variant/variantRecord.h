#ifndef __RECORD_VARIANT_H__
#define __RECORD_VARIANT_H__

#include "backend/propertyManager/property/variant/variantMetaDesc.h"

class BACKEND_API ibPropertyValueRecord : public ibPropertyValueMetaDesc {
public:

	ibValue GetDataValue() const;

	ibPropertyValueRecord(const ibPropertyObject* prop, const ibMetaDescription& typeDesc) : ibPropertyValueMetaDesc(prop, typeDesc) {}
	ibPropertyValueRecord(const ibPropertyValueRecord &src) : ibPropertyValueMetaDesc(src.m_ownerProperty, src.m_metaDesc) {}

	virtual ibPropertyValueRecord* Clone() const {
		return new ibPropertyValueRecord(*this);
	}

	virtual ibString GetString() const override;

};

#endif // !_DOCUMENT_VARIANT_
