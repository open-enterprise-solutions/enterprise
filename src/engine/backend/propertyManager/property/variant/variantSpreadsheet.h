#ifndef __TEMPLATE_VARIANT_H__
#define __TEMPLATE_VARIANT_H__

#include "backend/propertyManager/propertyValue.h"
#include "backend/propertyManager/propertyObject.h"
#include "core/description/spreadsheetDescription.h"

class BACKEND_API ibPropertyValueSpreadsheet : public ibPropertyValue {
public:

	bool IsEmptySpreadsheet() const { return m_spreadsheetDesc.IsEmptySpreadsheet(); }

	ibSpreadsheetDescription& GetSpreadsheetDesc() { return m_spreadsheetDesc; }

	ibPropertyValueSpreadsheet(const ibSpreadsheetDescription& val) : ibPropertyValue(ibValueTypes::TYPE_VALUE), m_spreadsheetDesc(val) {}

	virtual bool CompareValueEQ(const ibPropertyValue& data) const override {
		const ibPropertyValueSpreadsheet* srcData = dynamic_cast<const ibPropertyValueSpreadsheet*>(data.GetRef());
		if (srcData != nullptr)
			return srcData->m_spreadsheetDesc == m_spreadsheetDesc;
		return false;
	}

	virtual ibString GetString() const override;

private:
	ibSpreadsheetDescription m_spreadsheetDesc;
};

#endif