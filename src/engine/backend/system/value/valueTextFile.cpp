////////////////////////////////////////////////////////////////////////////
//	Description : the text of a file, read and written by a script
////////////////////////////////////////////////////////////////////////////

#include "valueTextFile.h"
#include "valueBinaryData.h"   // ReadWholeFile - the one road a file is read into memory

#include "backend/backend_exception.h"
#include "backend/compiler/enumUnit.h"   // ConvertToEnumValue<> is declared in value.h and DEFINED here

#include "core/fileSystem.h"             // ibPathOf - the writer's file as the system names it

#include <fstream>

//////////////////////////////////////////////////////////////////////
// The encoding
//////////////////////////////////////////////////////////////////////

ibCodePage ibCodePageOf(ibTextEncoding encoding)
{
	switch (encoding) {
	case ibTextEncoding_UTF16:       return ibCodePage::Of(ibCodePage::kUtf16);
	case ibTextEncoding_ANSI:
	case ibTextEncoding_System:      return ibCodePage::Of(ibCodePage::kAnsi);   // on Windows the system's encoding IS its ANSI page
	case ibTextEncoding_OEM:         return ibCodePage::Of(ibCodePage::kOem);
	case ibTextEncoding_Windows1251: return ibCodePage::Of(ibCodePage::kWindows1251);
	case ibTextEncoding_Cp866:       return ibCodePage::Of(ibCodePage::kCp866);
	case ibTextEncoding_UTF8:
	default:                         return ibCodePage::Of(ibCodePage::kUtf8);
	}
}

ibTextEncoding ibTextEncodingAt(ibValue** paParams, const long lSizeArray, const long at)
{
	if (lSizeArray <= at || paParams[at]->GetType() == ibValueTypes::TYPE_EMPTY)
		return ibTextEncoding_UTF8;
	if (paParams[at]->GetType() != ibValueTypes::TYPE_STRING)
		return paParams[at]->ConvertToEnumValue<ibTextEncoding>();

	// A page by its NAME, as XML names it - one table, so a name means the same bytes on every machine.
	const ibString name = paParams[at]->GetString();
	switch (ibCodePage::Find(name).Kind()) {
	case ibCodePage::kUtf8:        return ibTextEncoding_UTF8;
	case ibCodePage::kUtf16:       return ibTextEncoding_UTF16;
	case ibCodePage::kWindows1251: return ibTextEncoding_Windows1251;
	case ibCodePage::kCp866:       return ibTextEncoding_Cp866;
	case ibCodePage::kUnknown:
	default:
		ibBackendCoreException::Error(_("The encoding '%s' is not supported; supported: %s"), name, ibCodePage::Supported());
	}
}

//////////////////////////////////////////////////////////////////////
// TextReader
//////////////////////////////////////////////////////////////////////

// Order MUST match ibValueTextReader::Func - the method number is the index into this table.
void ibValueTextReader_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	helper.AppendConstructor(1, ibT("TextReader(path : string)"));
	helper.AppendConstructor(2, ibT("TextReader(path : string, encoding : TextEncoding | string)"));

	helper.AppendFunc(ibT("Open"), 2, ibT("Open(path : string, encoding : TextEncoding | string)"));
	helper.AppendFunc(ibT("ReadLine"), ibT("ReadLine()"));
	helper.AppendFunc(ibT("Read"), ibT("Read()"));
	helper.AppendFunc(ibT("Close"), ibT("Close()"));
	helper.AppendFunc(ibT("LineCount"), ibT("LineCount()"));
	helper.AppendFunc(ibT("GetLine"), 1, ibT("GetLine(line : number)"));
}

ibValueTextReader::ibValueTextReader() : ibValueStaticMembers(ibValueTypes::TYPE_VALUE, true) {}

ibValueTextReader::~ibValueTextReader() {}

bool ibValueTextReader::Init(ibValue** paParams, const long lSizeArray)
{
	if (lSizeArray < 1)
		return false;
	Open(paParams[0]->GetString(), ibTextEncodingAt(paParams, lSizeArray, 1));
	return true;
}

void ibValueTextReader::Open(const ibString& fileName, ibTextEncoding encoding)
{
	Close();

	// The bytes come whole, by the one road a file is read into memory (it refuses a file that does not fit).
	ibMemoryBuffer bytes;
	ibValueBinaryData::ReadWholeFile(fileName, ibT("TextReader"), bytes);

	// Bytes that do not spell text in this encoding decode to NOTHING - and an empty text out of a file that
	// is not empty would read as "the file has no lines", which is a different thing.
	if (!ibCodePageOf(encoding).Decode(bytes, m_text))
		ibBackendCoreException::Error(_("TextReader: the file '%s' cannot be read in the encoding given"), fileName);

	// Where every line begins, found ONCE: ReadLine walks this index and GetLine jumps into it, so the two
	// cannot disagree about what a line is. A break at the very end ENDS the last line - it opens none after it.
	for (size_t start = 0; start < m_text.length(); ) {
		m_lineStarts.push_back(start);
		const size_t brk = m_text.find(ibT('\n'), start);
		if (brk == ibString::npos)
			break;
		start = brk + 1;
	}
	m_next = 0;
	m_loaded = true;
}

ibString ibValueTextReader::LineAt(size_t index) const
{
	const size_t start = m_lineStarts[index];
	const size_t brk = m_text.find(ibT('\n'), start);
	ibString line = (brk == ibString::npos) ? m_text.Mid(start) : m_text.Mid(start, brk - start);
	if (!line.empty() && line.Last() == ibT('\r'))
		line.RemoveLast();   // a line ends the way its file says - CRLF as readily as LF
	return line;
}

bool ibValueTextReader::ReadLine(ibString& line)
{
	if (!m_loaded)
		ibBackendCoreException::Error(_("TextReader: no file is open"));
	if (m_next >= m_lineStarts.size())
		return false;
	line = LineAt(m_next++);
	return true;
}

ibString ibValueTextReader::ReadRest()
{
	if (!m_loaded)
		ibBackendCoreException::Error(_("TextReader: no file is open"));
	const ibString rest = m_next < m_lineStarts.size() ? m_text.Mid(m_lineStarts[m_next]) : ibString();
	m_next = m_lineStarts.size();
	return rest;
}

size_t ibValueTextReader::LineCount() const
{
	if (!m_loaded)
		ibBackendCoreException::Error(_("TextReader: no file is open"));
	return m_lineStarts.size();
}

bool ibValueTextReader::GetLine(size_t number, ibString& line) const
{
	if (!m_loaded)
		ibBackendCoreException::Error(_("TextReader: no file is open"));
	if (number < 1 || number > m_lineStarts.size())
		return false;
	line = LineAt(number - 1);
	return true;
}

void ibValueTextReader::Close()
{
	m_text.clear();
	m_lineStarts.clear();
	m_next = 0;
	m_loaded = false;
}

bool ibValueTextReader::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum) {
	case enOpen:
		if (lSizeArray < 1)
			return false;
		Open(paParams[0]->GetString(), ibTextEncodingAt(paParams, lSizeArray, 1));
		return true;
	case enReadLine: {
		// THE END OF THE FILE IS Undefined - an empty line is a line.
		ibString line;
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
	case enLineCount:
		pvarRetValue = ibNumber(static_cast<long long>(LineCount()));
		return true;
	case enGetLine: {
		if (lSizeArray < 1)
			return false;
		// NO SUCH LINE IS Undefined, as the end of the file is for ReadLine - an empty line is a line.
		const int number = paParams[0]->GetInteger();
		ibString line;
		if (number > 0 && GetLine(static_cast<size_t>(number), line))
			pvarRetValue = line;
		else
			pvarRetValue = ibValue();
		return true;
	}
	}
	return false;
}

//////////////////////////////////////////////////////////////////////
// TextWriter
//////////////////////////////////////////////////////////////////////

// Order MUST match ibValueTextWriter::Func.
void ibValueTextWriter_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	helper.AppendConstructor(1, ibT("TextWriter(path : string)"));
	helper.AppendConstructor(2, ibT("TextWriter(path : string, encoding : TextEncoding | string)"));
	helper.AppendConstructor(3, ibT("TextWriter(path : string, encoding : TextEncoding | string, append : boolean)"));

	helper.AppendFunc(ibT("Open"), 3, ibT("Open(path : string, encoding : TextEncoding | string, append : boolean)"));
	helper.AppendFunc(ibT("Write"), 1, ibT("Write(text : string)"));
	helper.AppendFunc(ibT("WriteLine"), 1, ibT("WriteLine(text : string)"));
	helper.AppendFunc(ibT("Close"), ibT("Close()"));
}

ibValueTextWriter::ibValueTextWriter() : ibValueStaticMembers(ibValueTypes::TYPE_VALUE, true) {}

// A writer nobody closed still leaves a whole file behind it: what was written is on disk.
ibValueTextWriter::~ibValueTextWriter()
{
	try { Close(); } catch (...) {}
}

bool ibValueTextWriter::IsOpen() const
{
	return m_file != nullptr && m_file->is_open();
}

bool ibValueTextWriter::Init(ibValue** paParams, const long lSizeArray)
{
	if (lSizeArray < 1)
		return false;
	Open(paParams[0]->GetString(), ibTextEncodingAt(paParams, lSizeArray, 1),
		lSizeArray > 2 && paParams[2]->GetBoolean());
	return true;
}

void ibValueTextWriter::Open(const ibString& fileName, ibTextEncoding encoding, bool append)
{
	Close();

	const std::filesystem::path path = ibPathOf(fileName);
	auto file = std::make_unique<std::ofstream>(path, std::ios::binary | (append ? std::ios::app : std::ios::trunc));
	if (!file->is_open())
		ibBackendCoreException::Error(_("TextWriter: cannot open the file '%s' for writing"), fileName);

	m_file = std::move(file);
	m_page = ibCodePageOf(encoding);
	m_fileName = fileName;

	// UTF-16 carries its mark, or nothing that reads the file can tell which end comes first. UTF-8 does
	// not: the mark is what breaks the first field for every reader that does not expect one.
	std::error_code error;
	if (m_page.Kind() == ibCodePage::kUtf16 && (!append || std::filesystem::file_size(path, error) == 0)) {
		const char mark[2] = { '\xFF', '\xFE' };
		if (!m_file->write(mark, sizeof(mark)))
			ibBackendCoreException::Error(_("TextWriter: writing to the file '%s' failed"), m_fileName);
	}
}

void ibValueTextWriter::Write(const ibString& text)
{
	if (!IsOpen())
		ibBackendCoreException::Error(_("TextWriter: no file is open"));
	if (text.empty())
		return;

	ibMemoryBuffer bytes;
	if (!m_page.Encode(text, bytes))
		ibBackendCoreException::Error(_("TextWriter: the text cannot be written to '%s' in the encoding given"), m_fileName);
	if (!m_file->write(static_cast<const char*>(bytes.GetData()), static_cast<std::streamsize>(bytes.GetDataLen())))
		ibBackendCoreException::Error(_("TextWriter: writing to the file '%s' failed"), m_fileName);
}

// ⚠ THE TEXT IS ON DISK WHEN THE FILE CLOSED, NOT WHEN Write ANSWERED. The stream is buffered: a full disk, a
// share that dropped - Write says yes to the buffer and the CLOSE is what reports it. A close taken on trust
// lets a script delete its source believing the export whole. (A Write that failed has said so already; the
// close answers for itself.)
void ibValueTextWriter::Close()
{
	if (m_file == nullptr)
		return;

	const std::unique_ptr<std::ofstream> file = std::move(m_file);
	if (!file->is_open())
		return;

	file->clear();
	file->close();
	if (file->fail())
		ibBackendCoreException::Error(_("TextWriter: the file '%s' could not be written to the end"), m_fileName);
}

bool ibValueTextWriter::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum) {
	case enOpen:
		if (lSizeArray < 1)
			return false;
		Open(paParams[0]->GetString(), ibTextEncodingAt(paParams, lSizeArray, 1),
			lSizeArray > 2 && paParams[2]->GetBoolean());
		return true;
	case enWrite:
		Write(lSizeArray > 0 ? paParams[0]->GetString() : ibString());
		return true;
	case enWriteLine:
		// One line ending, the same on every platform: a file written here reads the same there.
		Write((lSizeArray > 0 ? paParams[0]->GetString() : ibString()) + ibT("\n"));
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
