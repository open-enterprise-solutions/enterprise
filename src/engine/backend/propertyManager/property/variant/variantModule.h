#ifndef __MODULE_VARIANT_H__
#define __MODULE_VARIANT_H__

#include "backend/backend_core.h"

class BACKEND_API ibVariantDataModule : public wxVariantData {
	wxString MakeString() const;
public:

	//set module code 
	void SetModuleText(const ibString& moduleText) { m_moduleData = moduleText; }
	const ibString& GetModuleText() const { return m_moduleData; }

	ibVariantDataModule(const ibString& moduleData = ibEmptyString) : m_moduleData(moduleData) {}

	virtual bool Eq(wxVariantData& data) const {
		ibVariantDataModule* srcData = dynamic_cast<ibVariantDataModule*>(&data);
		if (srcData != nullptr) {
			return m_moduleData == srcData->GetModuleText();
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

	virtual wxString GetType() const { return ibT("ibVariantDataModule"); }

protected:
	ibString m_moduleData;
};

#endif