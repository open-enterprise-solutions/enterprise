// =============================================================================
// OES Enterprise — shared attributes: one declaration, many objects
//
// A shared attribute is declared under Shared and then exists as a REAL attribute
// inside every object checked into its composition (docs/private/shared-attributes.md).
// What is pinned here is what broke during the arc, each case for a reason:
//
//   * the copy is created by checking in, and only ONE per object exists —
//     duplicate copies produced two columns with the same name and made every
//     later restructuring fail on ALTER TABLE;
//   * the copy answers the declaration's TYPE, not a stored one — that is what
//     lets a type change reach every carrier with no propagation pass;
//   * the copy is in the object's attribute list — the runtime builds a form from
//     exactly that list, and taking it out silently emptied the form;
//   * a copy stops being allowed the moment its declaration does;
//   * a tabular section does NOT accept one — its ResolveChild says so, and the
//     lists must agree with that.
//
// DB-free: a fresh file application, tree edits only (as test_metadataTree).
// =============================================================================

#include <gtest/gtest.h>
#include <wx/string.h>   // wxT, wxString - the engine's headers include no wx

#include "backend/metadataApplication.h"
#include "backend/metaCollection/metaObject.h"
#include "backend/metaCollection/metaSharedAttributeObject.h"
#include "backend/metaCollection/partial/catalog.h"

namespace {

// An application with one catalog and one shared-attribute declaration.
struct SharedAttributeFixture {
	ibMetaDataApplicationFile cfg;
	ibValueMetaObjectApplication* root = nullptr;
	ibValueMetaObject* catalog = nullptr;
	ibValueMetaObjectSharedAttribute* declaration = nullptr;

	SharedAttributeFixture() {
		root = cfg.GetRootMetaObject();
		catalog = cfg.CreateMetaObject(g_metaCatalogCLSID, root, /*runObject*/ false);
		declaration = dynamic_cast<ibValueMetaObjectSharedAttribute*>(
			cfg.CreateMetaObject(g_metaSharedAttributeCLSID, root, /*runObject*/ false));
	}

	// The copies inside the catalog, by clsid — deliberately NOT through the
	// declaration's own helper, so the test sees the tree rather than the API's
	// opinion of it.
	std::vector<ibValueMetaObject*> CopiesInCatalog() const {
		std::vector<ibValueMetaObject*> found;
		if (catalog == nullptr)
			return found;
		for (unsigned int i = 0; i < catalog->GetChildCount(); ++i) {
			ibValueMetaObject* child = catalog->GetChild(i);
			if (child != nullptr && child->GetClassType() == g_metaSharedAttributeColumnCLSID)
				found.emplace_back(child);
		}
		return found;
	}
};

} // namespace

TEST(SharedAttribute, DeclarationLivesUnderTheRoot) {
	SharedAttributeFixture f;
	ASSERT_NE(f.declaration, nullptr);
	EXPECT_EQ(f.cfg.GetAnyArrayObject(g_metaSharedAttributeCLSID).size(), 1u);
}

TEST(SharedAttribute, CheckingInCreatesExactlyOneCopy) {
	SharedAttributeFixture f;
	ASSERT_NE(f.declaration, nullptr);
	ASSERT_NE(f.catalog, nullptr);

	EXPECT_FALSE(f.declaration->IsCompositionObject(f.catalog));
	ASSERT_TRUE(f.declaration->SetCompositionObject(f.catalog, true));

	EXPECT_TRUE(f.declaration->IsCompositionObject(f.catalog));
	EXPECT_EQ(f.CopiesInCatalog().size(), 1u);
}

// The duplicate that made ALTER TABLE fail: checking in twice must not add a
// second copy, whatever the membership flag happens to say.
TEST(SharedAttribute, CheckingInTwiceDoesNotDuplicate) {
	SharedAttributeFixture f;
	ASSERT_TRUE(f.declaration->SetCompositionObject(f.catalog, true));
	ASSERT_TRUE(f.declaration->SetCompositionObject(f.catalog, true));

	EXPECT_EQ(f.CopiesInCatalog().size(), 1u);
}

TEST(SharedAttribute, CheckingOutRemovesTheCopy) {
	SharedAttributeFixture f;
	ASSERT_TRUE(f.declaration->SetCompositionObject(f.catalog, true));
	ASSERT_EQ(f.CopiesInCatalog().size(), 1u);

	ASSERT_TRUE(f.declaration->SetCompositionObject(f.catalog, false));

	EXPECT_FALSE(f.declaration->IsCompositionObject(f.catalog));
	for (ibValueMetaObject* copy : f.CopiesInCatalog())
		EXPECT_TRUE(copy->IsDeleted());
}

// The copy carries the declaration's NAME (it is stored, because GetName is not
// virtual) — a rename on the declaration must reach it.
TEST(SharedAttribute, CopyCarriesTheDeclarationName) {
	SharedAttributeFixture f;
	f.declaration->SetName(wxT("Organisation"));
	ASSERT_TRUE(f.declaration->SetCompositionObject(f.catalog, true));

	const std::vector<ibValueMetaObject*> copies = f.CopiesInCatalog();
	ASSERT_EQ(copies.size(), 1u);
	EXPECT_EQ(copies.front()->GetName(), wxT("Organisation"));
}

// The copy has no type of its own — it answers the declaration's. This is what
// makes a type change need no propagation.
TEST(SharedAttribute, CopyDelegatesItsTypeToTheDeclaration) {
	SharedAttributeFixture f;
	ASSERT_TRUE(f.declaration->SetCompositionObject(f.catalog, true));

	const std::vector<ibValueMetaObject*> copies = f.CopiesInCatalog();
	ASSERT_EQ(copies.size(), 1u);

	auto* const column = dynamic_cast<ibValueMetaObjectSharedAttributeColumn*>(copies.front());
	ASSERT_NE(column, nullptr);
	ASSERT_EQ(column->GetSource(), f.declaration);

	// Same object, not a copy of its contents: the address IS the declaration's.
	EXPECT_EQ(&column->GetTypeDesc(), &f.declaration->GetTypeDesc());
}

// The runtime builds a form from the object's attribute list
// (ibValueRecordDataObjectCatalog::GetSourceExplorer walks it). A copy missing
// from that list is a field missing from the form.
TEST(SharedAttribute, CopyIsInTheObjectsAttributeList) {
	SharedAttributeFixture f;
	f.declaration->SetName(wxT("Organisation"));
	ASSERT_TRUE(f.declaration->SetCompositionObject(f.catalog, true));

	auto* const catalog = dynamic_cast<ibValueMetaObjectCatalog*>(f.catalog);
	ASSERT_NE(catalog, nullptr);

	bool found = false;
	for (const ibValueMetaObjectAttributeBase* a : catalog->GetAttributeArrayObject())
		if (a != nullptr && a->GetName() == wxT("Organisation"))
			found = true;

	EXPECT_TRUE(found);
}

// A copy is alive only while its declaration is — no sweep, no propagation pass.
// Everything that builds anything filters on IsAllowed, so this is what makes the
// column disappear with the declaration.
TEST(SharedAttribute, CopyStopsBeingAllowedWithItsDeclaration) {
	SharedAttributeFixture f;
	ASSERT_TRUE(f.declaration->SetCompositionObject(f.catalog, true));

	const std::vector<ibValueMetaObject*> copies = f.CopiesInCatalog();
	ASSERT_EQ(copies.size(), 1u);
	EXPECT_TRUE(copies.front()->IsAllowed());

	f.declaration->MarkAsDeleted();
	EXPECT_FALSE(copies.front()->IsAllowed());
}

// Who may carry one is asked of the object, never listed: the catalog/document
// line says yes, and the kinds with no place for a column say no.
TEST(SharedAttribute, CompositionIsAllowedOnStoredObjectsOnly) {
	SharedAttributeFixture f;
	ASSERT_NE(f.catalog, nullptr);
	EXPECT_TRUE(f.catalog->IsCompositionAllowed());

	// A declaration is an attribute under the root — it carries nothing itself.
	EXPECT_FALSE(f.declaration->IsCompositionAllowed());
}

// The lists and ResolveChild must agree: an owner that does not ACCEPT the copy
// must not be offered it, or the copy would be created and never saved.
TEST(SharedAttribute, OwnerAcceptsTheCopyOnlyWhenCompositionIsAllowed) {
	SharedAttributeFixture f;
	ASSERT_NE(f.catalog, nullptr);

	EXPECT_GT(f.catalog->ResolveChild(g_metaSharedAttributeColumnCLSID), 0u);
	// The declaration itself hosts no columns.
	EXPECT_EQ(f.declaration->ResolveChild(g_metaSharedAttributeColumnCLSID), 0u);
}

// =============================================================================
// The composition mechanism itself (compositionHelper.h) — its own set, kept
// apart from the section mechanism on purpose.
// =============================================================================

TEST(Composition, MembershipIsRecordedPerId) {
	SharedAttributeFixture f;
	ASSERT_NE(f.catalog, nullptr);

	const ibMetaID id = f.declaration->GetMetaID();
	EXPECT_FALSE(f.catalog->IsInComposition(id));

	f.catalog->SetComposition(id, true);
	EXPECT_TRUE(f.catalog->IsInComposition(id));

	f.catalog->SetComposition(id, false);
	EXPECT_FALSE(f.catalog->IsInComposition(id));
}

// The two mechanisms share a shape, never a set: an id checked into one must not
// be visible to the other, or a walk over "what am I part of" could not say
// "as what".
TEST(Composition, DoesNotShareItsSetWithSections) {
	SharedAttributeFixture f;
	const ibMetaID id = f.declaration->GetMetaID();

	f.catalog->SetComposition(id, true);
	EXPECT_TRUE(f.catalog->IsInComposition(id));
	EXPECT_FALSE(f.catalog->IsSetInterface(id));

	f.catalog->SetInterface(id, true);
	f.catalog->SetComposition(id, false);
	EXPECT_TRUE(f.catalog->IsSetInterface(id));    // untouched by the other mechanism
	EXPECT_FALSE(f.catalog->IsInComposition(id));
}
