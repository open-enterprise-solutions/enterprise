////////////////////////////////////////////////////////////////////////////
//	Description : the engine's own catalogs (core/localization/dictionary.h) - a .po read without any
//	wxLocale and without being compiled, the plural rule and the context the catalog carries, a region falling
//	to its language, the process's language for a thread that names none, and the language chosen per thread:
//	two threads of two languages at once. The catalogs are written by the test, the way gettext writes them;
//	the shipped ones (lang/) are read whole - what msgfmt --check did while they were compiled.
////////////////////////////////////////////////////////////////////////////

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include "core/fileSystem.h"   // ibPathOf
#include "core/formatString.h"   // Format's L - the names said in the language it names
#include "core/localization/dictionary.h"
#include "core/variant.h"        // ibVariant - the date Format is handed

namespace {

// One entry of a catalog: the msgid (a context is joined with '\x04', a plural with '\0') and the msgstr
// (plural forms joined with '\0').
struct Entry { std::string m_id; std::string m_text; };

// A string as a .po writes it: quoted, with the C escapes gettext uses.
std::string Quoted(const std::string& text)
{
	std::string out = "\"";
	for (const char c : text) {
		switch (c) {
		case '"':  out += "\\\""; break;
		case '\\': out += "\\\\"; break;
		case '\n': out += "\\n"; break;
		case '\t': out += "\\t"; break;
		default:   out += c; break;
		}
	}
	return out + "\"";
}

// A .po as a translator's tool writes it: the header entry first (msgid ""), a context as msgctxt, a plural as
// msgid_plural and msgstr[n].
std::string PoFile(const std::vector<Entry>& entries)
{
	std::string out;
	for (const Entry& e : entries) {
		std::string id = e.m_id;
		const size_t joint = id.find('\x04');
		if (joint != std::string::npos) {
			out += "msgctxt " + Quoted(id.substr(0, joint)) + "\n";
			id = id.substr(joint + 1);
		}
		const size_t plural = id.find('\0');
		if (plural == std::string::npos) {
			out += "msgid " + Quoted(id) + "\nmsgstr " + Quoted(e.m_text) + "\n\n";
			continue;
		}
		out += "msgid " + Quoted(id.substr(0, plural)) + "\nmsgid_plural " + Quoted(id.substr(plural + 1)) + "\n";
		size_t from = 0;
		for (int form = 0; from != std::string::npos; ++form) {
			const size_t end = e.m_text.find('\0', from);
			const std::string text = e.m_text.substr(from, end == std::string::npos ? std::string::npos : end - from);
			out += "msgstr[" + std::to_string(form) + "] " + Quoted(text) + "\n";
			from = end == std::string::npos ? end : end + 1;
		}
		out += "\n";
	}
	return out;
}

// THE SHIPPED CATALOG AS ITS TEXT SAYS — every entry a translator finished (no `fuzzy`, a translation, one form),
// read the plain way, line by line: what the reader under test must find, each one, word for word.
struct ShippedEntry { std::string m_context; std::string m_id; std::string m_text; };

std::string Unquoted(const std::string& line)
{
	const size_t open = line.find('"');
	const size_t close = line.rfind('"');
	std::string out;
	for (size_t i = open + 1; open != std::string::npos && i < close; ++i) {
		if (line[i] != '\\' || i + 1 >= close) {
			out += line[i];
			continue;
		}
		const char c = line[++i];
		out += c == 'n' ? '\n' : c == 't' ? '\t' : c == 'r' ? '\r' : c;
	}
	return out;
}

std::vector<ShippedEntry> ShippedEntries(const std::filesystem::path& file)
{
	std::ifstream in(file, std::ios::binary);
	std::vector<ShippedEntry> entries;
	ShippedEntry entry;
	std::string* field = nullptr;
	bool fuzzy = false, plural = false, translated = false;
	const auto finish = [&]() {
		if (translated && !fuzzy && !plural && !entry.m_id.empty() && !entry.m_text.empty())
			entries.push_back(entry);
		entry = ShippedEntry();
		field = nullptr;
		fuzzy = plural = translated = false;
	};
	std::string line;
	while (std::getline(in, line)) {
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		if (line.rfind("msgctxt ", 0) == 0 || line.rfind("msgid ", 0) == 0 || line.rfind("#", 0) == 0 || line.empty()) {
			if (translated)
				finish();
		}
		if (line.rfind("#,", 0) == 0) {
			fuzzy = fuzzy || line.find("fuzzy") != std::string::npos;
		}
		else if (line.rfind("#", 0) == 0 || line.empty()) {
			field = nullptr;
		}
		else if (line.rfind("msgctxt ", 0) == 0) {
			field = &entry.m_context;
			*field = Unquoted(line);
		}
		else if (line.rfind("msgid_plural ", 0) == 0) {
			plural = true;
			field = nullptr;
		}
		else if (line.rfind("msgid ", 0) == 0) {
			field = &entry.m_id;
			*field = Unquoted(line);
		}
		else if (line.rfind("msgstr", 0) == 0) {
			translated = true;
			field = &entry.m_text;
			*field = Unquoted(line);
		}
		else if (line.rfind("\"", 0) == 0 && field != nullptr) {
			*field += Unquoted(line);
		}
	}
	finish();
	return entries;
}

const char* const kHeader =
	"Content-Type: text/plain; charset=UTF-8\n"
	"Plural-Forms: nplurals=3; plural=(n%10==1 && n%100!=11 ? 0 : n%10>=2 && n%10<=4 && (n%100<10 || n%100>=20) ? 1 : 2);\n";

// The test's own domain and languages - names no catalog of the tree uses, so that the sets the process keeps
// for them are the test's alone. In a process that made other languages first (RegionalFix, in one run of
// several tests) the journal says the domain was declared after them: expected, and not what is tested.
const ibChar* const kDomain = ibT("oes_test");

class TranslationFix : public ::testing::Test {
protected:
	std::filesystem::path m_root;
	ibString m_processLanguage;
	ibDictionary::Source m_source = nullptr;

	void SetUp() override {
		const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
		std::error_code error;
		m_root = std::filesystem::temp_directory_path(error) / ("oes_translations_" + std::to_string(now));
		ASSERT_TRUE(std::filesystem::create_directories(m_root, error));
		Write("qa", {
			{ "", kHeader },
			{ "Document", "\xD0\x94\xD0\xBE\xD0\xBA\xD1\x83\xD0\xBC\xD0\xB5\xD0\xBD\xD1\x82" },             // Документ
			{ "Delete", "\xD0\xA3\xD0\xB4\xD0\xB0\xD0\xBB\xD0\xB8\xD1\x82\xD1\x8C" },                         // Удалить
			{ std::string("access right\x04") + "Delete", "\xD0\x92\xD0\xB8\xD0\xBB\xD1\x83\xD1\x87\xD0\xB8\xD1\x82\xD0\xB8" },   // Вилучити
			{ std::string("%d document") + '\0' + "%d documents",
			  std::string("%d \xD0\xB4\xD0\xBE\xD0\xBA") + '\0' + "%d \xD0\xB4\xD0\xBE\xD0\xBA\xD0\xB0" + '\0' + "%d \xD0\xB4\xD0\xBE\xD0\xBA\xD0\xBE\xD0\xB2" },   // док / дока / доков
			{ std::string("date pattern\x04") + "d MMMM yyyy", "d MMMM yyyy '\xD0\xB3.'" },                            // …'г.'
			{ std::string("month\x04") + "October", "\xD0\xBE\xD0\xBA\xD1\x82\xD1\x8F\xD0\xB1\xD1\x80\xD1\x8C" },            // октябрь
			{ std::string("month, genitive\x04") + "October", "\xD0\xBE\xD0\xBA\xD1\x82\xD1\x8F\xD0\xB1\xD1\x80\xD1\x8F" },  // октября
		});
		Write("qb", {
			{ "", kHeader },
			{ "Document", "Dokument" },
		});
		Write("qq", {
			{ "", kHeader },
			{ "Document", "Dokumentas" },
		});
		ibDictionary::AddCatalogLookupPathPrefix(ibString::FromUTF8(reinterpret_cast<const char*>((m_root / "lang").u8string().c_str())));
		ibDictionary::AddDomain(kDomain);
		m_processLanguage = ibDictionary::GetProcessLanguage();
		m_source = ibDictionary::SetSource(nullptr);
	}
	void TearDown() override {
		ibDictionary::SetSource(m_source);
		ibDictionary::SetProcessLanguage(m_processLanguage);
		std::error_code error;
		if (!m_root.empty())
			std::filesystem::remove_all(m_root, error);
	}

	void Write(const char* language, const std::vector<Entry>& entries) {
		WriteText(language, PoFile(entries));
	}
	// …or the catalog's text as it is, for what PoFile does not write: comments, flags, a string over lines.
	void WriteText(const char* language, const std::string& text) {
		const std::filesystem::path folder = m_root / "lang" / language;
		std::error_code error;
		ASSERT_TRUE(std::filesystem::create_directories(folder, error));
		std::ofstream file(folder / ibPathOf(ibString(kDomain) + ibT(".po")), std::ios::binary);
		ASSERT_TRUE(file.is_open());
		file.write(text.data(), static_cast<std::streamsize>(text.size()));
		ASSERT_TRUE(file.good());
	}
};

thread_local const ibDictionary* t_set = nullptr;   // what the test's source answers for this thread
const ibDictionary* SetOfThisTestThread() { return t_set; }

} // namespace

TEST_F(TranslationFix, ACatalogIsReadWithoutAnyLocale)
{
	const ibDictionary* const qa = ibDictionary::ForLanguage(ibT("qa"));
	ASSERT_NE(qa, nullptr);
	EXPECT_EQ(ibT("qa"), qa->GetLanguage());
	EXPECT_TRUE(qa->IsLoaded(kDomain));
	const ibString* const document = qa->GetTranslatedString(ibT("Document"));
	ASSERT_NE(document, nullptr);
	EXPECT_EQ(ibString::FromUTF8("\xD0\x94\xD0\xBE\xD0\xBA\xD1\x83\xD0\xBC\xD0\xB5\xD0\xBD\xD1\x82"), *document);
	EXPECT_EQ(nullptr, qa->GetTranslatedString(ibT("Nobody wrote this")));
	EXPECT_EQ(nullptr, qa->GetTranslatedString(ibString()));
	EXPECT_EQ(qa, ibDictionary::ForLanguage(ibT("qa"))) << "one set per language, kept";
}

// Format's L names the language the names are said in: that language's catalog, whatever the thread speaks. A
// month after a day is the catalog's genitive, on its own its nominative; the long date is the catalog's pattern.
TEST_F(TranslationFix, FormatSaysTheNamesInTheLanguageLNames)
{
	ibDictionary::SetProcessLanguage(ibString());   // the thread speaks no language: the names as written
	const ibVariant date(ibDateTime(2026, 10, 13, 15, 30, 45));
	EXPECT_EQ(ibString::FromUTF8("13 \xD0\xBE\xD0\xBA\xD1\x82\xD1\x8F\xD0\xB1\xD1\x80\xD1\x8F 2026 \xD0\xB3."),   // 13 октября 2026 г.
		ibFormatString::Parse(ibT("L=qa; DLF=DD")).Apply(date));
	EXPECT_EQ(ibString::FromUTF8("\xD0\xBE\xD0\xBA\xD1\x82\xD1\x8F\xD0\xB1\xD1\x80\xD1\x8C"),                        // октябрь
		ibFormatString::Parse(ibT("L=qa; DF=MMMM")).Apply(date));
	EXPECT_EQ(ibString(ibT("13 October 2026")), ibFormatString::Parse(ibT("DLF=DD")).Apply(date));
	EXPECT_EQ(ibString(ibT("Tue")), ibFormatString::Parse(ibT("L=qa; DF=ddd")).Apply(date)) << "a word qa has not";
}

TEST_F(TranslationFix, ThePluralRuleAndTheContextAreTheCatalogs)
{
	ibDictionary::SetProcessLanguage(ibT("qa"));
	EXPECT_EQ(ibString::FromUTF8("%d \xD0\xB4\xD0\xBE\xD0\xBA"), ibGetTranslation(ibT("%d document"), ibT("%d documents"), 1));
	EXPECT_EQ(ibString::FromUTF8("%d \xD0\xB4\xD0\xBE\xD0\xBA\xD0\xB0"), ibGetTranslation(ibT("%d document"), ibT("%d documents"), 3));
	EXPECT_EQ(ibString::FromUTF8("%d \xD0\xB4\xD0\xBE\xD0\xBA\xD0\xBE\xD0\xB2"), ibGetTranslation(ibT("%d document"), ibT("%d documents"), 11));
	EXPECT_EQ(ibString::FromUTF8("%d \xD0\xB4\xD0\xBE\xD0\xBA"), ibGetTranslation(ibT("%d document"), ibT("%d documents"), 21));
	// The same word in two contexts is two entries.
	EXPECT_EQ(ibString::FromUTF8("\xD0\xA3\xD0\xB4\xD0\xB0\xD0\xBB\xD0\xB8\xD1\x82\xD1\x8C"), ibGetTranslation(ibT("Delete")));
	EXPECT_EQ(ibString::FromUTF8("\xD0\x92\xD0\xB8\xD0\xBB\xD1\x83\xD1\x87\xD0\xB8\xD1\x82\xD0\xB8"), ibGetTranslation(ibT("Delete"), ibT("access right")));
	// …and the macros are these doors.
	EXPECT_EQ(ibString::FromUTF8("\xD0\x94\xD0\xBE\xD0\xBA\xD1\x83\xD0\xBC\xD0\xB5\xD0\xBD\xD1\x82"), _("Document"));
	EXPECT_EQ(ibString::FromUTF8("\xD0\x92\xD0\xB8\xD0\xBB\xD1\x83\xD1\x87\xD0\xB8\xD1\x82\xD0\xB8"), ibGETTEXT_IN_CONTEXT("access right", "Delete"));
	EXPECT_EQ(ibString::FromUTF8("%d \xD0\xB4\xD0\xBE\xD0\xBA\xD0\xB0"), ibPLURAL("%d document", "%d documents", 2));
	// A plural nobody translated falls to the English pair.
	EXPECT_EQ(ibT("%d rows"), ibGetTranslation(ibT("%d row"), ibT("%d rows"), 2));
	EXPECT_EQ(ibT("%d row"), ibGetTranslation(ibT("%d row"), ibT("%d rows"), 1));
}

// A catalog the size of the engine's own (half a megabyte) is read whole: the reader's tables once pointed into the
// file's buffer, gone with the read - a small one survived in the freed memory, the shipped one came out empty
// (2026-10-27). Past the allocator's mmap threshold the freed buffer is unmapped, so a dangling read fails here.
TEST_F(TranslationFix, ALargeCatalogIsReadWhole)
{
	Write("qc", {
		{ "", kHeader },
		{ "Document", "Dokument" },
		{ "Padding", std::string(512 * 1024, 'x') },
	});
	const ibDictionary* const large = ibDictionary::ForLanguage(ibT("qc"));
	ASSERT_NE(large, nullptr);
	EXPECT_TRUE(large->IsLoaded(kDomain));
	const ibString* const document = large->GetTranslatedString(ibT("Document"));
	ASSERT_NE(document, nullptr);
	EXPECT_EQ(ibT("Dokument"), *document);
}

// A catalog's text as gettext writes it: a string carried over lines, the C escapes, comments of every kind, an
// obsolete entry (#~), lines that end CRLF. A `fuzzy` entry is no translation yet and one with an empty msgstr none
// at all: msgfmt left both out, and so does the reader.
TEST_F(TranslationFix, ACatalogsTextIsReadAsGettextWritesIt)
{
	WriteText("qd",
		"# the translator's comment\r\n"
		"msgid \"\"\r\n"
		"msgstr \"\"\r\n"
		"\"Content-Type: text/plain; charset=UTF-8\\n\"\r\n"
		"\"Plural-Forms: nplurals=2; plural=(n != 1);\\n\"\r\n"
		"\r\n"
		"#. the source's comment\r\n"
		"#: src/engine/somewhere.cpp:1\r\n"
		"msgid \"A line \"\r\n"
		"\"carried over\"\r\n"
		"msgstr \"Eine Zeile \"\r\n"
		"\"fortgesetzt\"\r\n"
		"\r\n"
		"msgid \"Say \\\"no\\\"\\tthen\\\\stop\\n\"\r\n"
		"msgstr \"Sag \\\"nein\\\"\\tdann\\\\halt\\n\"\r\n"
		"\r\n"
		"#, fuzzy\r\n"
		"msgid \"Guessed\"\r\n"
		"msgstr \"Geraten\"\r\n"
		"\r\n"
		"msgid \"Not yet\"\r\n"
		"msgstr \"\"\r\n"
		"\r\n"
		"#~ msgid \"Gone\"\r\n"
		"#~ msgstr \"Weg\"\r\n");
	const ibDictionary* const qd = ibDictionary::ForLanguage(ibT("qd"));
	ASSERT_NE(qd, nullptr);
	ASSERT_TRUE(qd->IsLoaded(kDomain));
	const ibString* const carried = qd->GetTranslatedString(ibT("A line carried over"));
	ASSERT_NE(carried, nullptr);
	EXPECT_EQ(ibT("Eine Zeile fortgesetzt"), *carried);
	const ibString* const escaped = qd->GetTranslatedString(ibT("Say \"no\"\tthen\\stop\n"));
	ASSERT_NE(escaped, nullptr);
	EXPECT_EQ(ibT("Sag \"nein\"\tdann\\halt\n"), *escaped);
	EXPECT_EQ(nullptr, qd->GetTranslatedString(ibT("Guessed"))) << "fuzzy is no translation yet";
	EXPECT_EQ(nullptr, qd->GetTranslatedString(ibT("Not yet")));
	EXPECT_EQ(nullptr, qd->GetTranslatedString(ibT("Gone"))) << "an obsolete entry is a comment";
}

// A format translated with other conversions than its own would hand Format arguments it does not take: such a form
// is not taken - the text is said as written, and the journal names it - as msgfmt --check refused to compile it.
// Only where the entry says it is c-format: a `%` in any other text is a character.
TEST_F(TranslationFix, AFormatTranslatedWithOtherConversionsIsNotTaken)
{
	WriteText("qe",
		"msgid \"\"\n"
		"msgstr \"Content-Type: text/plain; charset=UTF-8\\n\"\n"
		"\n"
		"#, c-format\n"
		"msgid \"%d rows of %s\"\n"
		"msgstr \"%s: %d Zeilen\"\n"
		"\n"
		"#, c-format\n"
		"msgid \"%d rows\"\n"
		"msgstr \"%d Zeilen, 100%%\"\n"
		"\n"
		"msgid \"50% off\"\n"
		"msgstr \"50% weniger %s\"\n");
	const ibDictionary* const qe = ibDictionary::ForLanguage(ibT("qe"));
	ASSERT_NE(qe, nullptr);
	ASSERT_TRUE(qe->IsLoaded(kDomain));
	EXPECT_EQ(nullptr, qe->GetTranslatedString(ibT("%d rows of %s"))) << "a string where the number was";
	const ibString* const rows = qe->GetTranslatedString(ibT("%d rows"));
	ASSERT_NE(rows, nullptr) << "%% is a character, not a conversion";
	EXPECT_EQ(ibT("%d Zeilen, 100%%"), *rows);
	EXPECT_NE(nullptr, qe->GetTranslatedString(ibT("50% off"))) << "no c-format, no check";
}

// A file that is not a catalog - a line no .po has - is not loaded: the text is said as written, and the journal names
// the file and the line. A wrong line must not leave half a catalog.
TEST_F(TranslationFix, ABrokenCatalogIsNotLoaded)
{
	WriteText("qf",
		"msgid \"Document\"\n"
		"msgstr Dokument\n");
	const ibDictionary* const qf = ibDictionary::ForLanguage(ibT("qf"));
	ASSERT_NE(qf, nullptr);
	EXPECT_FALSE(qf->IsLoaded(kDomain));
	EXPECT_EQ(nullptr, qf->GetTranslatedString(ibT("Document")));
}

TEST_F(TranslationFix, ARegionFallsToItsLanguage)
{
	const ibDictionary* const regional = ibDictionary::ForLanguage(ibT("qq_RR"));
	ASSERT_NE(regional, nullptr);
	EXPECT_TRUE(regional->IsLoaded(kDomain)) << "lang/qq holds it";
	const ibString* const document = regional->GetTranslatedString(ibT("Document"));
	ASSERT_NE(document, nullptr);
	EXPECT_EQ(ibT("Dokumentas"), *document);
}

TEST_F(TranslationFix, AMissingCatalogGivesTheTextBack)
{
	const ibDictionary* const none = ibDictionary::ForLanguage(ibT("qz"));
	ASSERT_NE(none, nullptr);
	EXPECT_FALSE(none->IsLoaded(kDomain));
	ibDictionary::SetProcessLanguage(ibT("qz"));
	EXPECT_EQ(ibT("Document"), _("Document"));
	ibDictionary::SetProcessLanguage(ibString());
	EXPECT_EQ(ibT("Document"), _("Document")) << "no language at all: the text";
	EXPECT_EQ(nullptr, ibDictionary::ForLanguage(ibString()));
}

TEST_F(TranslationFix, TheProcessLanguageIsWhatAThreadWithoutASourceSpeaks)
{
	ibDictionary::SetProcessLanguage(ibT("qb"));
	EXPECT_EQ(ibT("Dokument"), _("Document"));
	// A source that answers nothing leaves the process's language in force…
	t_set = nullptr;
	ibDictionary::SetSource(&SetOfThisTestThread);
	EXPECT_EQ(ibT("Dokument"), _("Document"));
	// …and one that answers overrides it; the process's word changing is seen at once.
	t_set = ibDictionary::ForLanguage(ibT("qa"));
	EXPECT_EQ(ibString::FromUTF8("\xD0\x94\xD0\xBE\xD0\xBA\xD1\x83\xD0\xBC\xD0\xB5\xD0\xBD\xD1\x82"), _("Document"));
	t_set = nullptr;
	ibDictionary::SetProcessLanguage(ibT("qa"));
	EXPECT_EQ(ibString::FromUTF8("\xD0\x94\xD0\xBE\xD0\xBA\xD1\x83\xD0\xBC\xD0\xB5\xD0\xBD\xD1\x82"), _("Document"));
}

TEST_F(TranslationFix, TwoThreadsSpeakTwoLanguagesAtOnce)
{
	ibDictionary::SetSource(&SetOfThisTestThread);
	const ibString qaWord = ibString::FromUTF8("\xD0\x94\xD0\xBE\xD0\xBA\xD1\x83\xD0\xBC\xD0\xB5\xD0\xBD\xD1\x82");
	int qaWrong = 0, qbWrong = 0;
	const auto speak = [](const ibChar* language, const ibString& expected, int& wrong) {
		t_set = ibDictionary::ForLanguage(language);
		for (int i = 0; i < 2000; ++i)
			if (_("Document") != expected)
				++wrong;
	};
	std::thread a(speak, ibT("qa"), qaWord, std::ref(qaWrong));
	std::thread b(speak, ibT("qb"), ibString(ibT("Dokument")), std::ref(qbWrong));
	a.join();
	b.join();
	EXPECT_EQ(0, qaWrong);
	EXPECT_EQ(0, qbWrong);
}

// THE SHIPPED CATALOGS ARE READ WHOLE — lang/<language>/open_es.po, every entry a translator finished found word for
// word. A line the reader does not take, a format translated with other conversions (refused, as above), an entry
// lost between two others: red here, in CI, as msgfmt --check made it while the catalogs were compiled (2026-11-23).
TEST(Translations, TheShippedCatalogsAreReadWhole)
{
	const std::filesystem::path lang = std::filesystem::path(__FILE__).parent_path().parent_path() / "lang";
	ibDictionary::AddCatalogLookupPathPrefix(ibString::FromUTF8(reinterpret_cast<const char*>(lang.u8string().c_str())));
	for (const char* language : { "ru", "uk" }) {
		const ibDictionary* const dictionary = ibDictionary::ForLanguage(ibString::FromUTF8(language));
		ASSERT_NE(dictionary, nullptr) << language;
		ASSERT_TRUE(dictionary->IsLoaded(kEngineDomain)) << language;
		const std::vector<ShippedEntry> entries = ShippedEntries(lang / language / "open_es.po");
		EXPECT_GT(entries.size(), 1000u) << language;
		for (const ShippedEntry& entry : entries) {
			const ibString* const text = dictionary->GetTranslatedString(ibString::FromUTF8(entry.m_id),
				ibString::FromUTF8(entry.m_context));
			ASSERT_NE(text, nullptr) << language << ": " << entry.m_id;
			EXPECT_EQ(ibString::FromUTF8(entry.m_text), *text) << language << ": " << entry.m_id;
		}
	}
}

// A language asked for by any of its spellings is one name - its language in lower case, its region in upper - and
// what is not a language's name is nothing. No language's English name, no machine's default: the name is the one
// the catalogs are laid out under.
TEST(Translations, ANameIsCanonicalByAnyOfItsSpellings)
{
	for (const ibChar* spelling : { ibT("uk_UA"), ibT("uk-UA"), ibT("UK_ua"), ibT("uk_UA.UTF-8") })
		EXPECT_EQ(ibString(ibT("uk_UA")), ibDictionary::CanonicalLanguage(spelling)) << ibString(spelling);
	EXPECT_EQ(ibString(ibT("uk")), ibDictionary::CanonicalLanguage(ibT("UK")));
	EXPECT_EQ(ibString(ibT("xx_YY")), ibDictionary::CanonicalLanguage(ibT("xx_yy"))) << "the shape is checked, not a table";
	for (const ibChar* nothing : { ibT(""), ibT("u"), ibT("Ukrainian"), ibT("uk_U"), ibT("uk_UAA"), ibT("u1") })
		EXPECT_TRUE(ibDictionary::CanonicalLanguage(nothing).IsEmpty()) << ibString(nothing);
}
