#include "variantNumber.h"

ibString ibPropertyValueNumber::GetString() const
{
	return m_number.ToString();
}