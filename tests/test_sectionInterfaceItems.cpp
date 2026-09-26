// =============================================================================
// OES Enterprise — what a section shows, and how many times.
//
// The section page of the running application lists a section's commands. A catalog is both a list
// and a create ("combined"), and ibValueMetaObjectSection::GetInterfaceItemArrayObject answers such
// an object for BOTH the Default and the Create area — deliberately, so a section's navigation bar
// can lay out each area by itself. A page that asked the four areas one after another into ONE array
// therefore held every catalog twice: the same words, twice, leading to the same list.
//
// GetInterfaceItemArrayObject() — the same question without an area — is that page's question, answered
// once per object.
//
// DB-FREE AND UI-FREE: a fresh ibMetaDataConfigurationFile, objects created with runObject=false and
// included into the section with SetInterface — the same call section_include makes.
// =============================================================================

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/metaObject.h"
#include "backend/metaCollection/metaSectionObject.h"
#include "backend/metaCollection/metaCommandGroupObject.h"

namespace {

struct SectionFix {
	ibMetaDataConfigurationFile cfg;
	ibValueMetaObjectConfiguration* root = nullptr;
	ibValueMetaObjectSection* section = nullptr;

	SectionFix() {
		root = cfg.GetCommonMetaObject();
		if (root == nullptr)
			return;
		section = dynamic_cast<ibValueMetaObjectSection*>(
			cfg.CreateMetaObject(g_metaSectionCLSID, root, /*runObject*/ false));
	}

	ibValueMetaObject* Include(const ibClassID& kind, const wxString& name, bool inSection = true) {
		ibValueMetaObject* object = cfg.CreateMetaObject(kind, root, /*runObject*/ false);
		if (object != nullptr) {
			cfg.RenameMetaObject(object, name);
			if (inSection)
				object->SetInterface(section->GetMetaID(), true);
		}
		return object;
	}
};

size_t Occurrences(const std::vector<ibValueMetaObject*>& list, const ibValueMetaObject* object) {
	return (size_t)std::count(list.begin(), list.end(), object);
}

} // namespace

// The premise, stated once: the four areas ARE NOT disjoint — a catalog answers Default and Create.
TEST(SectionInterfaceItems, ACatalog_IsAnsweredForBothTheDefaultAndTheCreateArea)
{
	SectionFix f;
	ASSERT_NE(nullptr, f.section);
	ibValueMetaObject* catalog = f.Include(g_metaCatalogCLSID, wxT("Firms"));
	ASSERT_NE(nullptr, catalog);

	std::vector<ibValueMetaObject*> defaults, creates;
	f.section->GetInterfaceItemArrayObject(ibInterfaceCommandSection_Default, defaults);
	f.section->GetInterfaceItemArrayObject(ibInterfaceCommandSection_Create, creates);

	EXPECT_EQ(1u, Occurrences(defaults, catalog));
	EXPECT_EQ(1u, Occurrences(creates, catalog)) << "a combined object answers the Create area too";
}

// The defect: the four areas asked into one array hold the catalog twice.
TEST(SectionInterfaceItems, AskingTheFourAreasIntoOneArray_ListsACatalogTwice)
{
	SectionFix f;
	ASSERT_NE(nullptr, f.section);
	ibValueMetaObject* catalog = f.Include(g_metaCatalogCLSID, wxT("Firms"));
	ASSERT_NE(nullptr, catalog);

	std::vector<ibValueMetaObject*> naive;
	f.section->GetInterfaceItemArrayObject(ibInterfaceCommandSection_Default, naive);
	f.section->GetInterfaceItemArrayObject(ibInterfaceCommandSection_Create, naive);
	f.section->GetInterfaceItemArrayObject(ibInterfaceCommandSection_Report, naive);
	f.section->GetInterfaceItemArrayObject(ibInterfaceCommandSection_Service, naive);

	EXPECT_EQ(2u, Occurrences(naive, catalog)) << "this is what the section page drew";
}

TEST(SectionInterfaceItems, EveryArea_ListsACatalogOnce)
{
	SectionFix f;
	ASSERT_NE(nullptr, f.section);
	ibValueMetaObject* catalog = f.Include(g_metaCatalogCLSID, wxT("Firms"));
	ASSERT_NE(nullptr, catalog);

	const std::vector<ibValueMetaObject*> shown = f.section->GetInterfaceItemArrayObject();
	EXPECT_EQ(1u, Occurrences(shown, catalog));
	EXPECT_EQ(1u, shown.size());
}

TEST(SectionInterfaceItems, SeveralObjects_EachOnce_InTheOrderTheyAreFirstMet)
{
	SectionFix f;
	ASSERT_NE(nullptr, f.section);
	ibValueMetaObject* firms     = f.Include(g_metaCatalogCLSID,  wxT("Firms"));
	ibValueMetaObject* staff     = f.Include(g_metaCatalogCLSID,  wxT("Staff"));
	ibValueMetaObject* documents = f.Include(g_metaDocumentCLSID, wxT("Orders"));
	ASSERT_NE(nullptr, documents);

	const std::vector<ibValueMetaObject*> shown = f.section->GetInterfaceItemArrayObject();

	EXPECT_EQ(1u, Occurrences(shown, firms));
	EXPECT_EQ(1u, Occurrences(shown, staff));
	EXPECT_EQ(1u, Occurrences(shown, documents));
	EXPECT_EQ(3u, shown.size());

	// Default area first, in the configuration's own order: Firms, Staff, then Orders.
	const auto at = [&](const ibValueMetaObject* o) { return std::find(shown.begin(), shown.end(), o) - shown.begin(); };
	EXPECT_LT(at(firms), at(staff));
	EXPECT_LT(at(staff), at(documents));
}

TEST(SectionInterfaceItems, AnObjectNotIncludedInTheSection_IsNotShown)
{
	SectionFix f;
	ASSERT_NE(nullptr, f.section);
	ibValueMetaObject* included = f.Include(g_metaCatalogCLSID, wxT("Firms"));
	ibValueMetaObject* elsewhere = f.Include(g_metaCatalogCLSID, wxT("Staff"), /*inSection*/ false);
	ASSERT_NE(nullptr, elsewhere);

	const std::vector<ibValueMetaObject*> shown = f.section->GetInterfaceItemArrayObject();
	EXPECT_EQ(1u, Occurrences(shown, included));
	EXPECT_EQ(0u, Occurrences(shown, elsewhere));
}

TEST(SectionInterfaceItems, AnEmptySection_ShowsNothing)
{
	SectionFix f;
	ASSERT_NE(nullptr, f.section);
	EXPECT_TRUE(f.section->GetInterfaceItemArrayObject().empty());
}

// =============================================================================
// COMMAND GROUPS — a command names its group in Group: one of the platform's (the area) or one the
// configuration declares (its metaID). The section files a command under a declared group there, and in
// no platform area.
// =============================================================================

namespace {

ibValueMetaObject* IncludeCommand(SectionFix& f, const wxString& name, long group) {
	ibValueMetaObject* command = f.Include(g_metaCommonCommandCLSID, name);
	if (command != nullptr)
		command->GetProperty(wxT("Group"))->SetValue(wxVariant(group));
	return command;
}

} // namespace

TEST(SectionInterfaceItems, ACommandInADeclaredGroup_IsShownUnderThatGroupAndInNoArea)
{
	SectionFix f;
	ASSERT_NE(nullptr, f.section);
	ibValueMetaObjectCommandGroup* group = dynamic_cast<ibValueMetaObjectCommandGroup*>(
		f.cfg.CreateMetaObject(g_metaCommandGroupCLSID, f.root, /*runObject*/ false));
	ASSERT_NE(nullptr, group);
	ibValueMetaObject* command = IncludeCommand(f, wxT("PrintAll"), group->GetMetaID());
	ASSERT_NE(nullptr, command);

	std::vector<ibValueMetaObject*> inGroup, normal;
	f.section->GetInterfaceItemArrayObject(group, inGroup);
	f.section->GetInterfaceItemArrayObject(ibInterfaceCommandSection_Default, normal);

	EXPECT_EQ(1u, Occurrences(inGroup, command));
	EXPECT_EQ(0u, Occurrences(normal, command)) << "a command filed under its own group sits in no platform area";
	EXPECT_EQ(1u, Occurrences(f.section->GetInterfaceItemArrayObject(), command)) << "…and the section still shows it, once";
}

// A group of the FORM command bar is no heading on a section page: its commands stand in a form's submenu, so
// the page has them neither in an area nor in its list of everything (they came out as plain links there).
TEST(SectionInterfaceItems, ACommandInAFormCommandBarGroup_IsNotOnTheSectionPage)
{
	SectionFix f;
	ASSERT_NE(nullptr, f.section);
	ibValueMetaObjectCommandGroup* group = dynamic_cast<ibValueMetaObjectCommandGroup*>(
		f.cfg.CreateMetaObject(g_metaCommandGroupCLSID, f.root, /*runObject*/ false));
	ASSERT_NE(nullptr, group);
	group->GetProperty(wxT("Category"))->SetValue(wxVariant((long)ibCommandGroupCategory_FormCommandBar));
	ASSERT_EQ(ibCommandGroupCategory_FormCommandBar, group->GetCategory());
	ibValueMetaObject* command = IncludeCommand(f, wxT("PrintInvoice"), group->GetMetaID());
	ASSERT_NE(nullptr, command);

	std::vector<ibValueMetaObject*> normal;
	f.section->GetInterfaceItemArrayObject(ibInterfaceCommandSection_Default, normal);

	EXPECT_EQ(0u, Occurrences(normal, command));
	EXPECT_EQ(0u, Occurrences(f.section->GetInterfaceItemArrayObject(), command));
}

// The defect found beside it: Important was asked by no page, so a command marked Important was on none.
TEST(SectionInterfaceItems, ACommandMarkedImportant_IsShown)
{
	SectionFix f;
	ASSERT_NE(nullptr, f.section);
	ibValueMetaObject* command = IncludeCommand(f, wxT("CloseDay"), ibInterfaceCommandSection_Important);
	ASSERT_NE(nullptr, command);

	EXPECT_EQ(1u, Occurrences(f.section->GetInterfaceItemArrayObject(), command));
}
