#ifndef __NUMBER_VARIANT_H__
#define __NUMBER_VARIANT_H__

#include "backend/backend_core.h"
#include "core/variant.h"

class BACKEND_API ibVariantNumber : public ibVariant {
public:

	void SetNumber(const ibNumber& number) { m_number = number; }
	ibNumber& GetNumber() { return m_number; }

	ibVariantNumber(const ibNumber& number = 0.0f) : ibVariant(ibValueTypes::TYPE_VALUE), m_number(number) {}
	ibVariantNumber(const ibVariantNumber& src) : ibVariant(ibValueTypes::TYPE_VALUE), m_number(src.m_number) {}

	virtual bool CompareValueEQ(const ibVariant& data) const override {
		const ibVariantNumber* srcData = dynamic_cast<const ibVariantNumber*>(data.GetRef());
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