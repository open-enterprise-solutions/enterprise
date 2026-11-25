#include "valueZip.h"

#include "backend/backend_exception.h"              // ibBackendCoreException - a refusal said to the script
#include "backend/system/value/valueArray.h"        // ibValueArray - the entries a reader lists
#include "backend/system/value/valueBinaryData.h"   // ibValueBinaryData - an archive's bytes; a file read whole

#include "core/fileSystem.h"   // ibPathOf, ibDir
#include "core/zip/zip.h"      // ibZipReader, ibZipWriter

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <istream>
#include <streambuf>
#include <system_error>

namespace {

// Whether an argument was given - asked of its type, as HTTPConnection asks: a zero is an empty number, not a gap.
bool Given(ibValue** paParams, const long lSizeArray, const long at)
{
	return lSizeArray > at && paParams[at]->GetType() != ibValueTypes::TYPE_EMPTY;
}

// BYTES READ WHERE THEY LIE — an archive opened from BinaryData is passed over twice and more, and not copied for it.
class ibBytesBuffer : public std::streambuf {
public:
	ibBytesBuffer(const void* data, std::size_t size)
	{
		char* const at = static_cast<char*>(const_cast<void*>(data));
		setg(at, at, at + size);
	}
};

// AN ARCHIVE WITH NOTHING IN IT is its end record alone - `PK\5\6` where an entry's header would be. The reader finds
// no entry in it, and neither in a file that is no archive at all: the first four bytes tell the two apart.
bool HoldsNoEntries(std::istream& input)
{
	char head[4] = {};
	input.read(head, sizeof(head));
	return input.gcount() == sizeof(head) && head[0] == 'P' && head[1] == 'K' && head[2] == 5 && head[3] == 6;
}

// ⚠ AN ENTRY IS WRITTEN INSIDE THE FOLDER IT IS EXTRACTED TO, OR NOT AT ALL. An archive names its files, and a name
// such as `../../startup/run.bat` or `/etc/passwd` would put one wherever its maker chose (the "zip slip"): such a
// name is refused with the entry named, never followed and never quietly shortened.
std::filesystem::path TargetOf(const ibString& folder, const ibString& name)
{
	const std::filesystem::path inside = ibPathOf(name).lexically_normal();
	if (inside.empty() || inside.has_root_name() || inside.has_root_directory()
		|| *inside.begin() == std::filesystem::path("..")) {
		ibBackendCoreException::Error(_("ZipFileReader: the entry '%s' names a place outside the folder it is extracted to"),
			name);
	}
	return ibPathOf(folder) / inside;
}

// An entry's bytes put where it goes - a folder's entry (its name ends with `/`) is the folder made.
void WriteEntry(const ibString& folder, const ibString& name, const ibMemoryBuffer& bytes)
{
	const std::filesystem::path target = TargetOf(folder, name);
	std::error_code error;
	if (name.EndsWith(ibT("/"))) {
		std::filesystem::create_directories(target, error);
		if (error)
			ibBackendCoreException::Error(_("ZipFileReader: cannot make the folder '%s'"), ibString(target.wstring()));
		return;
	}
	std::filesystem::create_directories(target.parent_path(), error);
	if (error)
		ibBackendCoreException::Error(_("ZipFileReader: cannot make the folder '%s'"), ibString(target.parent_path().wstring()));

	std::ofstream file(target, std::ios::binary | std::ios::trunc);
	if (!file)
		ibBackendCoreException::Error(_("ZipFileReader: cannot open the file '%s' for writing"), ibString(target.wstring()));
	const std::size_t length = bytes.GetDataLen();
	if (length > 0)
		file.write(static_cast<const char*>(bytes.GetData()), static_cast<std::streamsize>(length));
	file.close();
	if (file.fail())
		ibBackendCoreException::Error(_("ZipFileReader: writing to the file '%s' failed"), ibString(target.wstring()));
}

} // namespace

//*********************************************************************************************************************
//*                                                  ZipFileWriter                                                    *
//*********************************************************************************************************************

void ibValueZipFileWriter_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	helper.AppendConstructor(1, ibT("ZipFileWriter(fileName : string)"));

	helper.AppendProc(ibT("Add"), 3,
		ibT("Add(path : string, storePathMode : ZIPStorePathMode = DontStorePath, subDirProcessingMode : ZIPSubDirProcessingMode = DontProcess)"));
	helper.AppendProc(ibT("Write"), ibT("Write()"));
}

ibValueZipFileWriter::ibValueZipFileWriter() : ibValueStaticMembers(ibValueTypes::TYPE_VALUE)
{
}

ibValueZipFileWriter::~ibValueZipFileWriter()
{
}

bool ibValueZipFileWriter::Init(ibValue** paParams, const long lSizeArray)
{
	if (lSizeArray < 1)
		return false;
	m_fileName = paParams[0]->GetString();
	if (m_fileName.IsEmpty())
		ibBackendCoreException::Error(_("ZipFileWriter: the archive needs a file name"));
	return true;
}

bool ibValueZipFileWriter::CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum) {
	case enAdd:
		if (lSizeArray < 1)
			return false;
		Add(paParams[0]->GetString(),
			Given(paParams, lSizeArray, 1) ? paParams[1]->ConvertToEnumValue<ibZIPStorePathMode>() : ibZIPStorePathMode_DontStorePath,
			Given(paParams, lSizeArray, 2) ? paParams[2]->ConvertToEnumValue<ibZIPSubDirProcessingMode>() : ibZIPSubDirProcessingMode_DontProcess);
		return true;
	case enWrite:
		Write();
		return true;
	}
	return false;
}

// WHAT A PATH NAMES — a file; a folder, whose files are taken (and its folders' files, ProcessRecursively); or a mask in
// its last part, `C:\out\*.pdf`, the files of that folder that match it. Each is put on the list under the name the
// store-path mode gives it. A path that names nothing is refused: a file not there is a file missing from the archive,
// and an archive short of a file says nothing about it. A mask matching nothing is no refusal - that is an answer.
void ibValueZipFileWriter::Add(const ibString& path, ibZIPStorePathMode storePath, ibZIPSubDirProcessingMode subDirs)
{
	const std::filesystem::path given = ibPathOf(path);
	const ibString last(given.filename().wstring());
	const int flags = ibDIR_FILES | (subDirs == ibZIPSubDirProcessingMode_ProcessRecursively ? ibDIR_DIRS : 0);

	std::error_code error;
	std::filesystem::path folder;   // what StoreRelativePath counts from
	std::vector<ibString> files;
	if (last.Contains(ibT("*")) || last.Contains(ibT("?"))) {
		folder = given.parent_path();
		ibDir::GetAllFiles(ibString(folder.wstring()), &files, last, flags);
	}
	else if (std::filesystem::is_directory(given, error)) {
		folder = given;
		ibDir::GetAllFiles(path, &files, ibEmptyString, flags);
	}
	else if (std::filesystem::is_regular_file(given, error)) {
		folder = given.parent_path();
		files.push_back(path);
	}
	else
		ibBackendCoreException::Error(_("ZipFileWriter: there is no file or folder '%s'"), path);

	// In the order of their names, so the same folder makes the same archive.
	std::sort(files.begin(), files.end());

	for (const ibString& file : files) {
		const std::filesystem::path one = ibPathOf(file);
		std::filesystem::path inside;
		if (storePath == ibZIPStorePathMode_StoreRelativePath)
			inside = one.lexically_relative(folder);
		else if (storePath == ibZIPStorePathMode_StoreFullPath)
			inside = one.relative_path();   // the root and the drive aside: a name inside is never absolute
		if (inside.empty() || *inside.begin() == std::filesystem::path(".."))
			inside = one.filename();
		m_entries.push_back({ file, ibString(inside.generic_wstring()) });
	}
}

// THE ARCHIVE, WHOLE OR NOT AT ALL: each file read as Write finds it and packed in the order added. The archive itself is
// passed over should a folder added hold it (an archive written into the folder it packs, on its second run).
void ibValueZipFileWriter::Write() const
{
	std::error_code error;
	const std::filesystem::path self = std::filesystem::absolute(ibPathOf(m_fileName), error).lexically_normal();

	std::ofstream file(ibPathOf(m_fileName), std::ios::binary | std::ios::trunc);
	if (!file)
		ibBackendCoreException::Error(_("ZipFileWriter: cannot open the file '%s' for writing"), m_fileName);

	ibZipWriter zip(file);
	for (const Entry& entry : m_entries) {
		if (std::filesystem::absolute(ibPathOf(entry.m_path), error).lexically_normal() == self)
			continue;
		ibMemoryBuffer bytes;
		ibValueBinaryData::ReadWholeFile(entry.m_path, ibT("ZipFileWriter"), bytes);
		if (!zip.PutNextEntry(entry.m_name) || !zip.Write(bytes.GetData(), bytes.GetDataLen()))
			ibBackendCoreException::Error(_("ZipFileWriter: writing to the file '%s' failed"), m_fileName);
	}
	if (!zip.Close())
		ibBackendCoreException::Error(_("ZipFileWriter: writing to the file '%s' failed"), m_fileName);

	// ⚠ CLOSED EXPLICITLY and its answer read, as a sheet's file is: a stream merely destructed can leave a file that
	// exists, has a size, and is missing its last block.
	file.close();
	if (file.fail())
		ibBackendCoreException::Error(_("ZipFileWriter: writing to the file '%s' failed"), m_fileName);
}

//*********************************************************************************************************************
//*                                                  ZipFileReader                                                    *
//*********************************************************************************************************************

void ibValueZipFileReader_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	helper.AppendConstructor(1, ibT("ZipFileReader(source : string | BinaryData)"));

	helper.AppendProp(ibT("Items"), true, false, ibNOT_FOUND);

	helper.AppendProc(ibT("Extract"), 2, ibT("Extract(item : ZipFileEntry | string, folder : string)"));
	helper.AppendProc(ibT("ExtractAll"), 1, ibT("ExtractAll(folder : string)"));
	helper.AppendProc(ibT("Close"), ibT("Close()"));
}

ibValueZipFileReader::ibValueZipFileReader() : ibValueStaticMembers(ibValueTypes::TYPE_VALUE)
{
}

ibValueZipFileReader::~ibValueZipFileReader()
{
}

template <class Take>
void ibValueZipFileReader::Walk(Take take) const
{
	if (m_closed)
		ibBackendCoreException::Error(_("ZipFileReader: the archive is closed"));

	ibBytesBuffer buffer(m_data.GetData(), m_data.GetDataLen());
	std::istream bytes(&buffer);
	std::ifstream file;
	if (!m_fileName.IsEmpty()) {
		file.open(ibPathOf(m_fileName), std::ios::binary);
		if (!file)
			ibBackendCoreException::Error(_("ZipFileReader: cannot open the file '%s' for reading"), m_fileName);
	}

	ibZipReader zip(m_fileName.IsEmpty() ? bytes : static_cast<std::istream&>(file));
	ibString name;
	while (zip.GetNextEntry(name)) {
		ibMemoryBuffer entry;
		if (!zip.ReadEntry(entry))
			ibBackendCoreException::Error(_("ZipFileReader: the entry '%s' cannot be read - the archive is damaged, encrypted, or packed by a method other than stored and deflated"),
				name);
		if (!take(name, entry))
			return;
	}
}

// OPENED FROM A FILE OR FROM BYTES, and listed at once: an archive that cannot be read is refused here, where the
// script names it, not at the first Extract.
bool ibValueZipFileReader::Init(ibValue** paParams, const long lSizeArray)
{
	if (lSizeArray < 1)
		return false;
	if (const ibValueBinaryData* const data = dynamic_cast<ibValueBinaryData*>(paParams[0]->GetRef()))
		m_data = data->GetBuffer();
	else {
		m_fileName = paParams[0]->GetString();
		if (m_fileName.IsEmpty())
			ibBackendCoreException::Error(_("ZipFileReader: the archive needs a file name or its bytes"));
	}

	const ibValuePtr<ibValueArray> items(new ibValueArray());   // born held
	Walk([&items](const ibString& name, const ibMemoryBuffer& bytes) {
		items->Add(ibValuePtr<ibValueZipFileEntry>(new ibValueZipFileEntry(name, bytes.GetDataLen())));
		return true;
	});

	if (items->Count() == 0) {
		bool empty = false;
		if (m_fileName.IsEmpty()) {
			ibBytesBuffer buffer(m_data.GetData(), m_data.GetDataLen());
			std::istream input(&buffer);
			empty = HoldsNoEntries(input);
		}
		else {
			std::ifstream input(ibPathOf(m_fileName), std::ios::binary);
			empty = HoldsNoEntries(input);
		}
		if (!empty)
			ibBackendCoreException::Error(_("ZipFileReader: '%s' is not a ZIP archive"),
				m_fileName.IsEmpty() ? ibString(ibT("BinaryData")) : m_fileName);
	}

	m_items = items;
	return true;
}

bool ibValueZipFileReader::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	switch (lPropNum) {
	case enItems:
		pvarPropVal = m_items;
		return true;
	}
	return false;
}

bool ibValueZipFileReader::CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum) {
	case enExtract: {
		if (lSizeArray < 2)
			return false;
		const ibValueZipFileEntry* const entry = dynamic_cast<ibValueZipFileEntry*>(paParams[0]->GetRef());
		Extract(entry != nullptr ? entry->GetFullName() : paParams[0]->GetString(), paParams[1]->GetString());
		return true;
	}
	case enExtractAll:
		if (lSizeArray < 1)
			return false;
		ExtractAll(paParams[0]->GetString());
		return true;
	case enClose:
		m_closed = true;
		m_data = ibMemoryBuffer();   // the bytes go with it; the listing stays readable
		return true;
	}
	return false;
}

void ibValueZipFileReader::Extract(const ibString& name, const ibString& folder) const
{
	bool found = false;
	Walk([&](const ibString& entryName, const ibMemoryBuffer& bytes) {
		if (entryName != name)
			return true;
		WriteEntry(folder, entryName, bytes);
		found = true;
		return false;
	});
	if (!found)
		ibBackendCoreException::Error(_("ZipFileReader: there is no entry '%s' in the archive"), name);
}

void ibValueZipFileReader::ExtractAll(const ibString& folder) const
{
	Walk([&folder](const ibString& name, const ibMemoryBuffer& bytes) {
		WriteEntry(folder, name, bytes);
		return true;
	});
}

//*********************************************************************************************************************
//*                                                  ZipFileEntry                                                     *
//*********************************************************************************************************************

void ibValueZipFileEntry_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	helper.AppendProp(ibT("Name"), true, false, ibNOT_FOUND);
	helper.AppendProp(ibT("BaseName"), true, false, ibNOT_FOUND);
	helper.AppendProp(ibT("Extension"), true, false, ibNOT_FOUND);
	helper.AppendProp(ibT("FullName"), true, false, ibNOT_FOUND);
	helper.AppendProp(ibT("Path"), true, false, ibNOT_FOUND);
	helper.AppendProp(ibT("UncompressedSize"), true, false, ibNOT_FOUND);
}

ibValueZipFileEntry::ibValueZipFileEntry() : ibValueStaticMembers(ibValueTypes::TYPE_VALUE)
{
}

ibValueZipFileEntry::ibValueZipFileEntry(const ibString& fullName, std::uint64_t size)
	: ibValueStaticMembers(ibValueTypes::TYPE_VALUE), m_fullName(fullName), m_size(size)
{
}

ibValueZipFileEntry::~ibValueZipFileEntry()
{
}

// THE NAME TAKEN APART at its last `/` - a folder's entry (`docs/`) is named by its folder: Name `docs`, Path empty.
bool ibValueZipFileEntry::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	ibString trimmed = m_fullName;
	if (trimmed.EndsWith(ibT("/")))
		trimmed.RemoveLast();
	const int slash = trimmed.Find(ibT('/'), true);
	const ibString name = slash == ibNOT_FOUND ? trimmed : trimmed.Mid(slash + 1);
	const int dot = name.Find(ibT('.'), true);
	const bool extended = dot != ibNOT_FOUND && dot > 0;   // `.profile` is a name, not an extension

	switch (lPropNum) {
	case enName:
		pvarPropVal = name;
		return true;
	case enBaseName:
		pvarPropVal = extended ? name.Left(dot) : name;
		return true;
	case enExtension:
		pvarPropVal = extended ? name.Mid(dot + 1) : ibString();
		return true;
	case enFullName:
		pvarPropVal = m_fullName;
		return true;
	case enPath:
		pvarPropVal = slash == ibNOT_FOUND ? ibString() : trimmed.Left(slash + 1);
		return true;
	case enUncompressedSize:
		pvarPropVal = ibNumber(m_size);
		return true;
	}
	return false;
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_TYPE_REGISTER(ibValueZipFileWriter, "ZipFileWriter", value_to_clsid("VL_ZPWR"));
VALUE_TYPE_REGISTER(ibValueZipFileReader, "ZipFileReader", value_to_clsid("VL_ZPRD"));
SYSTEM_TYPE_REGISTER(ibValueZipFileEntry, "ZipFileEntry", system_to_clsid("VL_ZPEN"));

ENUM_TYPE_REGISTER(ibValueEnumZIPStorePathMode, "ZIPStorePathMode", enum_to_clsid("EN_ZSPM"));
ENUM_TYPE_REGISTER(ibValueEnumZIPSubDirProcessingMode, "ZIPSubDirProcessingMode", enum_to_clsid("EN_ZSDM"));
