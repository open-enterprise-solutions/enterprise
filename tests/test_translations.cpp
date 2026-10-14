////////////////////////////////////////////////////////////////////////////
//	Description : the engine's own catalogs (core/localization/translations.h) - a .mo read without any
//	wxLocale, the plural rule and the context the catalog carries, a region falling to its language, the
//	process's language for a thread that names none, and the language chosen per thread: two threads of
//	two languages at once. The catalogs are written by the test, the way gettext lays them out.
////////////////////////////////////////////////////////////////////////////

#include <gtest/gtest.h>

#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/file.h>
#include <wx/init.h>
#include <wx/stdpaths.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "core/localization/translations.h"

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
const wxChar* const kDomain = wxT("oes_test");

class TranslationFix : public ::testing::Test {
protected:
	wxInitializer m_wxInit;
	wxString m_root;
	wxString m_processLanguage;
	ibTranslations::Source m_source = nullptr;

	void SetUp() override {
		if (!m_wxInit.IsOk())
			GTEST_SKIP() << "wxBase init failed";
		const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
		m_root = wxFileName(wxStandardPaths::Get().GetTempDir(), wxString::Format(wxT("oes_translations_%lld"), (long long)now)).GetFullPath();
		ASSERT_TRUE(wxFileName::Mkdir(m_root));
		Write(wxT("qa"), {
			{ "", kHeader },
			{ "Document", "\xD0\x94\xD0\xBE\xD0\xBA\xD1\x83\xD0\xBC\xD0\xB5\xD0\xBD\xD1\x82" },             // Документ
			{ "Delete", "\xD0\xA3\xD0\xB4\xD0\xB0\xD0\xBB\xD0\xB8\xD1\x82\xD1\x8C" },                         // Удалить
			{ std::string("access right\x04") + "Delete", "\xD0\x92\xD0\xB8\xD0\xBB\xD1\x83\xD1\x87\xD0\xB8\xD1\x82\xD0\xB8" },   // Вилучити
			{ std::string("%d document") + '\0' + "%d documents",
			  std::string("%d \xD0\xB4\xD0\xBE\xD0\xBA") + '\0' + "%d \xD0\xB4\xD0\xBE\xD0\xBA\xD0\xB0" + '\0' + "%d \xD0\xB4\xD0\xBE\xD0\xBA\xD0\xBE\xD0\xB2" },   // док / дока / доков
		});
		Write(wxT("qb"), {
			{ "", kHeader },
			{ "Document", "Dokument" },
		});
		Write(wxT("qq"), {
			{ "", kHeader },
			{ "Document", "Dokumentas" },
		});
		ibTranslations::AddCatalogLookupPathPrefix(m_root + wxFILE_SEP_PATH + wxT("lang"));
		ibTranslations::AddDomain(kDomain);
		m_processLanguage = ibTranslations::GetProcessLanguage();
		m_source = ibTranslations::SetSource(nullptr);
	}
	void TearDown() override {
		ibTranslations::SetSource(m_source);
		ibTranslations::SetProcessLanguage(m_processLanguage);
		if (!m_root.IsEmpty())
			wxFileName::Rmdir(m_root, wxPATH_RMDIR_RECURSIVE);
	}

	void Write(const wxString& language, std::vector<Entry> entries) {
		const wxString folder = m_root + wxFILE_SEP_PATH + wxT("lang") + wxFILE_SEP_PATH + language;
		ASSERT_TRUE(wxFileName::Mkdir(folder, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL));
		const std::vector<unsigned char> bytes = MoFile(std::move(entries));
		wxFile file(folder + wxFILE_SEP_PATH + kDomain + wxT(".mo"), wxFile::write);
		ASSERT_TRUE(file.IsOpened());
		ASSERT_EQ(bytes.size(), file.Write(bytes.data(), bytes.size()));
	}
};

thread_local const ibTranslations* t_set = nullptr;   // what the test's source answers for this thread
const ibTranslations* SetOfThisTestThread() { return t_set; }

} // namespace

TEST_F(TranslationFix, ACatalogIsReadWithoutAnyLocale)
{
	const ibTranslations* const qa = ibTranslations::ForLanguage(wxT("qa"));
	ASSERT_NE(qa, nullptr);
	EXPECT_EQ(wxT("qa"), qa->GetLanguage());
	EXPECT_TRUE(qa->IsLoaded(kDomain));
	const wxString* const document = qa->GetTranslatedString(wxT("Document"));
	ASSERT_NE(document, nullptr);
	EXPECT_EQ(wxString::FromUTF8("\xD0\x94\xD0\xBE\xD0\xBA\xD1\x83\xD0\xBC\xD0\xB5\xD0\xBD\xD1\x82"), *document);
	EXPECT_EQ(nullptr, qa->GetTranslatedString(wxT("Nobody wrote this")));
	EXPECT_EQ(nullptr, qa->GetTranslatedString(wxString()));
	EXPECT_EQ(qa, ibTranslations::ForLanguage(wxT("qa"))) << "one set per language, kept";
}

TEST_F(TranslationFix, ThePluralRuleAndTheContextAreTheCatalogs)
{
	ibTranslations::SetProcessLanguage(wxT("qa"));
	EXPECT_EQ(wxString::FromUTF8("%d \xD0\xB4\xD0\xBE\xD0\xBA"), ibTranslations::TranslatePlural(wxT("%d document"), wxT("%d documents"), 1));
	EXPECT_EQ(wxString::FromUTF8("%d \xD0\xB4\xD0\xBE\xD0\xBA\xD0\xB0"), ibTranslations::TranslatePlural(wxT("%d document"), wxT("%d documents"), 3));
	EXPECT_EQ(wxString::FromUTF8("%d \xD0\xB4\xD0\xBE\xD0\xBA\xD0\xBE\xD0\xB2"), ibTranslations::TranslatePlural(wxT("%d document"), wxT("%d documents"), 11));
	EXPECT_EQ(wxString::FromUTF8("%d \xD0\xB4\xD0\xBE\xD0\xBA"), ibTranslations::TranslatePlural(wxT("%d document"), wxT("%d documents"), 21));
	// The same word in two contexts is two entries.
	EXPECT_EQ(wxString::FromUTF8("\xD0\xA3\xD0\xB4\xD0\xB0\xD0\xBB\xD0\xB8\xD1\x82\xD1\x8C"), ibTranslations::Translate(wxT("Delete")));
	EXPECT_EQ(wxString::FromUTF8("\xD0\x92\xD0\xB8\xD0\xBB\xD1\x83\xD1\x87\xD0\xB8\xD1\x82\xD0\xB8"), ibTranslations::Translate(wxT("Delete"), wxT("access right")));
	// …and the macros are these doors.
	EXPECT_EQ(wxString::FromUTF8("\xD0\x94\xD0\xBE\xD0\xBA\xD1\x83\xD0\xBC\xD0\xB5\xD0\xBD\xD1\x82"), _("Document"));
	EXPECT_EQ(wxString::FromUTF8("\xD0\x92\xD0\xB8\xD0\xBB\xD1\x83\xD1\x87\xD0\xB8\xD1\x82\xD0\xB8"), wxGETTEXT_IN_CONTEXT("access right", "Delete"));
	EXPECT_EQ(wxString::FromUTF8("%d \xD0\xB4\xD0\xBE\xD0\xBA\xD0\xB0"), wxPLURAL("%d document", "%d documents", 2));
	// A plural nobody translated falls to the English pair.
	EXPECT_EQ(wxT("%d rows"), ibTranslations::TranslatePlural(wxT("%d row"), wxT("%d rows"), 2));
	EXPECT_EQ(wxT("%d row"), ibTranslations::TranslatePlural(wxT("%d row"), wxT("%d rows"), 1));
}

TEST_F(TranslationFix, ARegionFallsToItsLanguage)
{
	const ibTranslations* const regional = ibTranslations::ForLanguage(wxT("qq_RR"));
	ASSERT_NE(regional, nullptr);
	EXPECT_TRUE(regional->IsLoaded(kDomain)) << "lang/qq holds it";
	const wxString* const document = regional->GetTranslatedString(wxT("Document"));
	ASSERT_NE(document, nullptr);
	EXPECT_EQ(wxT("Dokumentas"), *document);
}

TEST_F(TranslationFix, AMissingCatalogGivesTheTextBack)
{
	const ibTranslations* const none = ibTranslations::ForLanguage(wxT("qz"));
	ASSERT_NE(none, nullptr);
	EXPECT_FALSE(none->IsLoaded(kDomain));
	ibTranslations::SetProcessLanguage(wxT("qz"));
	EXPECT_EQ(wxT("Document"), _("Document"));
	ibTranslations::SetProcessLanguage(wxString());
	EXPECT_EQ(wxT("Document"), _("Document")) << "no language at all: the text";
	EXPECT_EQ(nullptr, ibTranslations::ForLanguage(wxString()));
}

TEST_F(TranslationFix, TheProcessLanguageIsWhatAThreadWithoutASourceSpeaks)
{
	ibTranslations::SetProcessLanguage(wxT("qb"));
	EXPECT_EQ(wxT("Dokument"), _("Document"));
	// A source that answers nothing leaves the process's language in force…
	t_set = nullptr;
	ibTranslations::SetSource(&SetOfThisTestThread);
	EXPECT_EQ(wxT("Dokument"), _("Document"));
	// …and one that answers overrides it; the process's word changing is seen at once.
	t_set = ibTranslations::ForLanguage(wxT("qa"));
	EXPECT_EQ(wxString::FromUTF8("\xD0\x94\xD0\xBE\xD0\xBA\xD1\x83\xD0\xBC\xD0\xB5\xD0\xBD\xD1\x82"), _("Document"));
	t_set = nullptr;
	ibTranslations::SetProcessLanguage(wxT("qa"));
	EXPECT_EQ(wxString::FromUTF8("\xD0\x94\xD0\xBE\xD0\xBA\xD1\x83\xD0\xBC\xD0\xB5\xD0\xBD\xD1\x82"), _("Document"));
}

TEST_F(TranslationFix, TwoThreadsSpeakTwoLanguagesAtOnce)
{
	ibTranslations::SetSource(&SetOfThisTestThread);
	const wxString qaWord = wxString::FromUTF8("\xD0\x94\xD0\xBE\xD0\xBA\xD1\x83\xD0\xBC\xD0\xB5\xD0\xBD\xD1\x82");
	int qaWrong = 0, qbWrong = 0;
	const auto speak = [](const wxChar* language, const wxString& expected, int& wrong) {
		t_set = ibTranslations::ForLanguage(language);
		for (int i = 0; i < 2000; ++i)
			if (_("Document") != expected)
				++wrong;
	};
	std::thread a(speak, wxT("qa"), qaWord, std::ref(qaWrong));
	std::thread b(speak, wxT("qb"), wxString(wxT("Dokument")), std::ref(qbWrong));
	a.join();
	b.join();
	EXPECT_EQ(0, qaWrong);
	EXPECT_EQ(0, qbWrong);
}

// A language asked for by any of its names is one canonical name with its region - what a process says of
// itself once it speaks it - and a name wx knows no language for is nothing.
TEST(Translations, ANameIsCanonicalByAnyOfItsSpellings)
{
	wxInitializer init;
	for (const wxChar* spelling : { wxT("uk"), wxT("uk_UA"), wxT("uk-UA"), wxT("uk_UA.UTF-8"), wxT("Ukrainian") })
		EXPECT_EQ(wxT("uk_UA"), ibTranslations::CanonicalLanguage(spelling)) << spelling;
	EXPECT_EQ(wxT("ru_RU"), ibTranslations::CanonicalLanguage(wxT("ru")));
	EXPECT_TRUE(ibTranslations::CanonicalLanguage(wxT("xx_YY")).IsEmpty());
	// The machine's own, asked for with nothing: canonical already, whatever it is here.
	const wxString own = ibTranslations::CanonicalLanguage(wxString());
	EXPECT_EQ(own, ibTranslations::CanonicalLanguage(own));
}
