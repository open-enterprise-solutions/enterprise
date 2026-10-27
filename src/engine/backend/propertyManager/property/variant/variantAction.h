#ifndef __ACTION_VARIANT_H__
#define __ACTION_VARIANT_H__

#include "backend/backend_core.h"
#include "core/variant.h"
#include "backend/standardCommand.h"

class BACKEND_API ibVariantAction : public ibVariant {
public:

	ibStandardCommandDescription& GetValueAsActionDesc() { return m_actionData; }

	ibVariantAction(const ibStandardCommandDescription& act) : ibVariant(ibValueTypes::TYPE_VALUE), m_actionData(act) {}
	ibVariantAction(const ibVariantAction& src) : ibVariant(ibValueTypes::TYPE_VALUE), m_actionData(src.m_actionData) {}

	virtual bool CompareValueEQ(const ibVariant& data) const override { 
		const ibVariantAction* srcData = dynamic_cast<const ibVariantAction*>(data.GetRef());
		if (srcData != nullptr) return m_actionData == srcData->m_actionData;
		return false; 
	}

	virtual ibString GetString() const override;

private:
	ibStandardCommandDescription m_actionData;
};

#endif