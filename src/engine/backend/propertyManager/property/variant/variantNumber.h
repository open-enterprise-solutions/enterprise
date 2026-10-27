#ifndef __NUMBER_VARIANT_H__
#define __NUMBER_VARIANT_H__

#include "backend/backend_core.h"

class BACKEND_API ibVariantDataNumber : public wxVariantData {
	wxString MakeString() const;
public:

	void SetNumber(const ibNumber& number) { m_number = number; }
	ibNumber& GetNumber() { return m_number; }

	ibVariantDataNumber(const ibNumber& number = 0.0f) : m_number(number) {}

	virtual bool Eq(wxVariantData& data) const {
		ibVariantDataNumber* srcData = dynamic_cast<ibVariantDataNumber*>(&data);
		if (srcData != nullptr) {
			return m_number == srcData->GetNumber();
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

	virtual wxString GetType() const { return ibT("ibVariantDataNumber"); }

private:
	ibNumber m_number;
};

#endif