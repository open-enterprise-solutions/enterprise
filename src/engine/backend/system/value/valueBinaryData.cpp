////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : value binary data
////////////////////////////////////////////////////////////////////////////

#include "valueBinaryData.h"

#include "backend/backend_exception.h"

#include <wx/base64.h>
#include <wx/ffile.h>
#include <wx/log.h>

#include <cstring>


// Order MUST match ibValueBinaryData::Func - the method number is the index into this table.
void ibValueBinaryData_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	helper.AppendConstructor(0, wxT("BinaryData()"));
	helper.AppendConstructor(1, wxT("BinaryData(base64 : string)"));

	helper.AppendFunc(wxT("Size"), wxT("Size()"));
	helper.AppendFunc(wxT("Read"), 1, wxT("Read(path : string)"));
	helper.AppendFunc(wxT("Write"), 1, wxT("Write(path : string)"));
}

ibValueBinaryData::ibValueBinaryData() : ibValueStaticMembers(ibValueTypes::TYPE_VALUE, true), m_data() {}

ibValueBinaryData::ibValueBinaryData(const wxMemoryBuffer& data) : ibValueStaticMembers(ibValueTypes::TYPE_VALUE, true), m_data(data) {}

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

	// From TEXT — base64, the same form GetString hands out, so a value written to text and read
	// back keeps its bytes. Anything that is not valid base64 is a refusal rather than a silent
	// empty: bytes nobody can reconstruct are not the same thing as no bytes.
	if (paParams[0]->GetType() == ibValueTypes::TYPE_STRING) {
		size_t posError = wxCONV_FAILED;
		const std::string text = paParams[0]->GetString().ToUtf8();   // base64 is ASCII
		const wxMemoryBuffer decoded = wxBase64Decode(text.c_str(), text.size(),
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

void ibValueBinaryData::ReadWholeFile(const wxString& fileName, const wxString& who, wxMemoryBuffer& bytes)
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
		bytes = wxMemoryBuffer(0);
		return;
	}

	// ⚠ A FILE THAT DOES NOT FIT IS A REFUSAL, NOT A CRASH AND NOT A PART OF IT. The buffer's constructor does
	// not say when it got no memory - it hands back a null block, and reading into that is an access violation;
	// and a length past size_t (a file over 4 GB in a 32-bit build) would wrap, and the head of the file be
	// handed over as the whole of it.
	const size_t size = static_cast<size_t>(length);
	wxMemoryBuffer whole(static_cast<wxFileOffset>(size) == length ? size : 0);
	if (static_cast<wxFileOffset>(size) != length || whole.GetData() == nullptr)
		ibBackendCoreException::Error(_("%s: the file '%s' is too large to be read whole"), who, fileName);

	if (file.Read(whole.GetData(), size) != size)
		ibBackendCoreException::Error(_("%s: reading the file '%s' failed"), who, fileName);
	whole.SetDataLen(size);
	bytes = whole;
}

void ibValueBinaryData::ReadFile(const wxString& fileName)
{
	wxMemoryBuffer bytes;
	ReadWholeFile(fileName, wxT("BinaryData"), bytes);
	m_data = bytes;
}

void ibValueBinaryData::WriteFile(const wxString& fileName) const
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
	}
	return false;
}

ibString ibValueBinaryData::GetString() const
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
