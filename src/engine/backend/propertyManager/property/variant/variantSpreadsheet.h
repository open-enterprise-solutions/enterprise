#ifndef __TEMPLATE_VARIANT_H__
#define __TEMPLATE_VARIANT_H__

#include "core/variant.h"
#include "backend/propertyManager/propertyObject.h"
#include "core/description/spreadsheetDescription.h"

class BACKEND_API ibVariantSpreadsheet : public ibVariant {
public:

	bool IsEmptySpreadsheet() const { return m_spreadsheetDesc.IsEmptySpreadsheet(); }

	ibSpreadsheetDescription& GetSpreadsheetDesc() { return m_spreadsheetDesc; }

	ibVariantSpreadsheet(const ibSpreadsheetDescription& val) : ibVariant(ibValueTypes::TYPE_VALUE), m_spreadsheetDesc(val) {}
	ibVariantSpreadsheet(const ibVariantSpreadsheet& src) : ibVariant(ibValueTypes::TYPE_VALUE), m_spreadsheetDesc(src.m_spreadsheetDesc) {}

	virtual bool CompareValueEQ(const ibVariant& data) const override {
		const ibVariantSpreadsheet* srcData = dynamic_cast<const ibVariantSpreadsheet*>(data.GetRef());
		if (srcData != nullptr)
			return srcData->m_spreadsheetDesc == m_spreadsheetDesc;
		return false;
	}

	virtual ibString GetString() const override;

private:
	ibSpreadsheetDescription m_spreadsheetDesc;
};

#endif