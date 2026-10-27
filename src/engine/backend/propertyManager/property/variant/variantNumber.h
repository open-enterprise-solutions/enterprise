#ifndef __NUMBER_VARIANT_H__
#define __NUMBER_VARIANT_H__

#include "backend/backend_core.h"
#include "backend/propertyManager/propertyValue.h"

class BACKEND_API ibPropertyValueNumber : public ibPropertyValue {
public:

	void SetNumber(const ibNumber& number) { m_number = number; }
	ibNumber& GetNumber() { return m_number; }

	ibPropertyValueNumber(const ibNumber& number = 0.0f) : ibPropertyValue(ibValueTypes::TYPE_VALUE), m_number(number) {}

	virtual bool CompareValueEQ(const ibPropertyValue& data) const override {
		const ibPropertyValueNumber* srcData = dynamic_cast<const ibPropertyValueNumber*>(data.GetRef());
		if (srcData != nullptr) {
			return m_number == srcData->m_number;
		}
		return false;
	}

	virtual ibString GetString() const override;

private:
	ibNumber m_number;
};

#endif