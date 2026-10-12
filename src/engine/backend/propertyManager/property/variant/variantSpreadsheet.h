#ifndef __TEMPLATE_VARIANT_H__
#define __TEMPLATE_VARIANT_H__

#include "backend/propertyManager/propertyObject.h"
#include "core/description/spreadsheetDescription.h"

class BACKEND_API ibVariantDataSpreadsheet : public wxVariantData {
	wxString MakeString() const;
public:

	bool IsEmptySpreadsheet() const { return m_spreadsheetDesc.IsEmptySpreadsheet(); }

	ibSpreadsheetDescription& GetSpreadsheetDesc() { return m_spreadsheetDesc; }

	ibVariantDataSpreadsheet(const ibSpreadsheetDescription& val) : wxVariantData(), m_spreadsheetDesc(val) {}

	virtual bool Eq(wxVariantData& data) const {
		ibVariantDataSpreadsheet* srcData = dynamic_cast<ibVariantDataSpreadsheet*>(&data);
		if (srcData != nullptr)
			return srcData->m_spreadsheetDesc == m_spreadsheetDesc;
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

	virtual wxString GetType() const { return wxT("ibVariantDataSpreadsheet"); }

private:
	ibSpreadsheetDescription m_spreadsheetDesc;
};

#endif