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

#include "core/syntaxHelper/helpCorpus.h"
#include "core/syntaxHelper/helpCategory.h"
#include "core/syntaxHelper/helpEntry.h"
#include "core/syntaxHelper/helpLoader.h"
#include "core/syntaxHelper/helpLoadError.h"

#include "backend/compiler/value.h"   // ibValue::IsRegisterCtor / GetNameObjectFromID - the registry an article's class_id is checked by

#include <wx/file.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>

#include <algorithm>
#include "core/fileSystem.h"   // ibFILE_SEP_PATH
#include <string>

namespace {

ibHelpEntry MakeEntry(const ibString& id, std::vector<ibString> categoryKeys) {
	ibHelpEntry e;
	e.id             = id;
	e.nameEn         = id;
	e.nameLocal      = id;
	e.categoryKeys   = std::move(categoryKeys);
	e.reviewed       = true;
	return e;
}

// Walks to a direct child of the root by key.
const ibHelpCategory* ChildByKey(const ibHelpCategory* parent, const ibString& key) {
	if (parent == nullptr)
		return nullptr;
	for (const auto& child : parent->children) {
		if (child && child->key == key)
			return child.get();
	}
	return nullptr;
}

} // namespace

// A function is found by its job (#646): "replace substring" found nothing, because `StrReplace` says "replace" only in
// its name, run together, and its prose says "replaced"; and an entry that has not every word of a question is still
// an answer when none has them all.
TEST(HelpCorpus, AFunctionIsFoundByTheWordsOfItsJob) {
	auto make = [](const wxChar* name, const wxChar* description) {
		ibHelpEntry e = MakeEntry(ibString(wxT("fn.")) + name, { wxT("global_functions") });
		e.nameEn = e.nameLocal = name;
		e.description = description;
		return e;
	};
	std::vector<ibHelpEntry> entries;
	entries.push_back(make(wxT("StrReplace"), wxT("The string with every occurrence of one substring replaced by another.")));
	entries.push_back(make(wxT("StrSplit"), wxT("Splits a string into an array of parts at its separators.")));
	entries.push_back(make(wxT("Find"), wxT("Where one string first occurs in another.")));

	ibHelpCorpus corpus(wxT("en"), ibHelpCorpus::Source::kPlatform, std::move(entries), {}, {});

	const auto first = [&corpus](const wxChar* query) {
		const std::vector<const ibHelpEntry*> found = corpus.SearchText(query);
		return found.empty() ? ibString() : found.front()->id;
	};
	EXPECT_EQ(first(wxT("replace substring")), wxT("fn.StrReplace"));
	EXPECT_EQ(first(wxT("split string")), wxT("fn.StrSplit"));
	EXPECT_EQ(first(wxT("replace substring everywhere")), wxT("fn.StrReplace")) << "two words of three are an answer";
	EXPECT_TRUE(corpus.SearchText(wxT("nothing here")).empty());
}

// The dictionary reaches the tree: a key that has a translation shows it.
TEST(HelpCorpus, CategoryKeysCarryTheirTranslatedName) {
	std::vector<ibHelpEntry> entries;
	entries.push_back(MakeEntry(wxT("fn.Round"), { wxT("global_functions") }));

	std::map<ibString, ibString> names;
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

	std::map<ibString, ibString> names;
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

// A section holds its classes, and a class its members (2026-11-13, Max: "the forms, a button, and then what is inside
// the button"): an article whose id, one segment shorter, is another article's stands under that one. Every member had
// its class's categories, so the members of ComObject and ComSafeArray were one list under "com", an enum's values lay
// beside its type, and Table's Columns' own members beside Table's. A class's node is the class's article as well.
TEST(HelpCorpus, AMemberStandsUnderItsClassNotBesideIt) {
	const std::vector<ibString> com = { wxT("value_types"), wxT("runtime"), wxT("com") };
	std::vector<ibHelpEntry> entries;
	entries.push_back(MakeEntry(wxT("cls.ComObject"), com));
	entries.push_back(MakeEntry(wxT("cls.ComObject.AddEventHandler"), com));
	entries.push_back(MakeEntry(wxT("cls.ComSafeArray"), com));
	entries.push_back(MakeEntry(wxT("cls.ComSafeArray.GetLength"), com));
	entries.push_back(MakeEntry(wxT("cls.Table"), com));
	entries.push_back(MakeEntry(wxT("cls.Table.Columns"), com));
	entries.push_back(MakeEntry(wxT("cls.Table.Columns.Add"), com));
	entries.push_back(MakeEntry(wxT("enum.PictureLocation"), com));
	entries.push_back(MakeEntry(wxT("enum.PictureLocation.Tile"), com));
	entries.push_back(MakeEntry(wxT("fn.Message"), com));   // "fn" is no article: it stays in the section

	ibHelpCorpus corpus(wxT("en"), ibHelpCorpus::Source::kPlatform, std::move(entries), {}, {});

	const ibHelpCategory* section =
		ChildByKey(ChildByKey(ChildByKey(corpus.GetRoot(), wxT("value_types")), wxT("runtime")), wxT("com"));
	ASSERT_NE(section, nullptr);
	ASSERT_EQ(section->entries.size(), 1u) << "a member was left beside its class";
	EXPECT_EQ(section->entries.front()->id, wxT("fn.Message"));

	const ibHelpCategory* comObject = ChildByKey(section, wxT("cls.ComObject"));
	ASSERT_NE(comObject, nullptr);
	ASSERT_NE(comObject->article, nullptr) << "a class's node is its article";
	EXPECT_EQ(comObject->article->id, wxT("cls.ComObject"));
	ASSERT_EQ(comObject->entries.size(), 1u);
	EXPECT_EQ(comObject->entries.front()->id, wxT("cls.ComObject.AddEventHandler"));

	const ibHelpCategory* safeArray = ChildByKey(section, wxT("cls.ComSafeArray"));
	ASSERT_NE(safeArray, nullptr);
	ASSERT_EQ(safeArray->entries.size(), 1u) << "the two classes' members are one list again";
	EXPECT_EQ(safeArray->entries.front()->id, wxT("cls.ComSafeArray.GetLength"));

	const ibHelpCategory* table = ChildByKey(section, wxT("cls.Table"));
	ASSERT_NE(table, nullptr);
	EXPECT_TRUE(table->entries.empty());
	const ibHelpCategory* columns = ChildByKey(table, wxT("cls.Table.Columns"));
	ASSERT_NE(columns, nullptr) << "a member that has members of its own is a node under its class";
	ASSERT_EQ(columns->entries.size(), 1u);
	EXPECT_EQ(columns->entries.front()->id, wxT("cls.Table.Columns.Add"));

	const ibHelpCategory* location = ChildByKey(section, wxT("enum.PictureLocation"));
	ASSERT_NE(location, nullptr) << "an enum's values stand under its type";
	ASSERT_EQ(location->entries.size(), 1u);
	EXPECT_EQ(location->entries.front()->id, wxT("enum.PictureLocation.Tile"));

	EXPECT_EQ(section->article, nullptr) << "a section is no article";
}

// What the id does not say, the article does (Max, the same day: "an enum of the button's goes into the button's
// section"): `owner_id` names the article this one stands under - ButtonView under Button, beside its members, and its
// own values under it by their ids. An owner nobody wrote, or owners that name each other, leave the article in its
// categories, and the corpus says so.
TEST(HelpCorpus, AnArticleStandsUnderTheOwnerItNames) {
	const std::vector<ibString> forms = { wxT("value_types"), wxT("runtime"), wxT("forms") };
	auto owned = [&forms](const wxChar* id, const wxChar* owner) {
		ibHelpEntry e = MakeEntry(id, forms);
		e.ownerId = owner;
		return e;
	};
	std::vector<ibHelpEntry> entries;
	entries.push_back(MakeEntry(wxT("cls.Button"), forms));
	entries.push_back(MakeEntry(wxT("cls.Button.Title"), forms));
	entries.push_back(owned(wxT("enum.ButtonView"), wxT("cls.Button")));
	entries.push_back(MakeEntry(wxT("enum.ButtonView.Hyperlink"), forms));
	entries.push_back(owned(wxT("enum.Lost"), wxT("cls.Nothing")));
	entries.push_back(owned(wxT("enum.Ping"), wxT("enum.Pong")));
	entries.push_back(owned(wxT("enum.Pong"), wxT("enum.Ping")));

	ibHelpCorpus corpus(wxT("en"), ibHelpCorpus::Source::kPlatform, std::move(entries), {}, {});

	const ibHelpCategory* section =
		ChildByKey(ChildByKey(ChildByKey(corpus.GetRoot(), wxT("value_types")), wxT("runtime")), wxT("forms"));
	ASSERT_NE(section, nullptr);

	const ibHelpCategory* button = ChildByKey(section, wxT("cls.Button"));
	ASSERT_NE(button, nullptr);
	ASSERT_EQ(button->entries.size(), 1u);
	EXPECT_EQ(button->entries.front()->id, wxT("cls.Button.Title"));
	const ibHelpCategory* view = ChildByKey(button, wxT("enum.ButtonView"));
	ASSERT_NE(view, nullptr) << "the button's enum stands inside the button";
	ASSERT_EQ(view->entries.size(), 1u);
	EXPECT_EQ(view->entries.front()->id, wxT("enum.ButtonView.Hyperlink"));
	EXPECT_EQ(ChildByKey(section, wxT("enum.ButtonView")), nullptr) << "and not beside it as well";

	const auto inSection = [section](const wxChar* id) {
		return std::any_of(section->entries.begin(), section->entries.end(),
			[id](const ibHelpEntry* e) { return e->id == id; });
	};
	EXPECT_TRUE(inSection(wxT("enum.Lost"))) << "an owner nobody wrote leaves the article in its categories";
	EXPECT_TRUE(inSection(wxT("enum.Ping")) && inSection(wxT("enum.Pong"))) << "owners naming each other are no tree";

	const std::vector<ibHelpLoadError>& said = corpus.LoadErrors();
	const auto saysOf = [&said](const wxChar* id) {
		return std::any_of(said.begin(), said.end(), [id](const ibHelpLoadError& e) {
			return e.severity == ibHelpLoadSeverity::kWarning && e.message.Contains(id);
		});
	};
	EXPECT_TRUE(saysOf(wxT("enum.Lost")));
	EXPECT_TRUE(saysOf(wxT("enum.Ping")));
	EXPECT_FALSE(saysOf(wxT("enum.ButtonView")));
}

// The merge carries the dictionary too. A per-configuration corpus may rename a
// section for its own corpus, the same overlay rule its entries follow.
TEST(HelpCorpus, MergingKeepsPlatformNamesAndLetsAConfigurationOverlayThem) {
	std::vector<ibHelpEntry> platformEntries;
	platformEntries.push_back(MakeEntry(wxT("fn.Round"), { wxT("global_functions") }));
	std::map<ibString, ibString> platformNames;
	platformNames[wxT("global_functions")] = wxT("Глобальные функции");
	platformNames[wxT("date")]             = wxT("Дата");

	auto platform = std::make_shared<const ibHelpCorpus>(
		wxT("ru"), ibHelpCorpus::Source::kPlatform,
		std::move(platformEntries), std::vector<ibHelpLoadError>{}, std::move(platformNames));

	std::vector<ibHelpEntry> configEntries;
	configEntries.push_back(MakeEntry(wxT("fn.OurOwn"), { wxT("date") }));
	std::map<ibString, ibString> configNames;
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
ibString WriteHelpBucket(const std::string& bucket)
{
	const ibString root = ibString(wxStandardPaths::Get().GetTempDir()) + ibFILE_SEP_PATH
		+ ibString::Format(wxT("oes_help_classid_%ld"), (long)wxGetProcessId());
	const ibString locale = root + ibFILE_SEP_PATH + wxT("en");
	wxFileName::Mkdir(locale, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
	wxFile out(locale + ibFILE_SEP_PATH + wxT("types.json"), wxFile::write);
	out.Write(bucket.data(), bucket.size());
	out.Close();
	return root;
}

bool SaysSomethingAbout(const std::vector<ibHelpLoadError>& errors, const ibString& id)
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
	const ibString root = WriteHelpBucket(
		R"({ "format": "OES-HELP-1.0", "schema_version": 1, "locale": "en", "entries": [)"
		R"({ "id": "cls.Table",    "name_local": "Table",   "name_en": "Table",   "kind": "collection", "class_id": )" + tableId + R"( },)"
		R"({ "id": "cls.Nothing",  "name_local": "Nothing", "name_en": "Nothing", "kind": "collection", "class_id": 12345 },)"
		R"({ "id": "cls.Misnamed", "name_local": "Array",   "name_en": "Array",   "kind": "collection", "class_id": )" + tableId + R"( },)"
		R"({ "id": "cls.Text",     "name_local": "Text",    "name_en": "Text",    "kind": "collection", "class_id": "VL_TABL" },)"
		R"({ "id": "kw.Plain",     "name_local": "Plain",   "name_en": "Plain",   "kind": "keyword" })"
		R"(] })");

	// The registry the backend's host hands in (ibApplicationHost::InitLocale) - the core keeps none.
	const ibHelpLoadResult loaded = LoadHelpCorpus(wxT("en"), root, wxEmptyString, [](ibClassID id) {
		return ibValue::IsRegisterCtor(id) ? ibValue::GetNameObjectFromID(id) : ibString();
	});
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

// The owner an article names arrives from its bucket (`owner_id`, beside `class_id` and `see_also`).
TEST(HelpLoader, AnOwnerIdIsRead)
{
	const ibString root = WriteHelpBucket(
		R"({ "format": "OES-HELP-1.0", "schema_version": 1, "locale": "en", "entries": [)"
		R"({ "id": "cls.Button",      "name_local": "Button",     "name_en": "Button",     "kind": "collection" },)"
		R"({ "id": "enum.ButtonView", "name_local": "ButtonView", "name_en": "ButtonView", "kind": "enum_type", "owner_id": "cls.Button" })"
		R"(] })");

	const ibHelpLoadResult loaded = LoadHelpCorpus(wxT("en"), root);
	wxFileName::Rmdir(root, wxPATH_RMDIR_RECURSIVE);
	ASSERT_TRUE(loaded.ok());
	ASSERT_NE(loaded.corpus, nullptr);

	const ibHelpEntry* view = loaded.corpus->FindById(wxT("enum.ButtonView"));
	ASSERT_NE(view, nullptr);
	EXPECT_EQ(view->ownerId, wxT("cls.Button"));
	const ibHelpEntry* button = loaded.corpus->FindById(wxT("cls.Button"));
	ASSERT_NE(button, nullptr);
	EXPECT_TRUE(button->ownerId.IsEmpty());
}

// THE VOCABULARY, PINNED FROM THE CORPUS'S SIDE. The shipped corpus writes kind words the loader did
// not know - property, method, procedure, system_procedure - and an unknown word fell through to
// kKeyword in silence, so 37 English articles (and 74 more across the other two locales) described a
// member of a class and answered `help_read` with "keyword". Nothing failed, which is why it lasted.
// Two rules here, and the second is what makes the first stay true: a word the corpus uses arrives as
// its own kind, and a word NOBODY knows is said out loud instead of passing for a keyword.
TEST(HelpLoader, EveryKindTheCorpusWritesArrivesAsItself)
{
	const ibString root = WriteHelpBucket(
		R"({ "format": "OES-HELP-1.0", "schema_version": 1, "locale": "en", "entries": [)"
		R"({ "id": "cls.Table.Columns",      "name_local": "Columns", "name_en": "Columns", "kind": "property" },)"
		R"({ "id": "cls.Table.Find",         "name_local": "Find",    "name_en": "Find",    "kind": "method" },)"
		R"({ "id": "cls.Table.Clear",        "name_local": "Clear",   "name_en": "Clear",   "kind": "procedure" },)"
		R"({ "id": "fn.Message",             "name_local": "Message", "name_en": "Message", "kind": "system_procedure" },)"
		R"({ "id": "enum.TextEncoding",      "name_local": "TextEncoding", "name_en": "TextEncoding", "kind": "enum_type" },)"
		R"({ "id": "enum.TextEncoding.Utf8", "name_local": "Utf8",    "name_en": "Utf8",    "kind": "system_enum" },)"
		R"({ "id": "kw.Invented",            "name_local": "Invented","name_en": "Invented","kind": "nonesuch" },)"
		R"({ "id": "kw.Silent",              "name_local": "Silent",  "name_en": "Silent" })"
		R"(] })");

	const ibHelpLoadResult loaded = LoadHelpCorpus(wxT("en"), root);
	wxFileName::Rmdir(root, wxPATH_RMDIR_RECURSIVE);
	ASSERT_TRUE(loaded.ok());
	ASSERT_NE(loaded.corpus, nullptr);
	const std::vector<ibHelpLoadError>& said = loaded.corpus->LoadErrors();

	// kOperator stands for "no article at all" here: none of the entries above claims it, so a
	// missing article cannot be mistaken for a kind that was read.
	auto kindOf = [&loaded](const wxChar* id) {
		const ibHelpEntry* e = loaded.corpus->FindById(ibString(id));
		return e != nullptr ? e->kind : ibHelpKind::kOperator;
	};

	EXPECT_EQ(kindOf(wxT("cls.Table.Columns")), ibHelpKind::kProperty);
	EXPECT_EQ(kindOf(wxT("cls.Table.Find")), ibHelpKind::kMethod);
	EXPECT_EQ(kindOf(wxT("cls.Table.Clear")), ibHelpKind::kProcedure);
	EXPECT_EQ(kindOf(wxT("fn.Message")), ibHelpKind::kSystemProcedure);
	EXPECT_EQ(kindOf(wxT("enum.TextEncoding")), ibHelpKind::kEnumType)
		<< "the head of an enum family is not a collection";
	EXPECT_EQ(kindOf(wxT("enum.TextEncoding.Utf8")), ibHelpKind::kSystemEnum);

	EXPECT_EQ(kindOf(wxT("kw.Invented")), ibHelpKind::kKeyword)
		<< "an unknown word still loads the prose";
	EXPECT_TRUE(SaysSomethingAbout(said, wxT("kw.Invented")))
		<< "...and says which article it could not classify";

	EXPECT_EQ(kindOf(wxT("kw.Silent")), ibHelpKind::kKeyword);
	EXPECT_FALSE(SaysSomethingAbout(said, wxT("kw.Silent")))
		<< "a MISSING kind is the documented default, not a mistake to report";
}
