#ifndef _FRMCLIENT_BACKEND_VARIANT_RECORD_H__
#define _FRMCLIENT_BACKEND_VARIANT_RECORD_H__

#include "frmclient/backend/propertyManager/property/variant/variantMetaDesc.h"

// THE REGISTERS A DOCUMENT POSTS TO — the engine's ibVariantDataRecord: the metaobjects it names (advpropRecord.cpp).
class ibVariantDataRecord : public ibVariantDataMetaDesc {
public:

	ibVariantDataRecord(const ibPropertyObject* prop, const ibMetaDescription& typeDesc) : ibVariantDataMetaDesc(prop, typeDesc) {}
	ibVariantDataRecord(const ibVariantDataRecord& src) : ibVariantDataMetaDesc(src.m_ownerProperty, src.m_metaDesc) {}

	virtual ibVariantDataRecord* Clone() const override {
		return new ibVariantDataRecord(*this);
	}

	virtual wxString GetType() const override { return wxT("ibVariantDataRecord"); }
};

#endif
