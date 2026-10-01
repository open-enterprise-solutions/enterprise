#ifndef __TRANSLATE_VARIANT_H__
#define __TRANSLATE_VARIANT_H__

#include "backend/backend_localization.h"

// The cell that holds an ibTranslateString — the same shape as the number cell over ibNumber: the
// value lives in its own type, the cell only carries it into a property.
class BACKEND_API ibVariantDataTranslate : public wxVariantData {
	wxString MakeString() const { return m_translate.GetString(); }
public:

	void SetTranslate(const ibTranslateString& translate) { m_translate = translate; }
	ibTranslateString& GetTranslate() { return m_translate; }

	ibVariantDataTranslate(const ibTranslateString& translate = ibTranslateString()) : m_translate(translate) {}

	virtual bool Eq(wxVariantData& data) const {
		ibVariantDataTranslate* srcData = dynamic_cast<ibVariantDataTranslate*>(&data);
		if (srcData != nullptr) {
			return m_translate == srcData->GetTranslate();
		}
		return false;
	}

#if wxUSE_STD_IOSTREAM
	virtual bool Write(wxSTD ostream& str) const {
		str << MakeString();
		return true;
	}
#endif

	// ⚠ WHAT A PERSON READS, not the stored form: this is the string a grid cell, a log line or a
	// tooltip shows, and the language it is in is the one in force. The stored form is asked for by
	// name (ibTranslateString::GetRawText).
	virtual bool Write(wxString& str) const {
		str = MakeString();
		return true;
	}

	virtual wxString GetType() const { return wxT("ibVariantDataTranslate"); }

private:
	ibTranslateString m_translate;
};

#endif
