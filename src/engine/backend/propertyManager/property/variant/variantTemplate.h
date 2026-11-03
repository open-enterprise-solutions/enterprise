#ifndef __TEMPLATE_VARIANT_H__
#define __TEMPLATE_VARIANT_H__

#include "core/variant.h"
#include "backend/propertyManager/propertyObject.h"
#include "core/description/templateDescription.h"

// The cell that holds a TEMPLATE DESCRIPTION — its kind and the schema of that kind.
class BACKEND_API ibVariantTemplate : public ibVariant {
public:

	// By reference — a caller edits in place, a description is heavy.
	ibTemplateDescription& GetTemplateDesc() { return m_templateDesc; }
	const ibTemplateDescription& GetTemplateDesc() const { return m_templateDesc; }

	ibVariantTemplate(const ibTemplateDescription& val) : ibVariant(ibValueTypes::TYPE_VALUE), m_templateDesc(val) {}
	ibVariantTemplate(const ibVariantTemplate& src) : ibVariant(ibValueTypes::TYPE_VALUE), m_templateDesc(src.m_templateDesc) {}

	virtual bool CompareValueEQ(const ibVariant& data) const override {
		const ibVariantTemplate* srcData = dynamic_cast<const ibVariantTemplate*>(data.GetRef());
		if (srcData != nullptr)
			return srcData->m_templateDesc == m_templateDesc;
		return false;
	}

	virtual ibString GetString() const override;

private:
	ibTemplateDescription m_templateDesc;
};

#endif
