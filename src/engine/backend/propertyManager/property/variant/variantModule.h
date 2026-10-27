#ifndef __MODULE_VARIANT_H__
#define __MODULE_VARIANT_H__

#include "backend/backend_core.h"
#include "core/variant.h"

class BACKEND_API ibVariantModule : public ibVariant {
public:

	//set module code 
	void SetModuleText(const ibString& moduleText) { m_moduleData = moduleText; }
	const ibString& GetModuleText() const { return m_moduleData; }

	ibVariantModule(const ibString& moduleData = ibEmptyString) : ibVariant(ibValueTypes::TYPE_VALUE), m_moduleData(moduleData) {}
	ibVariantModule(const ibVariantModule& src) : ibVariant(ibValueTypes::TYPE_VALUE), m_moduleData(src.m_moduleData) {}

	virtual bool CompareValueEQ(const ibVariant& data) const override {
		const ibVariantModule* srcData = dynamic_cast<const ibVariantModule*>(data.GetRef());
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