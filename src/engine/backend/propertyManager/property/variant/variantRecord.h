#ifndef __RECORD_VARIANT_H__
#define __RECORD_VARIANT_H__

#include "backend/propertyManager/property/variant/variantMetaDesc.h"

class BACKEND_API ibVariantDataRecord : public ibVariantDataMetaDesc {
	wxString MakeString() const;
public:

	ibValue GetDataValue() const;

	ibVariantDataRecord(const ibPropertyObject* prop, const ibMetaDescription& typeDesc) : ibVariantDataMetaDesc(prop, typeDesc) {}
	ibVariantDataRecord(const ibVariantDataRecord &src) : ibVariantDataMetaDesc(src.m_ownerProperty, src.m_metaDesc) {}

	virtual ibVariantDataRecord* Clone() const {
		return new ibVariantDataRecord(*this);
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

	virtual wxString GetType() const { return wxT("ibVariantDataRecord"); }
};

#endif // !_DOCUMENT_VARIANT_
