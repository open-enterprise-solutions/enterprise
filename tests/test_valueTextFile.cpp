// =============================================================================
// TextReader / TextWriter / BinaryData - the edges a script cannot reach by itself: the byte-order marks,
// the refusals, and what is ON DISK after a write (the corpus suite, tests/scripts/test_text_files_suite.txt,
// checks the language side: New, ReadLine, Undefined at the end).
// =============================================================================

#include <gtest/gtest.h>

#include <string>
#include <utility>

#include <wx/ffile.h>
#include <wx/filename.h>

#ifdef _WIN32
#include <wx/msw/wrapwin.h>
#endif

#include "backend/backend_exception.h"
#include "backend/system/value/valueTextFile.h"
#include "backend/system/value/valueBinaryData.h"

namespace {

struct TempFile {
	wxString path = wxFileName::CreateTempFileName(wxT("oes"));
	~TempFile() { if (!path.empty()) wxRemoveFile(path); }

	void Put(const std::string& bytes) const {
		wxFFile f(path, wxT("wb"));
		ASSERT_TRUE(f.IsOpened());
		f.Write(bytes.data(), bytes.size());
	}
	std::string Get() const {
		wxFFile f(path, wxT("rb"));
		std::string bytes(static_cast<size_t>(f.Length()), '\0');
		if (!bytes.empty()) f.Read(&bytes[0], bytes.size());
		return bytes;
	}
};

} // namespace

// A UTF-8 byte-order mark is a mark: handed over as text it becomes an invisible first character, and the
// first column of a CSV never equals the name it is compared with.
TEST(TextReader, AUtf8ByteOrderMarkIsNotText) {
	TempFile f;
	f.Put(std::string("\xEF\xBB\xBF") + "code;name\r\n1;x\r\n");

	ibValueTextReader reader;
	reader.Open(f.path, ibTextEncoding_UTF8);
	wxString line;
	ASSERT_TRUE(reader.ReadLine(line));
	EXPECT_EQ(line, wxString(wxT("code;name")));   // no mark in front, no CR behind
	ASSERT_TRUE(reader.ReadLine(line));
	EXPECT_EQ(line, wxString(wxT("1;x")));
	EXPECT_FALSE(reader.ReadLine(line)) << "a file that ends with a line break has no line after it";
}

// An empty line is a line; the end of the file is where ReadLine says false.
TEST(TextReader, AnEmptyLineDoesNotEndTheFile) {
	TempFile f;
	f.Put("a\n\nb");
	ibValueTextReader reader;
	reader.Open(f.path);
	wxString line;
	ASSERT_TRUE(reader.ReadLine(line)); EXPECT_EQ(line, wxString(wxT("a")));
	ASSERT_TRUE(reader.ReadLine(line)); EXPECT_TRUE(line.empty());
	ASSERT_TRUE(reader.ReadLine(line)); EXPECT_EQ(line, wxString(wxT("b")));
	EXPECT_FALSE(reader.ReadLine(line));
}

// ...and the SCRIPT sees that end as Undefined. An empty string there is an empty LINE, and a `while` written
// against it never stops - so this is asked of the method a script calls, by the name a script calls it by.
TEST(TextReader, TheScriptMethodAnswersUndefinedAtTheEnd) {
	TempFile f;
	f.Put("\n");   // one empty line, then nothing
	ibValueTextReader reader;
	reader.Open(f.path);
	const long readLine = reader.FindMethod(wxT("ReadLine"));
	ASSERT_GE(readLine, 0);

	ibValue line;
	ASSERT_TRUE(reader.CallAsFunc(readLine, line, nullptr, 0));
	EXPECT_EQ(line.GetType(), ibValueTypes::TYPE_STRING) << "an empty line is a line";
	EXPECT_TRUE(line.GetString().empty());

	ASSERT_TRUE(reader.CallAsFunc(readLine, line, nullptr, 0));
	EXPECT_EQ(line.GetType(), ibValueTypes::TYPE_EMPTY) << "the end of the file is Undefined";
}

// UTF-16 of either end is told by its mark.
TEST(TextReader, Utf16OfEitherEndIsToldByItsMark) {
	TempFile little, big;
	little.Put(std::string("\xFF\xFE", 2) + std::string("\x1F\x04\x40\x04", 4));   // "Пр", little end first
	big.Put(std::string("\xFE\xFF", 2) + std::string("\x04\x1F\x04\x40", 4));      // the same, big end first

	for (const TempFile* file : { &little, &big }) {
		ibValueTextReader reader;
		reader.Open(file->path, ibTextEncoding_UTF16);
		EXPECT_EQ(reader.ReadRest(), wxString::FromUTF8("\xD0\x9F\xD1\x80"));
	}
}

// Bytes that are not UTF-8 are a refusal - not a text with holes in it, and not "the file is empty".
TEST(TextReader, BytesThatAreNotUtf8AreRefused) {
	TempFile f;
	f.Put("caf\xE9 au lait");   // Latin-1, not UTF-8
	ibValueTextReader reader;
	EXPECT_THROW(reader.Open(f.path, ibTextEncoding_UTF8), ibBackendException);
}

TEST(TextReader, AMissingFileIsRefusedInWords) {
	ibValueTextReader reader;
	try {
		reader.Open(wxT("Z:\\no\\such\\folder\\file.txt"));
		FAIL() << "opening a file that is not there must raise";
	}
	catch (const ibBackendException& e) {
		EXPECT_NE(std::string(e.what()).find("file.txt"), std::string::npos) << e.what();
	}
}

TEST(TextReader, ReadingWithNothingOpenIsRefused) {
	ibValueTextReader reader;
	wxString line;
	EXPECT_THROW(reader.ReadLine(line), ibBackendException);
}

// What a writer leaves on disk: UTF-8 with NO mark (the mark is what breaks the first field for a reader that
// does not expect one), UTF-16 WITH it, and one line ending everywhere.
TEST(TextWriter, WhatIsOnDiskAfterAWrite) {
	TempFile f;
	{
		ibValueTextWriter writer;
		writer.Open(f.path, ibTextEncoding_UTF8);
		writer.Write(wxString::FromUTF8("\xD0\x9F;1\n"));
		writer.Close();
	}
	EXPECT_EQ(f.Get(), std::string("\xD0\x9F;1\n"));

	{
		ibValueTextWriter writer;
		writer.Open(f.path, ibTextEncoding_UTF16);
		writer.Write(wxT("A"));
		writer.Close();
	}
	EXPECT_EQ(f.Get(), std::string("\xFF\xFE\x41\x00", 4));
}

#ifdef _WIN32
// ANSI and OEM are the SYSTEM's code pages, asked of it - so the test asks the same system what the bytes
// mean, and holds on whatever machine it runs: a Cyrillic one, a Western one, a build agent's. The upper
// half of a single-byte page is where the pages differ, which is the half this writes and reads back.
TEST(TextWriter, AnsiAndOemAreTheSystemsOwnPages) {
	const std::pair<ibTextEncoding, unsigned> pages[] = {
		{ ibTextEncoding_ANSI, ::GetACP() }, { ibTextEncoding_OEM, ::GetOEMCP() } };

	for (const auto& [encoding, page] : pages) {
		CPINFO info{};
		if (!::GetCPInfo(page, &info) || info.MaxCharSize != 1)
			continue;   // a multi-byte page: a lone upper byte is half a character there

		std::string bytes = "id;";
		wxString text = wxT("id;");
		for (int b = 0xA1; b <= 0xAF; b++) {
			const char one = static_cast<char>(b);
			wchar_t wide = 0;
			char back = 0;
			BOOL usedDefault = FALSE;
			if (::MultiByteToWideChar(page, MB_ERR_INVALID_CHARS, &one, 1, &wide, 1) == 1
				&& ::WideCharToMultiByte(page, WC_NO_BEST_FIT_CHARS, &wide, 1, &back, 1, nullptr, &usedDefault) == 1
				&& !usedDefault && back == one) {
				bytes += one;
				text += wide;
			}
		}
		ASSERT_GT(bytes.size(), 3u) << "code page " << page << " has nothing in 0xA1..0xAF";

		TempFile f;
		ibValueTextWriter writer;
		writer.Open(f.path, encoding);
		writer.Write(text);
		writer.Close();
		EXPECT_EQ(f.Get(), bytes) << "code page " << page;

		ibValueTextReader reader;
		reader.Open(f.path, encoding);
		EXPECT_EQ(reader.ReadRest(), text) << "code page " << page;
	}
}

// A character the page has no bytes for is a refusal - not a question mark in a file somebody will import.
TEST(TextWriter, ACharacterThePageCannotSpellIsRefused) {
	CPINFO info{};
	if (!::GetCPInfo(::GetOEMCP(), &info) || info.MaxCharSize != 1)
		GTEST_SKIP() << "the OEM page of this machine is multi-byte";
	TempFile f;
	ibValueTextWriter writer;
	writer.Open(f.path, ibTextEncoding_OEM);
	EXPECT_THROW(writer.Write(wxString::FromUTF8("\xE4\xB8\xAD")), ibBackendException);   // a CJK ideograph
}
#endif

// Appending adds to the end, and a UTF-16 file gets its mark once.
TEST(TextWriter, AppendingKeepsWhatWasThere) {
	TempFile f;
	for (const wxChar* text : { wxT("A"), wxT("B") }) {
		ibValueTextWriter writer;
		writer.Open(f.path, ibTextEncoding_UTF16, /*append*/ true);
		writer.Write(text);
		writer.Close();
	}
	EXPECT_EQ(f.Get(), std::string("\xFF\xFE\x41\x00\x42\x00", 6));
}

// A writer nobody closed still leaves a whole file behind it.
TEST(TextWriter, AWriterNobodyClosedLeavesItsTextOnDisk) {
	TempFile f;
	{
		ibValueTextWriter writer;
		writer.Open(f.path);
		writer.Write(wxT("kept"));
	}
	EXPECT_EQ(f.Get(), std::string("kept"));
}

// Bytes go to a file and come back as they were - zeros and all.
TEST(BinaryDataFile, TheBytesWrittenAreTheBytesRead) {
	TempFile f;
	const std::string raw("\x00\x01\xFF\x00\x7F", 5);
	const ibValueBinaryData out(raw.data(), raw.size());
	out.WriteFile(f.path);
	EXPECT_EQ(f.Get(), raw);

	ibValueBinaryData in;
	in.ReadFile(f.path);
	EXPECT_EQ(in.GetLength(), raw.size());
	EXPECT_TRUE(in.CompareValueEQ(out));
}

TEST(BinaryDataFile, AMissingFileIsRefused) {
	ibValueBinaryData data;
	EXPECT_THROW(data.ReadFile(wxT("Z:\\no\\such\\folder\\file.bin")), ibBackendException);
}
