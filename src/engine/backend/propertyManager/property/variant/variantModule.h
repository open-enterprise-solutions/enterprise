#ifndef __MODULE_VARIANT_H__
#define __MODULE_VARIANT_H__

#include "backend/backend_core.h"
#include "backend/propertyManager/propertyValue.h"

class BACKEND_API ibPropertyValueModule : public ibPropertyValue {
public:

	//set module code 
	void SetModuleText(const ibString& moduleText) { m_moduleData = moduleText; }
	const ibString& GetModuleText() const { return m_moduleData; }

	ibPropertyValueModule(const ibString& moduleData = ibEmptyString) : ibPropertyValue(ibValueTypes::TYPE_VALUE), m_moduleData(moduleData) {}

	virtual bool CompareValueEQ(const ibPropertyValue& data) const override {
		const ibPropertyValueModule* srcData = dynamic_cast<const ibPropertyValueModule*>(data.GetRef());
		if (srcData != nullptr) {
			return m_moduleData == srcData->GetModuleText();
		}
		return false;
	}

	virtual ibString GetString() const override;

protected:
	ibString m_moduleData;
};

#endif