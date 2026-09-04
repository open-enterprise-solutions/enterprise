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
