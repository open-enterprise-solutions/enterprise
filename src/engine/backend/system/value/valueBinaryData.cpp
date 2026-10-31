////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : value binary data
////////////////////////////////////////////////////////////////////////////

#include "valueBinaryData.h"
#include "valueTextFile.h"   // ibTextToBytes / ibTextFromBytes - a text in an encoding, and back

#include "backend/backend_exception.h"

#include "core/utils/base64.h"   // ibBase64 — the value as text, and back
#include "core/serialize/dataBuilder.h"   // ibDataNode — the packed form

#include <wx/ffile.h>
#include <wx/log.h>

#include <cstring>
#include <new>       // std::bad_alloc — a file the heap has no room for
#include <string>

namespace {

// A text form of bytes - base64, hex - with its spaces and line breaks left out: neither has any, and a text from
// elsewhere often comes wrapped (at 76 characters, in mail).
ibString WithoutWhitespace(const ibString& text)
{
	ibString kept;
	kept.reserve(text.length());
	for (const ibChar c : text) {
		if (c != ibT(' ') && c != ibT('\t') && c != ibT('\r') && c != ibT('\n'))
			kept += c;
	}
	return kept;
}

int HexDigit(ibChar c)
{
	if (c >= ibT('0') && c <= ibT('9')) return c - ibT('0');
	if (c >= ibT('A') && c <= ibT('F')) return c - ibT('A') + 10;
	if (c >= ibT('a') && c <= ibT('f')) return c - ibT('a') + 10;
	return -1;
}

} // namespace

// Order MUST match ibValueBinaryData::Func - the method number is the index into this table.
void ibValueBinaryData_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	helper.AppendConstructor(0, ibT("BinaryData()"));
	helper.AppendConstructor(1, ibT("BinaryData(base64 : string)"));
	helper.AppendConstructor(2, ibT("BinaryData(text : string, encoding : TextEncoding)"));
	helper.AppendConstructor(3, ibT("BinaryData(text : string, encoding : TextEncoding, byteOrderMark : boolean)"));

	helper.AppendFunc(ibT("Size"), ibT("Size()"));
	helper.AppendFunc(ibT("Read"), 1, ibT("Read(path : string)"));
	helper.AppendFunc(ibT("Write"), 1, ibT("Write(path : string)"));
	helper.AppendFunc(ibT("GetString"), 1, ibT("GetString(encoding : TextEncoding)"));
}

ibValueBinaryData::ibValueBinaryData() : ibValueStaticMembers(ibValueTypes::TYPE_VALUE, true), m_data() {}

ibValueBinaryData::ibValueBinaryData(const ibMemoryBuffer& data) : ibValueStaticMembers(ibValueTypes::TYPE_VALUE, true), m_data(data) {}

ibValueBinaryData::ibValueBinaryData(const void* data, size_t length) : ibValueStaticMembers(ibValueTypes::TYPE_VALUE, true), m_data()
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

	// From a TEXT IN AN ENCODING — the bytes it spells. The mark is asked for, never added by itself: in front of
	// JSON or the first field of a CSV it is what the reader at the other end breaks on. Only the two Unicode
	// encodings have one.
	if (lSizeArray > 1) {
		if (paParams[0]->GetType() != ibValueTypes::TYPE_STRING)
			ibBackendCoreException::Error(_("BinaryData: the text is a string"));
		const ibTextEncoding encoding = ibTextEncodingAt(paParams, lSizeArray, 1);
		std::string bytes;
		if (!ibTextToBytes(paParams[0]->GetString(), encoding, bytes))
			ibBackendCoreException::Error(_("BinaryData: the text cannot be written in the encoding given"));
		if (lSizeArray > 2 && paParams[2]->GetBoolean()) {
			if (encoding == ibTextEncoding_UTF8)
				bytes.insert(0, "\xEF\xBB\xBF", 3);
			else if (encoding == ibTextEncoding_UTF16)
				bytes.insert(0, "\xFF\xFE", 2);
		}
		m_data.Clear();
		m_data.AppendData(bytes.data(), bytes.size());
		return true;
	}

	// From TEXT — base64, the same form GetString hands out, so a value written to text and read
	// back keeps its bytes. Anything that is not valid base64 is a refusal rather than a silent
	// empty: bytes nobody can reconstruct are not the same thing as no bytes.
	if (paParams[0]->GetType() == ibValueTypes::TYPE_STRING) {
		if (!ibBase64::Read(WithoutWhitespace(paParams[0]->GetString()), m_data))
			ibBackendCoreException::Error(_("BinaryData: the text is not base64"));
		return true;
	}

	// From another BINARY value — a copy, by the same door a caller would expect assignment to take.
	m_data = paParams[0]->ConvertToType<ibValueBinaryData>()->GetBuffer();
	return true;
}

void ibValueBinaryData::ReadWholeFile(const ibString& fileName, const ibString& who, ibMemoryBuffer& bytes)
{
	wxFFile file;
	{
		wxLogNull quiet;   // a missing file is said in the platform's words, not in the library's log
		file.Open(fileName, wxT("rb"));
	}
	if (!file.IsOpened())
		ibBackendCoreException::Error(_("%s: cannot open the file '%s' for reading"), who, fileName);

	const wxFileOffset length = file.Length();
	if (length <= 0) {
		bytes = ibMemoryBuffer(0);
		return;
	}

	// ⚠ A FILE THAT DOES NOT FIT IS A REFUSAL, NOT A CRASH AND NOT A PART OF IT. A length past size_t (a file
	// over 4 GB in a 32-bit build) would wrap, and the head of the file be handed over as the whole of it; and
	// room the heap does not have is std::bad_alloc from the buffer, said here in the platform's words.
	const size_t size = static_cast<size_t>(length);
	if (static_cast<wxFileOffset>(size) != length)
		ibBackendCoreException::Error(_("%s: the file '%s' is too large to be read whole"), who, fileName);

	ibMemoryBuffer whole;
	void* place = nullptr;
	try {
		place = whole.GetWriteBuf(size);
	}
	catch (const std::bad_alloc&) {
		ibBackendCoreException::Error(_("%s: the file '%s' is too large to be read whole"), who, fileName);
	}

	if (file.Read(place, size) != size)
		ibBackendCoreException::Error(_("%s: reading the file '%s' failed"), who, fileName);
	whole.UngetWriteBuf(size);
	bytes = whole;
}

void ibValueBinaryData::ReadFile(const ibString& fileName)
{
	ibMemoryBuffer bytes;
	ReadWholeFile(fileName, ibT("BinaryData"), bytes);
	m_data = bytes;
}

void ibValueBinaryData::WriteFile(const ibString& fileName) const
{
	wxFFile file;
	{
		wxLogNull quiet;
		file.Open(fileName, wxT("wb"));
	}
	if (!file.IsOpened())
		ibBackendCoreException::Error(_("BinaryData: cannot open the file '%s' for writing"), fileName);
	const size_t length = m_data.GetDataLen();
	if (length > 0 && file.Write(m_data.GetData(), length) != length)
		ibBackendCoreException::Error(_("BinaryData: writing to the file '%s' failed"), fileName);

	// The bytes are on disk when the file CLOSED, not when Write answered: the stream is buffered, and a full
	// disk or a share that dropped is reported by the close. After a close that failed the stream is gone all
	// the same, so the handle is let go of rather than closed a second time by the destructor.
	bool closed = false;
	{
		wxLogNull quiet;
		closed = file.Close();
	}
	if (!closed) {
		file.Detach();
		ibBackendCoreException::Error(_("BinaryData: writing to the file '%s' failed"), fileName);
	}
}

bool ibValueBinaryData::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum) {
	case enSize:
		pvarRetValue = ibNumber(static_cast<long long>(m_data.GetDataLen()));
		return true;
	case enRead:
		if (lSizeArray < 1)
			return false;
		ReadFile(paParams[0]->GetString());
		return true;
	case enWrite:
		if (lSizeArray < 1)
			return false;
		WriteFile(paParams[0]->GetString());
		return true;
	case enGetString: {
		if (lSizeArray < 1)
			return false;
		ibString text;
		if (!ibTextFromBytes(m_data.GetData(), m_data.GetDataLen(), ibTextEncodingAt(paParams, lSizeArray, 0), text))
			ibBackendCoreException::Error(_("BinaryData: the bytes are not text in the encoding given"));
		pvarRetValue = text;
		return true;
	}
	}
	return false;
}

ibString ibValueBinaryData::GetString() const
{
	ibString text;
	ibBase64::Append(text, m_data.GetData(), m_data.GetDataLen());
	return text;
}

// Upper case, two digits a byte, nothing between them - the form a digest is compared in once Lower() has been
// applied to it. A string is NOT read as bytes here: which bytes a text is depends on an encoding, and that is
// New BinaryData's to be told.
ibString ibValueBinaryData::HexString(const ibValue& data)
{
	static const ibChar s_digits[] = ibT("0123456789ABCDEF");
	const ibMemoryBuffer& bytes = data.ConvertToType<ibValueBinaryData>()->m_data;
	const unsigned char* const at = static_cast<const unsigned char*>(bytes.GetData());
	const size_t length = bytes.GetDataLen();

	ibString text;
	text.reserve(length * 2);
	for (size_t i = 0; i < length; ++i) {
		text += s_digits[at[i] >> 4];
		text += s_digits[at[i] & 0x0F];
	}
	return text;
}

// ⚠ TEXT THAT IS NOT HEX IS A REFUSAL, NOT AN EMPTY VALUE. Bytes nobody can reconstruct are not the same thing as no
// bytes, and a signature checked against nothing compares as "different" for the wrong reason.
ibValue ibValueBinaryData::HexValue(const ibString& text)
{
	const ibString digits = WithoutWhitespace(text);
	if (digits.length() % 2 != 0)
		ibBackendCoreException::Error(_("HexValue: the text is not hex - its digits do not pair into bytes"));

	std::string bytes;
	bytes.reserve(digits.length() / 2);
	for (size_t i = 0; i < digits.length(); i += 2) {
		const int high = HexDigit(digits[i]), low = HexDigit(digits[i + 1]);
		if (high < 0 || low < 0)
			ibBackendCoreException::Error(_("HexValue: the text is not hex"));
		bytes += static_cast<char>((high << 4) | low);
	}
	const ibValuePtr<ibValueBinaryData> made(new ibValueBinaryData(bytes.data(), bytes.size()));
	return made;
}

ibString ibValueBinaryData::Base64String(const ibValue& data)
{
	return data.ConvertToType<ibValueBinaryData>()->GetString();
}

ibValue ibValueBinaryData::Base64Value(const ibString& text)
{
	const ibValuePtr<ibValueBinaryData> made(new ibValueBinaryData());
	if (!ibBase64::Read(WithoutWhitespace(text), made->m_data))
		ibBackendCoreException::Error(_("Base64Value: the text is not base64"));
	return made;
}

// Equality is over the BYTES, never over their text: two buffers are the same value when they hold
// the same content, and comparing base64 would answer the same question after paying for two
// encodings. A non-binary operand is compared through its text, which is the only form both sides
// share.
bool ibValueBinaryData::CompareValueEQ(const ibVariant& cParam) const
{
	ibValueBinaryData* other = nullptr;
	if (cParam.ConvertToValue(other)) {
		const size_t length = m_data.GetDataLen();
		if (length != other->m_data.GetDataLen())
			return false;
		return length == 0 || std::memcmp(m_data.GetData(), other->m_data.GetData(), length) == 0;
	}
	return GetString() == cParam.GetString();
}

bool ibValueBinaryData::CompareValueNE(const ibVariant& cParam) const
{
	return !CompareValueEQ(cParam);
}

bool ibValueBinaryData::DoSerialize(ibDataNode& node) const
{
	node.SetValue(kValueFieldData, m_data);
	return true;
}

bool ibValueBinaryData::DoDeserialize(const ibDataNode& node)
{
	const auto stored = node.FindField(kValueFieldData);
	if (!stored)
		m_data = ibMemoryBuffer();
	else if (stored->Kind() == ibDataKind::Binary)
		m_data = stored->AsBinary();
	else
		return false;
	return true;
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_TYPE_REGISTER(ibValueBinaryData, "BinaryData", value_to_clsid("VL_BDAT"));
