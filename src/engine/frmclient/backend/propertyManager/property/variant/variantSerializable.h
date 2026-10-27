#ifndef __SERIALIZABLE_VARIANT_H__
#define __SERIALIZABLE_VARIANT_H__

#include <wx/variant.h>

#include "core/serialize/dataBuilder.h"   // ibDataValue — the value, as the server stores it

// ⭐ THE ONE VARIANT OF A SERVER'S PROPERTY (Max, 2026-10-20) — what ibPropertySerializable holds, for every type alike:
// its value as the server stores it (the node), its type the server's class, its words as the server wrote them
// (Presentation). Nothing is defined per type to hold it: the presentation of a type takes out of it what it shows
// (ibPropertyRegistry), and the object inspector moves it as it moves any variant, knowing nothing of what is inside.
class ibVariantDataSerializable : public wxVariantData {
public:

	ibVariantDataSerializable(const ibString& type, const ibDataValue& value, const ibString& presentation)
		: wxVariantData(), m_type(type), m_value(value), m_presentation(presentation) {}

	const ibDataValue& GetNodeValue() const { return m_value; }
	// …of a value a cell holds — none: it is not one of these.
	static ibDataValue GetNodeValue(const wxVariant& value) {
		const ibVariantDataSerializable* const data = dynamic_cast<const ibVariantDataSerializable*>(value.GetData());
		return data != nullptr ? data->m_value : ibDataValue();
	}

	virtual bool Eq(wxVariantData& data) const override {
		const ibVariantDataSerializable* const other = dynamic_cast<const ibVariantDataSerializable*>(&data);
		return other != nullptr && other->m_type == m_type && other->m_value == m_value;
	}

	virtual ibVariantDataSerializable* Clone() const override {
		return new ibVariantDataSerializable(m_type, m_value, m_presentation);
	}

#if wxUSE_STD_IOSTREAM
	virtual bool Write(wxSTD ostream& str) const override { str << m_presentation.utf8_str(); return true; }
#endif
	virtual bool Write(wxString& str) const override { str = m_presentation; return true; }

	virtual wxString GetType() const override { return m_type; }

private:

	ibString    m_type;           // the server's class
	ibDataValue m_value;          // the value, as the server stores it
	ibString    m_presentation;   // the words it reads as — the server's
};

#endif // !__SERIALIZABLE_VARIANT_H__
