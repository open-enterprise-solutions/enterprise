#ifndef __FORM_VARIANT_H__
#define __FORM_VARIANT_H__

#include "variantModule.h"

class BACKEND_API ibVariantDataForm : public ibVariantDataModule {
	wxString MakeString() const;
public:

	//set form code 
	void SetFormData(const ibMemoryBuffer& formData) { m_formData = formData; }
	ibMemoryBuffer& GetFormData() { return m_formData; }

	ibVariantDataForm(
		const ibMemoryBuffer& memory = ibMemoryBuffer(),
		const wxString& moduleData = wxEmptyString
	) : ibVariantDataModule(moduleData), m_formData(memory) {
	}

	virtual bool Eq(wxVariantData& data) const {
		ibVariantDataForm* srcData = dynamic_cast<ibVariantDataForm*>(&data);
		if (srcData != nullptr) {
			return m_moduleData == srcData->m_moduleData &&
				std::memcmp(m_formData.GetData(), srcData->m_formData.GetData(), m_formData.GetDataLen()) == 0 && 
				srcData->m_formData.GetDataLen() == m_formData.GetDataLen();
		}
		return false;
	}

#if wxUSE_STD_IOSTREAM
	virtual bool Write(wxSTD ostream& str) const {
		str << MakeString();
		return true;
	}
#endif

	virtual bool Write(wxString& str) const {
		str = MakeString();
		return true;
	}

	virtual wxString GetType() const { return wxT("ibVariantDataForm"); }

private:
	ibMemoryBuffer m_formData;
};

#endif