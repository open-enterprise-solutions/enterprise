#ifndef __NUMBER_VARIANT_H__
#define __NUMBER_VARIANT_H__

#include "frmclient/backend/backend_core.h"

// A DECIMAL IN A VARIANT — the engine's ibVariantDataNumber (backend/propertyManager/property/variant/variantNumber.h):
// the exact decimal the server stores (ibNumber, core), so the cell edits it without passing through a double.
class ibVariantDataNumber : public wxVariantData {
	wxString MakeString() const { return m_number.ToString(); }
public:

	void SetNumber(const ibNumber& number) { m_number = number; }
	ibNumber& GetNumber() { return m_number; }

	ibVariantDataNumber(const ibNumber& number = 0.0f) : m_number(number) {}

	virtual bool Eq(wxVariantData& data) const override {
		ibVariantDataNumber* srcData = dynamic_cast<ibVariantDataNumber*>(&data);
		if (srcData != nullptr) {
			return m_number == srcData->GetNumber();
		}
		return false;
	}

#if wxUSE_STD_IOSTREAM
	virtual bool Write(wxSTD ostream& str) const override {
		str << MakeString();
		return true;
	}
#endif

	virtual bool Write(wxString& str) const override {
		str = MakeString();
		return true;
	}

	virtual wxString GetType() const override { return wxT("ibVariantDataNumber"); }

private:
	ibNumber m_number;
};

#endif
