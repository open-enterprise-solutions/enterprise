#ifndef __SIZE_VARIANT_H__
#define __SIZE_VARIANT_H__

#include "backend/backend_core.h"

// wxSize in a variant, our own — see variantPoint.h for why propgrid's
// WX_PG_DECLARE_VARIANT_DATA_EXPORTED(wxSize) / `variant << size` is not used.
class BACKEND_API ibVariantDataSize : public wxVariantData {
	wxString MakeString() const;
public:

	void SetSize(const wxSize& size) { m_size = size; }
	wxSize& GetSize() { return m_size; }

	ibVariantDataSize(const wxSize& size = wxDefaultSize) : m_size(size) {}

	virtual bool Eq(wxVariantData& data) const {
		ibVariantDataSize* srcData = dynamic_cast<ibVariantDataSize*>(&data);
		if (srcData != nullptr) {
			return m_size == srcData->GetSize();
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

	virtual wxString GetType() const { return wxT("ibVariantDataSize"); }

private:
	wxSize m_size;
};

#endif
