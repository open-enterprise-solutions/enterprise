////////////////////////////////////////////////////////////////////////////
//	Description : the engine's own catalogs (core/localization/dictionary.h) - a .mo read without any
//	wxLocale, the plural rule and the context the catalog carries, a region falling to its language, the
//	process's language for a thread that names none, and the language chosen per thread: two threads of
//	two languages at once. The catalogs are written by the test, the way gettext lays them out.
////////////////////////////////////////////////////////////////////////////

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "core/fileSystem.h"   // ibPathOf
#include "core/localization/dictionary.h"

namespace {

// One entry of a catalog: the msgid (a context is joined with '\x04', a plural with '\0') and the msgstr
// (plural forms joined with '\0').
struct Entry { std::string m_id; std::string m_text; };

// A .mo as gettext writes it: little-endian, the header entry first (msgid ""), msgids sorted, no hash table.
std::vector<unsigned char> MoFile(std::vector<Entry> entries)
{
	std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.m_id < b.m_id; });
	const uint32_t n = static_cast<uint32_t>(entries.size());
	const uint32_t origTable = 28, transTable = origTable + 8 * n, strings = transTable + 8 * n;
	std::vector<unsigned char> out;
	const auto put32 = [&out](uint32_t v) {
		for (int i = 0; i < 4; ++i)
			out.push_back(static_cast<unsigned char>((v >> (8 * i)) & 0xff));
	};
	put32(0x950412de); put32(0); put32(n); put32(origTable); put32(transTable); put32(0); put32(0);
	std::vector<unsigned char> pool;
	std::vector<std::pair<uint32_t, uint32_t>> orig, trans;
	for (const Entry& e : entries) {
		orig.emplace_back(static_cast<uint32_t>(e.m_id.size()), strings + static_cast<uint32_t>(pool.size()));
		pool.insert(pool.end(), e.m_id.begin(), e.m_id.end());
		pool.push_back(0);
	}
	for (const Entry& e : entries) {
		trans.emplace_back(static_cast<uint32_t>(e.m_text.size()), strings + static_cast<uint32_t>(pool.size()));
		pool.insert(pool.end(), e.m_text.begin(), e.m_text.end());
		pool.push_back(0);
	}
	for (const auto& o : orig) { put32(o.first); put32(o.second); }
	for (const auto& t : trans) { put32(t.first); put32(t.second); }
	out.insert(out.end(), pool.begin(), pool.end());
	return out;
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

	void Write(const char* language, std::vector<Entry> entries) {
		const std::filesystem::path folder = m_root / "lang" / language;
		std::error_code error;
		ASSERT_TRUE(std::filesystem::create_directories(folder, error));
		const std::vector<unsigned char> bytes = MoFile(std::move(entries));
		std::ofstream file(folder / ibPathOf(ibString(kDomain) + ibT(".mo")), std::ios::binary);
		ASSERT_TRUE(file.is_open());
		file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
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
