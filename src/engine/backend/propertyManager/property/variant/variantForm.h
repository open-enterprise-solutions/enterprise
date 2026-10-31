#ifndef __FORM_VARIANT_H__
#define __FORM_VARIANT_H__

#include "variantModule.h"

class BACKEND_API ibVariantForm : public ibVariantModule {
public:

	//set form code 
	void SetFormData(const ibMemoryBuffer& formData) { m_formData = formData; }
	ibMemoryBuffer& GetFormData() { return m_formData; }

	ibVariantForm(
		const ibMemoryBuffer& memory = ibMemoryBuffer(),
		const ibString& moduleData = ibEmptyString
	) : ibVariantModule(moduleData), m_formData(memory) {
	}
	ibVariantForm(const ibVariantForm& src) : ibVariantModule(src), m_formData(src.m_formData) {}

	virtual bool CompareValueEQ(const ibVariant& data) const override {
		const ibVariantForm* srcData = dynamic_cast<const ibVariantForm*>(data.GetRef());
		if (srcData != nullptr) {
			// The length first: an empty form holds no buffer at all, and memcmp is not given a null.
			return m_moduleData == srcData->m_moduleData &&
				srcData->m_formData.GetDataLen() == m_formData.GetDataLen() &&
				(m_formData.GetDataLen() == 0
					|| std::memcmp(m_formData.GetData(), srcData->m_formData.GetData(), m_formData.GetDataLen()) == 0);
		}
		return false;
	}

	virtual ibString GetString() const override;

private:
	ibMemoryBuffer m_formData;
};

#endif