#ifndef __GEN_VARIANT_H__
#define __GEN_VARIANT_H__

#include "backend/propertyManager/property/variant/variantMetaDesc.h"

class BACKEND_API ibVariantDataGeneration : public ibVariantDataMetaDesc {
	wxString MakeString() const;
public:

	ibValue GetDataValue() const;

	ibVariantDataGeneration(const ibPropertyObject* prop, const ibMetaDescription& typeDesc) : ibVariantDataMetaDesc(prop, typeDesc) {}
	ibVariantDataGeneration(const ibVariantDataGeneration& src) : ibVariantDataMetaDesc(src.m_ownerProperty, src.m_metaDesc) {}

	virtual ibVariantDataGeneration* Clone() const {
		return new ibVariantDataGeneration(*this);
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

	virtual wxString GetType() const { return wxT("ibVariantDataGeneration"); }
};

#endif