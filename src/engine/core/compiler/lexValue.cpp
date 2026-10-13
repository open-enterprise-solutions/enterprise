#include "core/compiler/lexValue.h"

#include "core/stringUtils.h"

ibLexValue::ibLexValue(const ibNumber& number)
	: m_typeClass(ibValueTypes::TYPE_NUMBER), m_text(number.ToString()), m_numeric(number)
{
	long long whole = 0;
	if (number.ToInt(whole) == 0)   // 0 — it fits
		m_number = static_cast<long>(whole);
}

ibLexValue::ibLexValue(const ibDateTime& date)
	: m_typeClass(ibValueTypes::TYPE_DATE), m_text(date.ToString()), m_date(date)
{
}

ibLexValue::ibLexValue(const ibLexValue& rhs)
	: ibAnyValue(), m_typeClass(rhs.m_typeClass), m_text(rhs.m_text), m_number(rhs.m_number),
	m_numeric(rhs.m_numeric), m_date(rhs.m_date)
{
}

ibLexValue::ibLexValue(ibLexValue&& rhs) noexcept
	: ibAnyValue(), m_typeClass(rhs.m_typeClass), m_text(std::move(rhs.m_text)), m_number(rhs.m_number),
	m_numeric(std::move(rhs.m_numeric)), m_date(std::move(rhs.m_date))
{
}

ibLexValue& ibLexValue::operator=(const ibLexValue& rhs)
{
	m_typeClass = rhs.m_typeClass;
	m_text = rhs.m_text;
	m_number = rhs.m_number;
	m_numeric = rhs.m_numeric;
	m_date = rhs.m_date;
	return *this;
}

ibLexValue& ibLexValue::operator=(ibLexValue&& rhs) noexcept
{
	m_typeClass = rhs.m_typeClass;
	m_text = std::move(rhs.m_text);
	m_number = rhs.m_number;
	m_numeric = std::move(rhs.m_numeric);
	m_date = std::move(rhs.m_date);
	return *this;
}

bool ibLexValue::IsEmpty() const
{
	switch (m_typeClass) {
	case ibValueTypes::TYPE_BOOLEAN: return m_number == 0;
	case ibValueTypes::TYPE_NUMBER:  return m_numeric.IsZero();
	case ibValueTypes::TYPE_DATE:    return m_date.IsEmpty();
	case ibValueTypes::TYPE_STRING:  return m_text.IsEmpty();
	default:                         return true;   // Undefined and Null
	}
}

bool ibLexValue::GetBoolean() const
{
	if (m_typeClass == ibValueTypes::TYPE_STRING)
		return m_text.IsSameAs(wxT("true"), false) || m_text.IsSameAs(_("True"), false) || m_text == wxT("1");
	if (m_typeClass == ibValueTypes::TYPE_NUMBER)
		return !m_numeric.IsZero();
	return m_number != 0;
}

ibNumber ibLexValue::GetNumber() const
{
	if (m_typeClass == ibValueTypes::TYPE_STRING) {
		// A decimal comma reads as the point — what a person types in the locale's way.
		wxString text = m_text;
		text.Trim().Trim(false);
		text.Replace(wxT(","), wxT("."));
		text.Replace(wxT(" "), wxEmptyString);
		ibNumber number;
		return number.FromString(text) ? number : ibNumber();
	}
	if (m_typeClass == ibValueTypes::TYPE_BOOLEAN)
		return ibNumber(m_number != 0 ? 1 : 0);
	return m_numeric;
}

ibDateTime ibLexValue::GetDate() const
{
	if (m_typeClass == ibValueTypes::TYPE_STRING) {
		ibDateTime date;
		date.FromString(m_text);
		return date;
	}
	return m_date;
}

ibString ibLexValue::GetString() const
{
	return m_text;
}

bool ibLexValue::SetBoolean(const wxString& text)
{
	*this = ibLexValue(stringUtils::CompareString(text, wxT("True")));
	return true;
}

// A text that is not one leaves the value empty — the engine's way (ibValue::SetNumber resets first).
bool ibLexValue::SetNumber(const wxString& text)
{
	ibNumber number;
	if (!number.FromString(text)) {
		*this = ibLexValue();
		return false;
	}
	*this = ibLexValue(number);
	return true;
}

bool ibLexValue::SetDate(const wxString& text)
{
	ibDateTime date;
	if (!text.IsEmpty() && !date.FromString(text)) {
		*this = ibLexValue();
		return false;
	}
	*this = ibLexValue(date);
	return true;
}

bool ibLexValue::SetString(const wxString& text)
{
	*this = ibLexValue(text);
	return true;
}
