// =============================================================================
// OES Enterprise — common attributes: one declaration, many objects
//
// A common attribute is declared under Common and then exists as a REAL attribute
// inside every object checked into its composition (docs/private/common-attributes.md).
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
// DB-free: a fresh file configuration, tree edits only (as test_metadataTree).
// =============================================================================

#include <gtest/gtest.h>

#include <memory>

#include <wx/init.h>

#include "backend/appData.h"
#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/metaObject.h"
#include "backend/metaCollection/metaCommonAttributeObject.h"
#include "backend/metaCollection/partial/catalog.h"
#include "backend/metaCollection/partial/document.h"
#include "backend/session/session.h"

namespace {

// A configuration with one catalog and one common-attribute declaration.
struct CommonAttributeFixture {
	ibMetaDataConfigurationFile cfg;
	ibValueMetaObjectConfiguration* root = nullptr;
	ibValueMetaObject* catalog = nullptr;
	ibValueMetaObjectCommonAttribute* declaration = nullptr;

	CommonAttributeFixture() {
		root = cfg.GetCommonMetaObject();
		catalog = cfg.CreateMetaObject(g_metaCatalogCLSID, root, /*runObject*/ false);
		declaration = dynamic_cast<ibValueMetaObjectCommonAttribute*>(
			cfg.CreateMetaObject(g_metaCommonAttributeCLSID, root, /*runObject*/ false));
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
			if (child != nullptr && child->GetClassType() == g_metaCommonAttributeColumnCLSID)
				found.emplace_back(child);
		}
		return found;
	}
};

} // namespace

TEST(CommonAttribute, DeclarationLivesUnderTheRoot) {
	CommonAttributeFixture f;
	ASSERT_NE(f.declaration, nullptr);
	EXPECT_EQ(f.cfg.GetAnyArrayObject(g_metaCommonAttributeCLSID).size(), 1u);
}

TEST(CommonAttribute, CheckingInCreatesExactlyOneCopy) {
	CommonAttributeFixture f;
	ASSERT_NE(f.declaration, nullptr);
	ASSERT_NE(f.catalog, nullptr);

	EXPECT_FALSE(f.declaration->IsCompositionObject(f.catalog));
	ASSERT_TRUE(f.declaration->SetCompositionObject(f.catalog, true));

	EXPECT_TRUE(f.declaration->IsCompositionObject(f.catalog));
	EXPECT_EQ(f.CopiesInCatalog().size(), 1u);
}

// The duplicate that made ALTER TABLE fail: checking in twice must not add a
// second copy, whatever the membership flag happens to say.
TEST(CommonAttribute, CheckingInTwiceDoesNotDuplicate) {
	CommonAttributeFixture f;
	ASSERT_TRUE(f.declaration->SetCompositionObject(f.catalog, true));
	ASSERT_TRUE(f.declaration->SetCompositionObject(f.catalog, true));

	EXPECT_EQ(f.CopiesInCatalog().size(), 1u);
}

TEST(CommonAttribute, CheckingOutRemovesTheCopy) {
	CommonAttributeFixture f;
	ASSERT_TRUE(f.declaration->SetCompositionObject(f.catalog, true));
	ASSERT_EQ(f.CopiesInCatalog().size(), 1u);

	ASSERT_TRUE(f.declaration->SetCompositionObject(f.catalog, false));

	EXPECT_FALSE(f.declaration->IsCompositionObject(f.catalog));
	for (ibValueMetaObject* copy : f.CopiesInCatalog())
		EXPECT_TRUE(copy->IsDeleted());
}

// The copy carries the declaration's NAME (it is stored, because GetName is not
// virtual) — a rename on the declaration must reach it.
TEST(CommonAttribute, CopyCarriesTheDeclarationName) {
	CommonAttributeFixture f;
	f.declaration->SetName(wxT("Organisation"));
	ASSERT_TRUE(f.declaration->SetCompositionObject(f.catalog, true));

	const std::vector<ibValueMetaObject*> copies = f.CopiesInCatalog();
	ASSERT_EQ(copies.size(), 1u);
	EXPECT_EQ(copies.front()->GetName(), wxT("Organisation"));
}

// The copy has no type of its own — it answers the declaration's. This is what
// makes a type change need no propagation.
TEST(CommonAttribute, CopyDelegatesItsTypeToTheDeclaration) {
	CommonAttributeFixture f;
	ASSERT_TRUE(f.declaration->SetCompositionObject(f.catalog, true));

	const std::vector<ibValueMetaObject*> copies = f.CopiesInCatalog();
	ASSERT_EQ(copies.size(), 1u);

	auto* const column = dynamic_cast<ibValueMetaObjectCommonAttributeColumn*>(copies.front());
	ASSERT_NE(column, nullptr);
	ASSERT_EQ(column->GetSource(), f.declaration);

	// Same object, not a copy of its contents: the address IS the declaration's.
	EXPECT_EQ(&column->GetTypeDesc(), &f.declaration->GetTypeDesc());
}

// The runtime builds a form from the object's attribute list
// (ibValueRecordDataObjectCatalog::GetSourceExplorer walks it). A copy missing
// from that list is a field missing from the form.
TEST(CommonAttribute, CopyIsInTheObjectsAttributeList) {
	CommonAttributeFixture f;
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
TEST(CommonAttribute, CopyStopsBeingAllowedWithItsDeclaration) {
	CommonAttributeFixture f;
	ASSERT_TRUE(f.declaration->SetCompositionObject(f.catalog, true));

	const std::vector<ibValueMetaObject*> copies = f.CopiesInCatalog();
	ASSERT_EQ(copies.size(), 1u);
	EXPECT_TRUE(copies.front()->IsAllowed());

	f.declaration->MarkAsDeleted();
	EXPECT_FALSE(copies.front()->IsAllowed());
}

// Who may carry one is asked of the object, never listed: the catalog/document
// line says yes, and the kinds with no place for a column say no.
TEST(CommonAttribute, CompositionIsAllowedOnStoredObjectsOnly) {
	CommonAttributeFixture f;
	ASSERT_NE(f.catalog, nullptr);
	EXPECT_TRUE(f.catalog->IsCompositionAllowed());

	// A declaration is an attribute under the root — it carries nothing itself.
	EXPECT_FALSE(f.declaration->IsCompositionAllowed());
}

// The lists and ResolveChild must agree: an owner that does not ACCEPT the copy
// must not be offered it, or the copy would be created and never saved.
TEST(CommonAttribute, OwnerAcceptsTheCopyOnlyWhenCompositionIsAllowed) {
	CommonAttributeFixture f;
	ASSERT_NE(f.catalog, nullptr);

	EXPECT_GT(f.catalog->ResolveChild(g_metaCommonAttributeColumnCLSID), 0u);
	// The declaration itself hosts no columns.
	EXPECT_EQ(f.declaration->ResolveChild(g_metaCommonAttributeColumnCLSID), 0u);
}

// =============================================================================
// The composition mechanism itself (compositionHelper.h) — its own set, kept
// apart from the section mechanism on purpose.
// =============================================================================

TEST(Composition, MembershipIsRecordedPerId) {
	CommonAttributeFixture f;
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
	CommonAttributeFixture f;
	const ibMetaID id = f.declaration->GetMetaID();

	f.catalog->SetComposition(id, true);
	EXPECT_TRUE(f.catalog->IsInComposition(id));
	EXPECT_FALSE(f.catalog->IsSetInterface(id));

	f.catalog->SetInterface(id, true);
	f.catalog->SetComposition(id, false);
	EXPECT_TRUE(f.catalog->IsSetInterface(id));    // untouched by the other mechanism
	EXPECT_FALSE(f.catalog->IsInComposition(id));
}

// =============================================================================
// A fresh start. The column is in the database (the session that checked the
// object in already bound the declaration and applied the table). A process
// that only LOADS the configuration has the source id and a null pointer.
// RunSubtree refuses a deleted node, and the pointer is filled in only from
// OnBeforeRun — so reading "null pointer" as deleted left the field off the
// document and unknown to a query, with the column sitting in the table.
// =============================================================================

namespace {

ibValueMetaObjectCommonAttributeColumn* ColumnNamed(ibValueMetaObject* owner, const wxString& name) {
	if (owner == nullptr)
		return nullptr;
	for (unsigned int i = 0; i < owner->GetChildCount(); ++i) {
		auto* column = dynamic_cast<ibValueMetaObjectCommonAttributeColumn*>(owner->GetChild(i));
		if (column != nullptr && column->GetName() == name)
			return column;
	}
	return nullptr;
}

} // namespace

TEST(CommonAttribute, ReloadedCopyIsADocumentFieldAndQueryAttribute) {
	wxInitializer wxInit;
	if (!wxInit.IsOk())
		GTEST_SKIP() << "wxBase init failed (no wxApp host)";
	const bool ownEnv = ibApplicationInstance::Get() == nullptr;
	if (ownEnv && !ibApplicationInstance::CreateAppDataEnv(ibRunMode::eFILE_MODE))
		GTEST_SKIP() << "appData env unavailable headless";
	if (ibApplicationInstance::Get() == nullptr)
		GTEST_SKIP() << "appData env unavailable headless";
	struct EnvGuard {
		bool own = false;
		~EnvGuard() {
			if (own && ibApplicationInstance::Get() != nullptr)
				ibApplicationInstance::DestroyAppDataEnv();
		}
	} env{ ownEnv };

	const wxString field = wxT("AuthorEmployee");

	ibMetaDataConfigurationFile cfg;
	ibValueMetaObjectConfiguration* root = cfg.GetCommonMetaObject();
	ASSERT_NE(root, nullptr);
	ibValueMetaObject* document = cfg.CreateMetaObject(g_metaDocumentCLSID, root, /*runObject*/ false);
	auto* declaration = dynamic_cast<ibValueMetaObjectCommonAttribute*>(
		cfg.CreateMetaObject(g_metaCommonAttributeCLSID, root, /*runObject*/ false));
	ASSERT_NE(document, nullptr);
	ASSERT_NE(declaration, nullptr);
	declaration->SetName(field);
	ASSERT_TRUE(declaration->SetCompositionObject(document, true));

	wxMemoryBuffer saved;
	ASSERT_TRUE(cfg.SaveConfigToBuffer(saved));

	ibMetaDataConfigurationFile loaded;
	ASSERT_TRUE(loaded.LoadConfigFromBuffer(saved));

	const std::vector<ibValueMetaObject*> documents = loaded.GetAnyArrayObject(g_metaDocumentCLSID);
	ASSERT_EQ(documents.size(), 1u);
	auto* doc = dynamic_cast<ibValueMetaObjectDocument*>(documents.front());
	ASSERT_NE(doc, nullptr);

	ibValueMetaObjectCommonAttributeColumn* column = ColumnNamed(doc, field);
	ASSERT_NE(column, nullptr);
	// The id survived the save. The pointer did not — and that must not read as deleted,
	// or the run below never reaches the bind.
	EXPECT_EQ(column->GetSource(), nullptr);
	EXPECT_NE(column->GetSourceMetaID(), 0);
	EXPECT_FALSE(column->IsDeleted());

	ASSERT_TRUE(loaded.RunDatabase()) << "the reloaded configuration did not run";

	EXPECT_NE(column->GetSource(), nullptr);
	EXPECT_TRUE(column->IsAllowed());
	EXPECT_FALSE(column->IsDeleted());
	EXPECT_EQ(column->GetName(), field);

	bool listed = false;
	for (const ibValueMetaObjectAttributeBase* attribute : doc->GetGenericAttributeArrayObject())
		if (attribute != nullptr && attribute->GetName() == field)
			listed = true;
	EXPECT_TRUE(listed) << "the document's attribute list — what an object field is built from — omitted the copy";
	EXPECT_NE(doc->FindAnyAttributeObjectByFilter(field), nullptr);
	ASSERT_NE(doc->GetQueryable(), nullptr);
	EXPECT_NE(doc->GetQueryable()->ResolveColumnByName(field), nullptr)
		<< "a query naming the common attribute is refused as an unknown attribute";

	// The same question a script asks: Documents.<Name>.CreateDocument().<Field>.
	// The session root has to be compiled first — the object's module is compiled
	// against it, and a root that has never run has no parent bytecode.
	{
		const auto session = std::make_shared<ibSession>(wxString(wxT("common-attribute")), ibSessionKind::Enterprise);
		const ibSessionScope scope(session.get());
		ASSERT_NE(session->CreateRoot(&loaded), nullptr);
		ASSERT_TRUE(session->CompileRoot());

		ibValuePtr<ibValueRecordDataObjectRef> created = doc->CreateObjectValue();
		ibValueRecordDataObjectRef* object = created;
		ASSERT_NE(object, nullptr);
		const long prop = object->FindProp(field);
		ASSERT_GE(prop, 0) << "the document object has no field AuthorEmployee";
		ASSERT_TRUE(object->SetPropVal(prop, ibValue(wxT("clerk"))));
		ibValue read;
		ASSERT_TRUE(object->GetPropVal(prop, read));
		EXPECT_EQ(read.GetString(), wxT("clerk"));
		created = nullptr;
	}

	if (loaded.IsConfigOpen()) {
		EXPECT_TRUE(loaded.CloseDatabase());
	}
}
