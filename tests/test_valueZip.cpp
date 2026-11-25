// =============================================================================
// ZipFileWriter / ZipFileReader / ZipFileEntry - what a script cannot check by itself: the names an archive gets for
// each store-path mode, a folder walked with and without its folders, what lands ON DISK, and the refusals - a name
// that points out of the folder it is extracted to above all. The corpus suite (tests/scripts/test_zip_suite.txt)
// checks the language side: New, Items, For Each, an archive from BinaryData.
//
// The reader is held against an archive built here byte by byte (one stored entry, its CRC computed here): an
// archive the writer made and the reader read back proves only that the two agree.
// =============================================================================

#include <gtest/gtest.h>
#include <wx/string.h>   // wxT - the engine's headers include no wx

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "backend/backend_exception.h"
#include "backend/system/value/valueArray.h"
#include "backend/system/value/valueBinaryData.h"
#include "backend/system/value/valueZip.h"

#include "core/fileSystem.h"   // ibPathOf, ibTextOf
#include "core/zip/zip.h"

namespace {

namespace fs = std::filesystem;

// A FOLDER OF ITS OWN under the system's temp, gone with the test.
struct TempFolder {
	fs::path path;
	TempFolder() {
		std::mt19937 engine{ std::random_device{}() };
		path = fs::temp_directory_path() / ("oes-zip-" + std::to_string(engine()));
		fs::create_directories(path);
	}
	~TempFolder() { std::error_code error; fs::remove_all(path, error); }

	void Put(const fs::path& name, const std::string& bytes) const {
		fs::create_directories((path / name).parent_path());
		std::ofstream file(path / name, std::ios::binary | std::ios::trunc);
		file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
	}
	std::string Get(const fs::path& name) const {
		std::ifstream file(path / name, std::ios::binary);
		std::ostringstream bytes;
		bytes << file.rdbuf();
		return bytes.str();
	}
	ibString Text(const fs::path& name = fs::path()) const { return ibTextOf(name.empty() ? path : path / name); }
};

// What a refusal says, or "" when the call went through.
template <class Call>
ibString Refusal(Call call)
{
	try {
		call();
	}
	catch (const ibBackendException& refusal) {
		const ibString why = refusal.GetErrorDescription();
		return why.empty() ? ibString(wxT("(a refusal with no sentence)")) : why;
	}
	return ibString();
}

std::string Utf8(const ibString& text)
{
	const std::string utf8 = text.utf8_str();
	return std::string(utf8.data(), utf8.length());
}

// A method called as a script calls it - by its name, with the script's arguments; a procedure through CallAsProc and
// a function through CallAsFunc, as the interpreter picks them (HasRetVal).
void Call(ibValue& value, const wxChar* name, std::vector<ibValue> args = {})
{
	const long method = value.FindMethod(name);
	ASSERT_GE(method, 0) << Utf8(name);
	std::vector<ibValue*> params;
	for (ibValue& arg : args)
		params.push_back(&arg);
	const long count = static_cast<long>(params.size());
	ibValue out;
	ASSERT_TRUE(value.HasRetVal(method) ? value.CallAsFunc(method, out, params.data(), count)
		: value.CallAsProc(method, params.data(), count)) << Utf8(name);
}

ibValue Prop(ibValue& value, const wxChar* name)
{
	const long prop = value.FindProp(name);
	EXPECT_GE(prop, 0) << Utf8(name);
	ibValue out;
	EXPECT_TRUE(value.GetPropVal(prop, out)) << Utf8(name);
	return out;
}

// An archive written: the writer made with its file, each path added with its modes, written.
void Pack(const ibString& archive, const std::vector<std::vector<ibValue>>& adds)
{
	ibValue name(archive);
	ibValue* params[] = { &name };
	ibValueZipFileWriter writer;
	ASSERT_TRUE(writer.Init(params, 1));
	for (const std::vector<ibValue>& add : adds)
		Call(writer, wxT("Add"), add);
	Call(writer, wxT("Write"));
}

// The names a reader lists, in its order.
std::vector<std::string> Names(ibValueZipFileReader& reader)
{
	std::vector<std::string> names;
	const ibValue items = Prop(reader, wxT("Items"));
	const ibValueArray* const array = dynamic_cast<ibValueArray*>(items.GetRef());
	if (array == nullptr) {
		ADD_FAILURE() << "Items is not an Array";
		return names;
	}
	for (const ibValue& item : array->Values()) {
		ibValue entry = item;
		names.push_back(Utf8(Prop(entry, wxT("FullName")).GetString()));
	}
	return names;
}

ibValue Open(ibValueZipFileReader& reader, const ibValue& source)
{
	ibValue given = source;
	ibValue* params[] = { &given };
	reader.Init(params, 1);
	return given;
}

ibValue StorePath(ibZIPStorePathMode mode) { return ibValue::CreateEnumObject<ibValueEnumZIPStorePathMode>(mode); }
ibValue SubDirs(ibZIPSubDirProcessingMode mode) { return ibValue::CreateEnumObject<ibValueEnumZIPSubDirProcessingMode>(mode); }

// ---- an archive by hand: the format's own bytes, little-endian ---------------------------------------------------

void U16(std::string& out, std::uint16_t v) { out += static_cast<char>(v & 0xFF); out += static_cast<char>(v >> 8); }
void U32(std::string& out, std::uint32_t v) { U16(out, static_cast<std::uint16_t>(v & 0xFFFF)); U16(out, static_cast<std::uint16_t>(v >> 16)); }

std::uint32_t Crc32(const std::string& bytes)
{
	std::uint32_t crc = 0xFFFFFFFFu;
	for (const unsigned char c : bytes) {
		crc ^= c;
		for (int bit = 0; bit < 8; ++bit)
			crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
	}
	return ~crc;
}

// One entry, STORED: a local header, the bytes, the central directory, its end.
std::string StoredArchive(const std::string& name, const std::string& bytes)
{
	const std::uint32_t crc = Crc32(bytes);
	std::string zip;
	U32(zip, 0x04034b50); U16(zip, 20); U16(zip, 0); U16(zip, 0); U16(zip, 0); U16(zip, 0x21);
	U32(zip, crc); U32(zip, static_cast<std::uint32_t>(bytes.size())); U32(zip, static_cast<std::uint32_t>(bytes.size()));
	U16(zip, static_cast<std::uint16_t>(name.size())); U16(zip, 0);
	zip += name;
	zip += bytes;

	const std::uint32_t directory = static_cast<std::uint32_t>(zip.size());
	U32(zip, 0x02014b50); U16(zip, 20); U16(zip, 20); U16(zip, 0); U16(zip, 0); U16(zip, 0); U16(zip, 0x21);
	U32(zip, crc); U32(zip, static_cast<std::uint32_t>(bytes.size())); U32(zip, static_cast<std::uint32_t>(bytes.size()));
	U16(zip, static_cast<std::uint16_t>(name.size())); U16(zip, 0); U16(zip, 0); U16(zip, 0); U16(zip, 0); U32(zip, 0);
	U32(zip, 0);
	zip += name;
	const std::uint32_t directorySize = static_cast<std::uint32_t>(zip.size()) - directory;

	U32(zip, 0x06054b50); U16(zip, 0); U16(zip, 0); U16(zip, 1); U16(zip, 1);
	U32(zip, directorySize); U32(zip, directory); U16(zip, 0);
	return zip;
}

// An archive whose entries are named as given - what a hostile maker writes; the core's writer puts any name.
void ArchiveNamed(const fs::path& archive, const std::vector<std::pair<std::string, std::string>>& entries)
{
	std::ofstream file(archive, std::ios::binary | std::ios::trunc);
	ibZipWriter zip(file);
	for (const auto& [name, bytes] : entries) {
		ASSERT_TRUE(zip.PutNextEntry(ibString::FromUTF8(name.c_str())));
		ASSERT_TRUE(zip.Write(bytes.data(), bytes.size()));
	}
	ASSERT_TRUE(zip.Close());
}

} // namespace

// The types are what a script makes and what it is handed: a writer and a reader are made with New, an entry is
// listed, never made.
TEST(ZipFile, TheWriterAndTheReaderAreMadeAnEntryIsHandedOut)
{
	EXPECT_TRUE(ibValue::IsRegisterCtor(wxT("ZipFileWriter"), ibCtorObjectType::ibCtorObjectType_object_value));
	EXPECT_TRUE(ibValue::IsRegisterCtor(wxT("ZipFileReader"), ibCtorObjectType::ibCtorObjectType_object_value));
	EXPECT_TRUE(ibValue::IsRegisterCtor(wxT("ZipFileEntry"), ibCtorObjectType::ibCtorObjectType_object_system));
	EXPECT_TRUE(ibValue::IsRegisterCtor(wxT("ZIPStorePathMode"), ibCtorObjectType::ibCtorObjectType_object_enum));
	EXPECT_TRUE(ibValue::IsRegisterCtor(wxT("ZIPSubDirProcessingMode"), ibCtorObjectType::ibCtorObjectType_object_enum));
	EXPECT_FALSE(ibValue::IsRegisterCtor(wxT("ZipFileEntry"), ibCtorObjectType::ibCtorObjectType_object_value));

	ibValue made;
	EXPECT_FALSE(Refusal([&] { made = ibValue::CreateObject(wxT("ZipFileWriter")); }).empty()) << "a writer needs its file";
}

// The reader against bytes laid out here: one stored entry, read back and written out as it was.
TEST(ZipFile, TheReaderReadsAnArchiveLaidOutByHand)
{
	TempFolder folder;
	folder.Put("hand.zip", StoredArchive("notes/hello.txt", "hi there"));

	ibValueZipFileReader reader;
	Open(reader, ibValue(folder.Text("hand.zip")));
	ASSERT_EQ(Names(reader), std::vector<std::string>({ "notes/hello.txt" }));

	Call(reader, wxT("ExtractAll"), { ibValue(folder.Text("out")) });
	EXPECT_EQ(folder.Get("out/notes/hello.txt"), "hi there");
}

// Written and read back: a folder added is its files (not its folders), named by the name alone, in the order of
// their names; each comes out with the bytes it went in with - a zero byte, a byte past 127 and Cyrillic in a name
// among them.
TEST(ZipFile, WhatIsWrittenIsWhatIsReadBack)
{
	TempFolder folder;
	const std::string binary = { '\0', '\1', static_cast<char>(0xFF), static_cast<char>(0x80), 'z', 'i', 'p' };
	const ibString soup(wxT("\u0431\u043E\u0440\u0449.txt"));
	folder.Put("in/b.bin", binary);
	folder.Put("in/a.txt", "alpha");
	folder.Put(ibPathOf(ibString(wxT("in/")) + soup), "soup");
	folder.Put("in/sub/c.txt", "nested");

	const ibString archive = folder.Text("out.zip");
	Pack(archive, { { ibValue(folder.Text("in")) } });

	ibValueZipFileReader reader;
	Open(reader, ibValue(archive));
	EXPECT_EQ(Names(reader), std::vector<std::string>({ "a.txt", "b.bin", Utf8(soup) }))
		<< "a folder's folder is not added unless said";

	Call(reader, wxT("ExtractAll"), { ibValue(folder.Text("back")) });
	EXPECT_EQ(folder.Get("back/a.txt"), "alpha");
	EXPECT_EQ(folder.Get("back/b.bin"), binary);
	EXPECT_EQ(folder.Get(ibPathOf(ibString(wxT("back/")) + soup)), "soup");
}

// The three store-path modes over a folder walked with its folders.
TEST(ZipFile, TheStorePathModeNamesTheEntries)
{
	TempFolder folder;
	folder.Put("in/a.txt", "a");
	folder.Put("in/sub/c.txt", "c");
	const ibValue in(folder.Text("in"));
	const ibValue recursive = SubDirs(ibZIPSubDirProcessingMode_ProcessRecursively);

	const ibString relative = folder.Text("relative.zip");
	Pack(relative, { { in, StorePath(ibZIPStorePathMode_StoreRelativePath), recursive } });
	ibValueZipFileReader relativeReader;
	Open(relativeReader, ibValue(relative));
	EXPECT_EQ(Names(relativeReader), std::vector<std::string>({ "a.txt", "sub/c.txt" }));

	const ibString bare = folder.Text("bare.zip");
	Pack(bare, { { in, StorePath(ibZIPStorePathMode_DontStorePath), recursive } });
	ibValueZipFileReader bareReader;
	Open(bareReader, ibValue(bare));
	EXPECT_EQ(Names(bareReader), std::vector<std::string>({ "a.txt", "c.txt" }));

	const ibString full = folder.Text("full.zip");
	Pack(full, { { in, StorePath(ibZIPStorePathMode_StoreFullPath), recursive } });
	ibValueZipFileReader fullReader;
	Open(fullReader, ibValue(full));
	for (const std::string& name : Names(fullReader)) {
		EXPECT_NE(name.front(), '/') << "a name inside is never absolute: " << name;
		EXPECT_EQ(name.find(':'), std::string::npos) << "nor carries a drive: " << name;
		EXPECT_EQ(name.find('\\'), std::string::npos) << "folders are parted by '/': " << name;
	}
	const std::string tail = Utf8(ibTextOf(fs::path(folder.path / "in" / "sub" / "c.txt").relative_path().generic_u16string()));
	EXPECT_EQ(Names(fullReader).back(), tail);
}

// A mask in the last part takes the files of that folder that match it; a mask that matches nothing is an answer,
// a path that names nothing is a refusal.
TEST(ZipFile, AMaskTakesWhatMatchesAMissingPathIsRefused)
{
	TempFolder folder;
	folder.Put("in/a.txt", "a");
	folder.Put("in/b.pdf", "b");
	const ibString archive = folder.Text("mask.zip");
	Pack(archive, { { ibValue(folder.Text("in/*.txt")) }, { ibValue(folder.Text("in/*.none")) } });

	ibValueZipFileReader reader;
	Open(reader, ibValue(archive));
	EXPECT_EQ(Names(reader), std::vector<std::string>({ "a.txt" }));

	ibValue name(folder.Text("missing.zip"));
	ibValue* params[] = { &name };
	ibValueZipFileWriter writer;
	ASSERT_TRUE(writer.Init(params, 1));
	const long add = writer.FindMethod(wxT("Add"));
	ibValue nothing(folder.Text("no-such-file.txt"));
	ibValue* args[] = { &nothing };
	EXPECT_FALSE(Refusal([&] { writer.CallAsProc(add, args, 1); }).empty());
}

// ⚠ AN ENTRY THAT NAMES A PLACE OUTSIDE THE FOLDER IS REFUSED, and nothing lands there. The archive is made by the
// core's writer with the names a hostile archive carries.
TEST(ZipFile, AnEntryNamedOutsideTheFolderIsRefused)
{
	for (const char* name : { "../evil.txt", "a/../../evil.txt", "/evil.txt" }) {
		TempFolder folder;
		ArchiveNamed(folder.path / "evil.zip", { { "fine.txt", "ok" }, { name, "owned" } });

		ibValueZipFileReader reader;
		Open(reader, ibValue(folder.Text("evil.zip")));
		const long extractAll = reader.FindMethod(wxT("ExtractAll"));
		ibValue target(folder.Text("inside/deeper"));
		ibValue* args[] = { &target };
		EXPECT_FALSE(Refusal([&] { reader.CallAsProc(extractAll, args, 1); }).empty()) << name;
		EXPECT_FALSE(fs::exists(folder.path / "evil.txt")) << name;
		EXPECT_FALSE(fs::exists(folder.path / "inside" / "evil.txt")) << name;
		EXPECT_FALSE(fs::exists(fs::path("/evil.txt"))) << name;

		// The archive's other entries are not barred by it: one taken by its name comes out.
		ibValueZipFileReader again;
		Open(again, ibValue(folder.Text("evil.zip")));
		Call(again, wxT("Extract"), { ibValue(ibString(wxT("fine.txt"))), ibValue(folder.Text("one")) });
		EXPECT_EQ(folder.Get("one/fine.txt"), "ok") << name;
	}
}

// An archive from its bytes reads as the file does; Extract takes an entry as listed or by its name.
TEST(ZipFile, AnArchiveIsReadFromItsBytes)
{
	TempFolder folder;
	folder.Put("in/a.txt", "alpha");
	folder.Put("in/b.txt", "beta");
	const ibString archive = folder.Text("bytes.zip");
	Pack(archive, { { ibValue(folder.Text("in")) } });

	ibMemoryBuffer bytes;
	ibValueBinaryData::ReadWholeFile(archive, ibT("test"), bytes);
	ibValueZipFileReader reader;
	Open(reader, ibValue(ibValuePtr<ibValueBinaryData>(new ibValueBinaryData(bytes))));
	ASSERT_EQ(Names(reader), std::vector<std::string>({ "a.txt", "b.txt" }));

	const ibValue items = Prop(reader, wxT("Items"));
	const ibValueArray* const array = dynamic_cast<ibValueArray*>(items.GetRef());
	ASSERT_NE(array, nullptr);
	Call(reader, wxT("Extract"), { array->Values().back(), ibValue(folder.Text("out")) });
	EXPECT_EQ(folder.Get("out/b.txt"), "beta");
	EXPECT_FALSE(fs::exists(folder.path / "out" / "a.txt")) << "Extract takes the one entry";

	const long extract = reader.FindMethod(wxT("Extract"));
	ibValue missing(ibString(wxT("c.txt")));
	ibValue target(folder.Text("out"));
	ibValue* args[] = { &missing, &target };
	EXPECT_FALSE(Refusal([&] { reader.CallAsProc(extract, args, 2); }).empty()) << "an entry not there";

	Call(reader, wxT("Close"));
	EXPECT_FALSE(Refusal([&] { reader.CallAsProc(extract, args, 2); }).empty()) << "a closed archive";
	EXPECT_EQ(Names(reader).size(), 2u) << "the listing stays readable after Close";
}

// An archive with nothing in it opens with no items; a file that is no archive is refused when opened.
TEST(ZipFile, AnEmptyArchiveOpensAFileThatIsNoneIsRefused)
{
	TempFolder folder;
	const ibString empty = folder.Text("empty.zip");
	Pack(empty, {});
	ibValueZipFileReader reader;
	EXPECT_TRUE(Refusal([&] { Open(reader, ibValue(empty)); }).empty());
	EXPECT_TRUE(Names(reader).empty());

	folder.Put("text.zip", "this is not an archive at all");
	ibValueZipFileReader notOne;
	EXPECT_FALSE(Refusal([&] { Open(notOne, ibValue(folder.Text("text.zip"))); }).empty());

	ibValueZipFileReader missing;
	EXPECT_FALSE(Refusal([&] { Open(missing, ibValue(folder.Text("no-such.zip"))); }).empty());
}

// An entry's name taken apart: the name, its base and extension, the path before it; a folder's entry is named by
// its folder; a leading dot is a name, not an extension.
TEST(ZipFile, AnEntryTakesItsNameApart)
{
	struct Case { const wxChar* full; const char* name; const char* base; const char* extension; const char* path; };
	for (const Case& one : { Case{ wxT("docs/report.final.pdf"), "report.final.pdf", "report.final", "pdf", "docs/" },
			Case{ wxT("readme"), "readme", "readme", "", "" },
			Case{ wxT("home/.profile"), ".profile", ".profile", "", "home/" },
			Case{ wxT("docs/sub/"), "sub", "sub", "", "docs/" } }) {
		ibValueZipFileEntry entry(one.full, 42);
		EXPECT_EQ(Utf8(Prop(entry, wxT("Name")).GetString()), one.name) << Utf8(one.full);
		EXPECT_EQ(Utf8(Prop(entry, wxT("BaseName")).GetString()), one.base) << Utf8(one.full);
		EXPECT_EQ(Utf8(Prop(entry, wxT("Extension")).GetString()), one.extension) << Utf8(one.full);
		EXPECT_EQ(Utf8(Prop(entry, wxT("Path")).GetString()), one.path) << Utf8(one.full);
		EXPECT_EQ(Utf8(Prop(entry, wxT("FullName")).GetString()), Utf8(one.full));
		EXPECT_EQ(Prop(entry, wxT("UncompressedSize")).GetNumber(), ibNumber(42));
	}
}
