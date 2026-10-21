#include "core/compiler/literal.h"

ibLiteral::ibLiteral(const ibNumber& number)
	: m_typeClass(ibValueTypes::TYPE_NUMBER), m_text(number.ToString()), m_numeric(number)
{
	long long whole = 0;
	if (number.ToInt(whole) == 0)   // 0 — it fits
		m_number = static_cast<long>(whole);
}

ibLiteral::ibLiteral(const ibDateTime& date)
	: m_typeClass(ibValueTypes::TYPE_DATE), m_text(date.ToString()), m_date(date)
{
}

ibLiteral::ibLiteral(const ibLiteral& rhs)
	: ibAnyValue(), m_typeClass(rhs.m_typeClass), m_text(rhs.m_text), m_number(rhs.m_number),
	m_numeric(rhs.m_numeric), m_date(rhs.m_date)
{
}

ibLiteral::ibLiteral(ibLiteral&& rhs) noexcept
	: ibAnyValue(), m_typeClass(rhs.m_typeClass), m_text(std::move(rhs.m_text)), m_number(rhs.m_number),
	m_numeric(std::move(rhs.m_numeric)), m_date(std::move(rhs.m_date))
{
}

ibLiteral& ibLiteral::operator=(const ibLiteral& rhs)
{
	m_typeClass = rhs.m_typeClass;
	m_text = rhs.m_text;
	m_number = rhs.m_number;
	m_numeric = rhs.m_numeric;
	m_date = rhs.m_date;
	return *this;
}

ibLiteral& ibLiteral::operator=(ibLiteral&& rhs) noexcept
{
	m_typeClass = rhs.m_typeClass;
	m_text = std::move(rhs.m_text);
	m_number = rhs.m_number;
	m_numeric = std::move(rhs.m_numeric);
	m_date = std::move(rhs.m_date);
	return *this;
}

bool ibLiteral::IsEmpty() const
{
	switch (m_typeClass) {
	case ibValueTypes::TYPE_BOOLEAN: return m_number == 0;
	case ibValueTypes::TYPE_NUMBER:  return m_numeric.IsZero();
	case ibValueTypes::TYPE_DATE:    return m_date.IsEmpty();
	case ibValueTypes::TYPE_STRING:  return m_text.IsEmpty();
	default:                         return true;   // Undefined and Null
	}
}

bool ibLiteral::GetBoolean() const
{
	if (m_typeClass == ibValueTypes::TYPE_STRING) {
		ibBoolean flag;
		flag.FromString(m_text);
		return flag;
	}
	if (m_typeClass == ibValueTypes::TYPE_NUMBER)
		return !m_numeric.IsZero();
	return m_number != 0;
}

ibNumber ibLiteral::GetNumber() const
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

ibDateTime ibLiteral::GetDate() const
{
	if (m_typeClass == ibValueTypes::TYPE_STRING) {
		ibDateTime date;
		date.FromString(m_text);
		return date;
	}
	return m_date;
}

ibString ibLiteral::GetString() const
{
	return m_text;
}

bool ibLiteral::SetBoolean(const wxString& text)
{
	ibBoolean flag;
	flag.FromString(text);
	*this = ibLiteral(flag);
	return true;
}

// A text that is not one leaves the value empty — the engine's way (ibValue::SetNumber resets first).
bool ibLiteral::SetNumber(const wxString& text)
{
	ibNumber number;
	if (!number.FromString(text)) {
		*this = ibLiteral();
		return false;
	}
	*this = ibLiteral(number);
	return true;
}

bool ibLiteral::SetDate(const wxString& text)
{
	ibDateTime date;
	if (!text.IsEmpty() && !date.FromString(text)) {
		*this = ibLiteral();
		return false;
	}
	*this = ibLiteral(date);
	return true;
}

bool ibLiteral::SetString(const wxString& text)
{
	*this = ibLiteral(text);
	return true;
}
