#ifndef __TRANSLATE_VARIANT_H__
#define __TRANSLATE_VARIANT_H__

#include "backend/propertyManager/propertyValue.h"
#include "backend/backend_localization.h"

// The cell that holds an ibTranslateString — the same shape as the number cell over ibNumber: the
// value lives in its own type, the cell only carries it into a property.
class BACKEND_API ibPropertyValueTranslate : public ibPropertyValue {
public:

	void SetTranslate(const ibTranslateString& translate) { m_translate = translate; }
	ibTranslateString& GetTranslate() { return m_translate; }

	ibPropertyValueTranslate(const ibTranslateString& translate = ibTranslateString()) : ibPropertyValue(ibValueTypes::TYPE_VALUE), m_translate(translate) {}

	virtual bool CompareValueEQ(const ibPropertyValue& data) const override {
		const ibPropertyValueTranslate* srcData = dynamic_cast<const ibPropertyValueTranslate*>(data.GetRef());
		if (srcData != nullptr) {
			return m_translate == srcData->m_translate;
		}
		return false;
	}

	// ⚠ WHAT A PERSON READS, not the stored form: this is the string a grid cell, a log line or a
	// tooltip shows, and the language it is in is the one in force. The stored form is asked for by
	// name (ibTranslateString::GetRawText).
	virtual ibString GetString() const override { return m_translate.GetString(); }

private:
	ibTranslateString m_translate;
};

#endif
