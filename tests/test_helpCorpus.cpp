// =============================================================================
// OES Enterprise — the syntax-helper corpus
//
// THERE WERE NO TESTS HERE AT ALL, and that is the reason this file exists at
// the same time as its first fix: `_categories.json` — the dictionary of
// section names, translated into all three locales and packed into every .hlk —
// was read by nobody. `ibHelpCategory::displayName` was declared, documented as
// "looked up from _categories.json at load", and never assigned, so the tree
// view's fallback showed the raw key ("global_functions") where a translation
// was sitting in the shipped file.
//
// A dictionary nothing reads is invisible: nothing fails, nothing logs, and the
// only symptom is an English word in a Russian tree, which reads as "not
// translated yet" rather than "not loaded".
// =============================================================================

#include <gtest/gtest.h>

#include "backend/syntaxHelper/helpCorpus.h"
#include "backend/syntaxHelper/helpCategory.h"
#include "backend/syntaxHelper/helpEntry.h"
#include "backend/syntaxHelper/helpLoader.h"
#include "backend/syntaxHelper/helpLoadError.h"

#include <wx/file.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>

#include <algorithm>
#include <string>

namespace {

ibHelpEntry MakeEntry(const wxString& id, std::vector<wxString> categoryKeys) {
	ibHelpEntry e;
	e.id             = id;
	e.nameEn         = id;
	e.nameLocal      = id;
	e.categoryKeys   = std::move(categoryKeys);
	e.reviewed       = true;
	return e;
}

// Walks to a direct child of the root by key.
const ibHelpCategory* ChildByKey(const ibHelpCategory* parent, const wxString& key) {
	if (parent == nullptr)
		return nullptr;
	for (const auto& child : parent->children) {
		if (child && child->key == key)
			return child.get();
	}
	return nullptr;
}

} // namespace

// The dictionary reaches the tree: a key that has a translation shows it.
TEST(HelpCorpus, CategoryKeysCarryTheirTranslatedName) {
	std::vector<ibHelpEntry> entries;
	entries.push_back(MakeEntry(wxT("fn.Round"), { wxT("global_functions") }));

	std::map<wxString, wxString> names;
	names[wxT("global_functions")] = wxT("Глобальные функции");

	ibHelpCorpus corpus(wxT("ru"), ibHelpCorpus::Source::kPlatform,
	                    std::move(entries), {}, std::move(names));

	const ibHelpCategory* section = ChildByKey(corpus.GetRoot(), wxT("global_functions"));
	ASSERT_NE(section, nullptr);
	EXPECT_EQ(section->displayName, wxT("Глобальные функции"))
		<< "the category dictionary did not reach the tree";
}

// A key the dictionary does not mention keeps an empty display name, because
// the tree view falls back to the key — visibly untranslated beats absent.
TEST(HelpCorpus, AnUnlistedCategoryFallsBackRatherThanFailing) {
	std::vector<ibHelpEntry> entries;
	entries.push_back(MakeEntry(wxT("fn.Round"), { wxT("no_such_section") }));

	ibHelpCorpus corpus(wxT("ru"), ibHelpCorpus::Source::kPlatform,
	                    std::move(entries), {}, {});

	const ibHelpCategory* section = ChildByKey(corpus.GetRoot(), wxT("no_such_section"));
	ASSERT_NE(section, nullptr);
	EXPECT_TRUE(section->displayName.IsEmpty());
	EXPECT_EQ(section->key, wxT("no_such_section"));
}

// Nested keys are a PATH, and each level is named independently — the entry
// below sits two levels down, and both levels take their own translation.
TEST(HelpCorpus, EveryLevelOfThePathIsNamed) {
	std::vector<ibHelpEntry> entries;
	entries.push_back(MakeEntry(wxT("kw.From"), { wxT("common_lang"), wxT("linq") }));

	std::map<wxString, wxString> names;
	names[wxT("common_lang")] = wxT("Общее описание языка");
	names[wxT("linq")]        = wxT("LINQ — запросы к коллекциям");

	ibHelpCorpus corpus(wxT("ru"), ibHelpCorpus::Source::kPlatform,
	                    std::move(entries), {}, std::move(names));

	const ibHelpCategory* outer = ChildByKey(corpus.GetRoot(), wxT("common_lang"));
	ASSERT_NE(outer, nullptr);
	EXPECT_EQ(outer->displayName, wxT("Общее описание языка"));

	const ibHelpCategory* inner = ChildByKey(outer, wxT("linq"));
	ASSERT_NE(inner, nullptr);
	EXPECT_EQ(inner->displayName, wxT("LINQ — запросы к коллекциям"));
}

// The merge carries the dictionary too. A per-configuration corpus may rename a
// section for its own corpus, the same overlay rule its entries follow.
TEST(HelpCorpus, MergingKeepsPlatformNamesAndLetsAConfigurationOverlayThem) {
	std::vector<ibHelpEntry> platformEntries;
	platformEntries.push_back(MakeEntry(wxT("fn.Round"), { wxT("global_functions") }));
	std::map<wxString, wxString> platformNames;
	platformNames[wxT("global_functions")] = wxT("Глобальные функции");
	platformNames[wxT("date")]             = wxT("Дата");

	auto platform = std::make_shared<const ibHelpCorpus>(
		wxT("ru"), ibHelpCorpus::Source::kPlatform,
		std::move(platformEntries), std::vector<ibHelpLoadError>{}, std::move(platformNames));

	std::vector<ibHelpEntry> configEntries;
	configEntries.push_back(MakeEntry(wxT("fn.OurOwn"), { wxT("date") }));
	std::map<wxString, wxString> configNames;
	configNames[wxT("date")] = wxT("Даты и периоды");

	auto perConfig = std::make_shared<const ibHelpCorpus>(
		wxT("ru"), ibHelpCorpus::Source::kPerConfiguration,
		std::move(configEntries), std::vector<ibHelpLoadError>{}, std::move(configNames));

	ibHelpCorpus merged(platform, perConfig, wxT("ru"));

	const ibHelpCategory* functions = ChildByKey(merged.GetRoot(), wxT("global_functions"));
	ASSERT_NE(functions, nullptr);
	EXPECT_EQ(functions->displayName, wxT("Глобальные функции"))
		<< "a platform name was lost in the merge";

	const ibHelpCategory* dates = ChildByKey(merged.GetRoot(), wxT("date"));
	ASSERT_NE(dates, nullptr);
	EXPECT_EQ(dates->displayName, wxT("Даты и периоды"))
		<< "the configuration's own name did not overlay the platform's";
}

// =============================================================================
// The hidden class id — an article's join key to the runtime value it describes
// =============================================================================

namespace {

// A locale directory holding one bucket, in a folder of its own under the temp directory.
wxString WriteHelpBucket(const std::string& bucket)
{
	const wxString root = wxStandardPaths::Get().GetTempDir() + wxFileName::GetPathSeparator()
		+ wxString::Format(wxT("oes_help_classid_%ld"), (long)wxGetProcessId());
	const wxString locale = root + wxFileName::GetPathSeparator() + wxT("en");
	wxFileName::Mkdir(locale, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
	wxFile out(locale + wxFileName::GetPathSeparator() + wxT("types.json"), wxFile::write);
	out.Write(bucket.data(), bucket.size());
	out.Close();
	return root;
}

bool SaysSomethingAbout(const std::vector<ibHelpLoadError>& errors, const wxString& id)
{
	return std::any_of(errors.begin(), errors.end(), [&id](const ibHelpLoadError& e) {
		return e.severity == ibHelpLoadSeverity::kWarning && e.message.Contains(id);
	});
}

} // namespace

// An article on a runtime value names its class by the id the registry holds, and the loader asks the
// REGISTRY about it - so the article is joined to the value by id, not by a localised name, and an article
// whose class was removed or renamed says so at load instead of pointing at nothing.
TEST(HelpLoader, AClassIdIsCheckedAgainstTheRegistry)
{
	const std::string tableId = std::to_string(value_to_clsid("VL_TABL"));
	const wxString root = WriteHelpBucket(
		R"({ "format": "OES-HELP-1.0", "schema_version": 1, "locale": "en", "entries": [)"
		R"({ "id": "cls.Table",    "name_local": "Table",   "name_en": "Table",   "kind": "collection", "class_id": )" + tableId + R"( },)"
		R"({ "id": "cls.Nothing",  "name_local": "Nothing", "name_en": "Nothing", "kind": "collection", "class_id": 12345 },)"
		R"({ "id": "cls.Misnamed", "name_local": "Array",   "name_en": "Array",   "kind": "collection", "class_id": )" + tableId + R"( },)"
		R"({ "id": "cls.Text",     "name_local": "Text",    "name_en": "Text",    "kind": "collection", "class_id": "VL_TABL" },)"
		R"({ "id": "kw.Plain",     "name_local": "Plain",   "name_en": "Plain",   "kind": "keyword" })"
		R"(] })");

	const ibHelpLoadResult loaded = LoadHelpCorpus(wxT("en"), root);
	wxFileName::Rmdir(root, wxPATH_RMDIR_RECURSIVE);
	ASSERT_TRUE(loaded.ok());
	ASSERT_NE(loaded.corpus, nullptr);
	// What a bucket's entries are told is kept INSIDE the corpus (LoadErrors) - result.errors carries only what
	// happened around the load (a locale fallen back, a construction that failed); helpLoader.cpp says why.
	const std::vector<ibHelpLoadError>& said = loaded.corpus->LoadErrors();

	const ibHelpEntry* table = loaded.corpus->FindById(wxT("cls.Table"));
	ASSERT_NE(table, nullptr);
	EXPECT_EQ(table->classId, value_to_clsid("VL_TABL")) << "the tag resolves to the class the registry holds";
	EXPECT_FALSE(SaysSomethingAbout(said, wxT("cls.Table")));

	const ibHelpEntry* nothing = loaded.corpus->FindById(wxT("cls.Nothing"));
	ASSERT_NE(nothing, nullptr) << "a wrong join key does not cost the article its prose";
	EXPECT_EQ(nothing->classId, 0u);
	EXPECT_TRUE(SaysSomethingAbout(said, wxT("cls.Nothing"))) << "an id nothing is registered under is said";

	EXPECT_TRUE(SaysSomethingAbout(said, wxT("cls.Misnamed")))
		<< "an article about Array that points at the Table class is said";

	const ibHelpEntry* text = loaded.corpus->FindById(wxT("cls.Text"));
	ASSERT_NE(text, nullptr);
	EXPECT_EQ(text->classId, 0u) << "a class id is a number; a tag in its place is not read as one";
	EXPECT_TRUE(SaysSomethingAbout(said, wxT("cls.Text")));

	const ibHelpEntry* plain = loaded.corpus->FindById(wxT("kw.Plain"));
	ASSERT_NE(plain, nullptr);
	EXPECT_EQ(plain->classId, 0u) << "an article about no class carries none";
}
