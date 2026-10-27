#ifndef __ACTION_VARIANT_H__
#define __ACTION_VARIANT_H__

#include "backend/backend_core.h"
#include "backend/propertyManager/propertyValue.h"
#include "backend/standardCommand.h"

class BACKEND_API ibPropertyValueAction : public ibPropertyValue {
public:

	ibStandardCommandDescription& GetValueAsActionDesc() { return m_actionData; }

	ibPropertyValueAction(const ibStandardCommandDescription& act) : ibPropertyValue(ibValueTypes::TYPE_VALUE), m_actionData(act) {}

	virtual bool CompareValueEQ(const ibPropertyValue& data) const override { 
		const ibPropertyValueAction* srcData = dynamic_cast<const ibPropertyValueAction*>(data.GetRef());
		if (srcData != nullptr) return m_actionData == srcData->m_actionData;
		return false; 
	}

	virtual ibString GetString() const override;

private:
	ibStandardCommandDescription m_actionData;
};

#endif