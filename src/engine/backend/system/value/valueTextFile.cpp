////////////////////////////////////////////////////////////////////////////
//	Description : the text of a file, read and written by a script
////////////////////////////////////////////////////////////////////////////

#include "valueTextFile.h"
#include "valueBinaryData.h"   // ReadWholeFile - the one road a file is read into memory

#include "backend/backend_exception.h"
#include "backend/compiler/enumUnit.h"   // ConvertToEnumValue<> is declared in value.h and DEFINED here

#include <wx/ffile.h>
#include <wx/strconv.h>
#include <wx/fontenc.h>
#include <wx/log.h>

#ifdef __WXMSW__
#include <wx/msw/wrapwin.h>
#endif

//////////////////////////////////////////////////////////////////////
// The encoding, as the converter that spells it
//////////////////////////////////////////////////////////////////////

#ifdef __WXMSW__
namespace {

// A code page BY ITS NUMBER, converted by the system that owns it. The library's converter is reached by
// NAME, and knows the names of some pages and not of others (737, 775, 857-869, 65001 as an ANSI page): asked
// for one it does not know it falls back to Latin-1 without a word, and the file reads as garbage that
// looks like text. The number is what GetACP / GetOEMCP answer with, so nothing is spelled and nothing is
// guessed. Bytes the page has no character for are a FAILURE, as are characters the page has no bytes for.
class ibCodePageConv : public wxMBConv {
public:
	explicit ibCodePageConv(unsigned codePage) : m_codePage(codePage) {}

	virtual size_t ToWChar(wchar_t* dst, size_t dstLen, const char* src, size_t srcLen = wxNO_LEN) const override {
		const int got = ::MultiByteToWideChar(m_codePage, MB_ERR_INVALID_CHARS,
			src, srcLen == wxNO_LEN ? -1 : static_cast<int>(srcLen), dst, dst != nullptr ? static_cast<int>(dstLen) : 0);
		return got > 0 ? static_cast<size_t>(got) : wxCONV_FAILED;
	}

	virtual size_t FromWChar(char* dst, size_t dstLen, const wchar_t* src, size_t srcLen = wxNO_LEN) const override {
		// UTF-8 and UTF-7 have bytes for everything and refuse to be asked whether a default was used.
		const bool lossy = m_codePage != CP_UTF8 && m_codePage != CP_UTF7;
		BOOL usedDefault = FALSE;
		const int got = ::WideCharToMultiByte(m_codePage, lossy ? WC_NO_BEST_FIT_CHARS : 0,
			src, srcLen == wxNO_LEN ? -1 : static_cast<int>(srcLen), dst, dst != nullptr ? static_cast<int>(dstLen) : 0,
			nullptr, lossy ? &usedDefault : nullptr);
		return got > 0 && !usedDefault ? static_cast<size_t>(got) : wxCONV_FAILED;
	}

	virtual wxMBConv* Clone() const override { return new ibCodePageConv(m_codePage); }

private:
	unsigned m_codePage;
};

} // namespace
#endif

// ANSI and OEM are asked of the SYSTEM, not assumed: a base moved from one machine to another reads the
// files of the machine it runs on. Elsewhere than Windows there is one such encoding - the locale's.
std::unique_ptr<wxMBConv> ibCreateTextConv(ibTextEncoding encoding)
{
	switch (encoding) {
	case ibTextEncoding_UTF16:
		return std::make_unique<wxMBConvUTF16LE>();
	case ibTextEncoding_ANSI:
#ifdef __WXMSW__
		return std::make_unique<ibCodePageConv>(::GetACP());
#else
		return std::make_unique<wxCSConv>(wxFONTENCODING_SYSTEM);
#endif
	case ibTextEncoding_OEM:
#ifdef __WXMSW__
		return std::make_unique<ibCodePageConv>(::GetOEMCP());
#else
		return std::make_unique<wxCSConv>(wxFONTENCODING_SYSTEM);
#endif
	case ibTextEncoding_System:
#ifdef __WXMSW__
		// On Windows the system's encoding IS its ANSI page - by the same road, for the same reason.
		return std::make_unique<ibCodePageConv>(::GetACP());
#else
		return std::make_unique<wxCSConv>(wxFONTENCODING_SYSTEM);
#endif
	case ibTextEncoding_UTF8:
	default:
		// STRICT: bytes that are not UTF-8 are a refusal, not a text with holes in it.
		return std::make_unique<wxMBConvStrictUTF8>();
	}
}

namespace {

ibTextEncoding EncodingOf(ibValue** paParams, const long lSizeArray, long at)
{
	if (lSizeArray > at && !paParams[at]->IsEmpty())
		return paParams[at]->ConvertToEnumValue<ibTextEncoding>();
	return ibTextEncoding_UTF8;
}

} // namespace

//////////////////////////////////////////////////////////////////////
// TextReader
//////////////////////////////////////////////////////////////////////

// Order MUST match ibValueTextReader::Func - the method number is the index into this table.
void ibValueTextReader_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	helper.AppendConstructor(1, wxT("TextReader(path : string)"));
	helper.AppendConstructor(2, wxT("TextReader(path : string, encoding : TextEncoding)"));

	helper.AppendFunc(wxT("Open"), 2, wxT("Open(path : string, encoding : TextEncoding)"));
	helper.AppendFunc(wxT("ReadLine"), wxT("ReadLine()"));
	helper.AppendFunc(wxT("Read"), wxT("Read()"));
	helper.AppendFunc(wxT("Close"), wxT("Close()"));
}

ibValueTextReader::ibValueTextReader() : ibValueStaticMembers(ibValueTypes::TYPE_VALUE, true) {}

ibValueTextReader::~ibValueTextReader() {}

bool ibValueTextReader::Init(ibValue** paParams, const long lSizeArray)
{
	if (lSizeArray < 1)
		return false;
	Open(paParams[0]->GetString(), EncodingOf(paParams, lSizeArray, 1));
	return true;
}

void ibValueTextReader::Open(const wxString& fileName, ibTextEncoding encoding)
{
	Close();

	// The bytes come whole, by the one road a file is read into memory (it refuses a file that does not fit).
	wxMemoryBuffer bytes;
	ibValueBinaryData::ReadWholeFile(fileName, wxT("TextReader"), bytes);

	const unsigned char* data = static_cast<const unsigned char*>(bytes.GetData());
	size_t size = bytes.GetDataLen(), skip = 0;

	// ⚠ A BYTE-ORDER MARK IS A MARK, NOT TEXT. Handed over, it becomes an invisible first character of the
	// first line - and the first column of a CSV never equals the name it is compared with.
	std::unique_ptr<wxMBConv> conv;
	if (encoding == ibTextEncoding_UTF8 && size >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) {
		skip = 3;
	}
	else if (encoding == ibTextEncoding_UTF16 && size >= 2 && data[0] == 0xFF && data[1] == 0xFE) {
		skip = 2;
	}
	else if (encoding == ibTextEncoding_UTF16 && size >= 2 && data[0] == 0xFE && data[1] == 0xFF) {
		skip = 2;
		conv = std::make_unique<wxMBConvUTF16BE>();   // the mark says which end comes first
	}
	if (!conv)
		conv = ibCreateTextConv(encoding);

	m_text.clear();
	if (size > skip) {
		m_text = wxString(static_cast<const char*>(bytes.GetData()) + skip, *conv, size - skip);
		// Bytes that do not spell text in this encoding decode to NOTHING - and an empty text out of a file
		// that is not empty would read as "the file has no lines", which is a different thing.
		if (m_text.empty())
			ibBackendCoreException::Error(_("TextReader: the file '%s' cannot be read in the encoding given"), fileName);
	}
	m_at = 0;
	m_loaded = true;
}

bool ibValueTextReader::ReadLine(wxString& line)
{
	if (!m_loaded)
		ibBackendCoreException::Error(_("TextReader: no file is open"));
	if (m_at >= m_text.length())
		return false;

	const size_t end = m_text.find(wxT('\n'), m_at);
	if (end == wxString::npos) {
		line = m_text.Mid(m_at);
		m_at = m_text.length();
	}
	else {
		line = m_text.Mid(m_at, end - m_at);
		m_at = end + 1;
	}
	if (!line.empty() && line.Last() == wxT('\r'))
		line.RemoveLast();   // a line ends the way its file says - CRLF as readily as LF
	return true;
}

wxString ibValueTextReader::ReadRest()
{
	if (!m_loaded)
		ibBackendCoreException::Error(_("TextReader: no file is open"));
	const wxString rest = m_at < m_text.length() ? m_text.Mid(m_at) : wxString();
	m_at = m_text.length();
	return rest;
}

void ibValueTextReader::Close()
{
	m_text.clear();
	m_at = 0;
	m_loaded = false;
}

bool ibValueTextReader::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum) {
	case enOpen:
		if (lSizeArray < 1)
			return false;
		Open(paParams[0]->GetString(), EncodingOf(paParams, lSizeArray, 1));
		return true;
	case enReadLine: {
		// THE END OF THE FILE IS Undefined - an empty line is a line.
		wxString line;
		if (ReadLine(line))
			pvarRetValue = line;
		else
			pvarRetValue = ibValue();
		return true;
	}
	case enRead:
		pvarRetValue = ReadRest();
		return true;
	case enClose:
		Close();
		return true;
	}
	return false;
}

//////////////////////////////////////////////////////////////////////
// TextWriter
//////////////////////////////////////////////////////////////////////

// Order MUST match ibValueTextWriter::Func.
void ibValueTextWriter_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	helper.AppendConstructor(1, wxT("TextWriter(path : string)"));
	helper.AppendConstructor(2, wxT("TextWriter(path : string, encoding : TextEncoding)"));
	helper.AppendConstructor(3, wxT("TextWriter(path : string, encoding : TextEncoding, append : boolean)"));

	helper.AppendFunc(wxT("Open"), 3, wxT("Open(path : string, encoding : TextEncoding, append : boolean)"));
	helper.AppendFunc(wxT("Write"), 1, wxT("Write(text : string)"));
	helper.AppendFunc(wxT("WriteLine"), 1, wxT("WriteLine(text : string)"));
	helper.AppendFunc(wxT("Close"), wxT("Close()"));
}

ibValueTextWriter::ibValueTextWriter() : ibValueStaticMembers(ibValueTypes::TYPE_VALUE, true) {}

// A writer nobody closed still leaves a whole file behind it: what was written is on disk.
ibValueTextWriter::~ibValueTextWriter()
{
	try { Close(); } catch (...) {}
}

bool ibValueTextWriter::IsOpen() const
{
	return m_file != nullptr && m_file->IsOpened();
}

bool ibValueTextWriter::Init(ibValue** paParams, const long lSizeArray)
{
	if (lSizeArray < 1)
		return false;
	Open(paParams[0]->GetString(), EncodingOf(paParams, lSizeArray, 1),
		lSizeArray > 2 && paParams[2]->GetBoolean());
	return true;
}

void ibValueTextWriter::Open(const wxString& fileName, ibTextEncoding encoding, bool append)
{
	Close();

	auto file = std::make_unique<wxFFile>();
	{
		wxLogNull quiet;
		file->Open(fileName, append ? wxT("ab") : wxT("wb"));
	}
	if (!file->IsOpened())
		ibBackendCoreException::Error(_("TextWriter: cannot open the file '%s' for writing"), fileName);

	m_file = std::move(file);
	m_conv = ibCreateTextConv(encoding);
	m_fileName = fileName;

	// UTF-16 carries its mark, or nothing that reads the file can tell which end comes first. UTF-8 does
	// not: the mark is what breaks the first field for every reader that does not expect one.
	if (encoding == ibTextEncoding_UTF16 && (!append || m_file->Length() == 0)) {
		const unsigned char mark[2] = { 0xFF, 0xFE };
		if (m_file->Write(mark, sizeof(mark)) != sizeof(mark))
			ibBackendCoreException::Error(_("TextWriter: writing to the file '%s' failed"), m_fileName);
	}
}

void ibValueTextWriter::Write(const wxString& text)
{
	if (!IsOpen())
		ibBackendCoreException::Error(_("TextWriter: no file is open"));
	if (text.empty())
		return;

	size_t length = 0;
	const wxCharBuffer bytes = m_conv->cWC2MB(text.wc_str(), text.length(), &length);
	if (length == 0 || length == wxCONV_FAILED)
		ibBackendCoreException::Error(_("TextWriter: the text cannot be written to '%s' in the encoding given"), m_fileName);
	if (m_file->Write(bytes.data(), length) != length)
		ibBackendCoreException::Error(_("TextWriter: writing to the file '%s' failed"), m_fileName);
}

// ⚠ THE TEXT IS ON DISK WHEN THE FILE CLOSED, NOT WHEN Write ANSWERED. The stream is buffered: a full disk, a
// share that dropped - Write says yes to the buffer and the CLOSE is what reports it. A close taken on trust
// lets a script delete its source believing the export whole. After a close that failed the stream is gone
// all the same, so the handle is let go of rather than closed a second time by the destructor.
void ibValueTextWriter::Close()
{
	if (m_file == nullptr)
		return;

	const std::unique_ptr<wxFFile> file = std::move(m_file);
	m_conv.reset();
	if (!file->IsOpened())
		return;

	bool closed = false;
	{
		wxLogNull quiet;
		closed = file->Close();
	}
	if (!closed) {
		file->Detach();
		ibBackendCoreException::Error(_("TextWriter: the file '%s' could not be written to the end"), m_fileName);
	}
}

bool ibValueTextWriter::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum) {
	case enOpen:
		if (lSizeArray < 1)
			return false;
		Open(paParams[0]->GetString(), EncodingOf(paParams, lSizeArray, 1),
			lSizeArray > 2 && paParams[2]->GetBoolean());
		return true;
	case enWrite:
		Write(lSizeArray > 0 ? paParams[0]->GetString() : wxString());
		return true;
	case enWriteLine:
		// One line ending, the same on every platform: a file written here reads the same there.
		Write((lSizeArray > 0 ? paParams[0]->GetString() : wxString()) + wxT("\n"));
		return true;
	case enClose:
		Close();
		return true;
	}
	return false;
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_TYPE_REGISTER(ibValueTextReader, "TextReader", value_to_clsid("VL_TXRD"));
VALUE_TYPE_REGISTER(ibValueTextWriter, "TextWriter", value_to_clsid("VL_TXWR"));
