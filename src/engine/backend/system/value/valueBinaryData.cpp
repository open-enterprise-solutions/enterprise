////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : value binary data
////////////////////////////////////////////////////////////////////////////

#include "valueBinaryData.h"

#include <wx/base64.h>

#include <cstring>


ibValueBinaryData::ibValueBinaryData() : ibValue(ibValueTypes::TYPE_VALUE, true), m_data() {}

ibValueBinaryData::ibValueBinaryData(const wxMemoryBuffer& data) : ibValue(ibValueTypes::TYPE_VALUE, true), m_data(data) {}

ibValueBinaryData::ibValueBinaryData(const void* data, size_t length) : ibValue(ibValueTypes::TYPE_VALUE, true), m_data()
{
	if (data != nullptr && length > 0)
		m_data.AppendData(data, length);
}

bool ibValueBinaryData::Init()
{
	m_data.Clear();   // New BinaryData() — empty content, which is a legitimate value, not a failure
	return true;
}

bool ibValueBinaryData::Init(ibValue** paParams, const long lSizeArray)
{
	if (lSizeArray < 1)
		return false;

	// From TEXT — base64, the same form GetString hands out, so a value written to text and read
	// back keeps its bytes. Anything that is not valid base64 is a refusal rather than a silent
	// empty: bytes nobody can reconstruct are not the same thing as no bytes.
	if (paParams[0]->GetType() == ibValueTypes::TYPE_STRING) {
		size_t posError = wxCONV_FAILED;
		const wxMemoryBuffer decoded = wxBase64Decode(paParams[0]->GetString(), wxNO_LEN,
			wxBase64DecodeMode_Strict, &posError);
		if (posError != wxCONV_FAILED)
			return false;
		m_data = decoded;
		return true;
	}

	// From another BINARY value — a copy, by the same door a caller would expect assignment to take.
	if (const ibValueBinaryData* other = dynamic_cast<const ibValueBinaryData*>(paParams[0]->GetRef())) {
		m_data = other->GetBuffer();
		return true;
	}

	return false;
}

wxString ibValueBinaryData::GetString() const
{
	if (m_data.GetDataLen() == 0)
		return wxEmptyString;
	return wxBase64Encode(m_data.GetData(), m_data.GetDataLen());
}

// Equality is over the BYTES, never over their text: two buffers are the same value when they hold
// the same content, and comparing base64 would answer the same question after paying for two
// encodings. A non-binary operand is compared through its text, which is the only form both sides
// share.
bool ibValueBinaryData::CompareValueEQ(const ibValue& cParam) const
{
	if (const ibValueBinaryData* other = dynamic_cast<const ibValueBinaryData*>(cParam.GetRef())) {
		const size_t length = m_data.GetDataLen();
		if (length != other->m_data.GetDataLen())
			return false;
		return length == 0 || std::memcmp(m_data.GetData(), other->m_data.GetData(), length) == 0;
	}
	return GetString() == cParam.GetString();
}

bool ibValueBinaryData::CompareValueNE(const ibValue& cParam) const
{
	return !CompareValueEQ(cParam);
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_TYPE_REGISTER(ibValueBinaryData, "BinaryData", value_to_clsid("VL_BDAT"));
