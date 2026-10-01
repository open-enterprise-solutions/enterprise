#ifndef __PROPERTY_FORMAT_H__
#define __PROPERTY_FORMAT_H__

#include "backend/propertyManager/property/propertyString.h"

// The property "Format" — how a value is SHOWN: a format string (`NFD=2; NGS= `, backend/formatString.h)
// written per language, because a decimal separator and a date order are the language's. An attribute
// carries one, and an input field or a table column may carry their own, which then overrides it
// (docs/private/format-property.md).
//
// ⭐ IT IS A TRANSLATED STRING: the cell, the storage, undo and MCP are ibPropertyTString's. What it
// adds is its reader, and in the property grid the format string constructor opened from each
// language's box (wxFormatStringProperty).
class BACKEND_API ibPropertyFormat : public ibPropertyTString {
public:

	// THE FORMAT STRINGS, one per language, as stored. Which language is read is the reader's question
	// (ibTranslateString::GetString — the language in force).
	const ibTranslateString& GetValueAsFormatString() const { return GetValueAsTranslate(); }

	ibPropertyFormat(ibPropertyCategory* cat, const wxString& name,
		const ibTranslateString& value) : ibPropertyTString(cat, name, value)
	{
	}

	ibPropertyFormat(ibPropertyCategory* cat, const wxString& name, const wxString& label,
		const ibTranslateString& value) : ibPropertyTString(cat, name, label, value)
	{
	}

	ibPropertyFormat(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxString& helpString,
		const ibTranslateString& value) : ibPropertyTString(cat, name, label, helpString, value)
	{
	}
};

#endif
