// =============================================================================
// OES Enterprise — what a PASTED subtree must not inherit: an identity.
//
// A metaobject's inner module (ibPropertyInnerModule, metaCollection/metaModuleObject.h)
// travels as its WHOLE node — code, guid and metaID together — because LoadNode
// is a full deserialization. So the module ADOPTS both identities off the
// payload, and the paste has to hand both back:
//
//   * the GUID, because a module caches its compiled bytecode BY guid
//     (sys_bytecode_cache / g_byteCodeRegistry); keeping the source's shares the
//     original's cache row and loads the wrong owner's bytecode
//     ("Binding type mismatch for 'ThisObject'");
//   * the METAID, which was the half left behind until 2026-08-31. A copied
//     object's modules kept the SOURCE's ids — two objects in one configuration
//     answering to the same number, with the copy shadowed
//     (ibFindMetaObjectById returns the original for both). Not cosmetic: the
//     physical column is named `fld<id>`, and ibMetaData::GenerateNewID never
//     re-issues one for exactly that reason.
//
// Both are safe to reset for the same reason: an ordinary metaobject re-homes
// its bindings BY guid and so must adopt the source's, while a module has no
// re-homed hops and nothing addresses it by either identity — the module storage
// holds pointers, not keys.
//
// DB-FREE AND UI-FREE by construction, the same way test_metadataSerialize is:
// a fresh ibMetaDataConfigurationFile (public ctor, never "run"), objects created
// with runObject=false, and the copy/paste exercised at the PROPERTY door
// (CopyNodeValue / PasteNodeValue) — the same door ibValueMetaObject::PasteObject
// drives through ibPropertyObject::PasteProperty, with none of the metaobject
// RUN machinery a live configuration would bring.
// =============================================================================

#include <gtest/gtest.h>

#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/metaObject.h"        // ibValueMetaObject + g_meta*CLSID
#include "backend/metaCollection/metaModuleObject.h"  // ibValueMetaObjectModule
#include "backend/serialize/dataBuilder.h"            // ibDataValue — the clipboard's shape

namespace {

// The property both a copy and a paste travel through, reached BY NAME — the way
// PasteProperty reaches it (a paste is a merge by name).
ibProperty* ModuleProperty(ibValueMetaObject* object) {
	return object != nullptr ? object->GetProperty(wxT("ObjectModule")) : nullptr;
}

// The module metaobject itself, reached as the CHILD it is (a predefined child,
// created in its owner's ctor). Taken this way rather than through the owner's
// const GetObjectModule() so the module can be WRITTEN to, and so the test sees
// the tree rather than an accessor's opinion of it.
//
// The manager module is a different class (ibValueMetaObjectManagerModule derives
// from ibValueMetaObjectCommonModule), so the cast alone already separates them;
// the name check says which one is meant.
ibValueMetaObjectModule* ObjectModuleOf(ibValueMetaObject* owner) {
	if (owner == nullptr)
		return nullptr;
	for (unsigned int idx = 0; idx < owner->GetChildCount(); idx++) {
		ibValueMetaObjectModule* module =
			dynamic_cast<ibValueMetaObjectModule*>(owner->GetChild(idx));
		if (module != nullptr && module->GetName() == wxT("ObjectModule"))
			return module;
	}
	return nullptr;
}

// Two catalogs in one fresh configuration — a source to copy from and a target
// to paste onto, which is what Ctrl+C / Ctrl+V in the designer amounts to.
struct PasteFixture {
	ibMetaDataConfigurationFile cfg;
	ibValueMetaObject* source = nullptr;
	ibValueMetaObject* target = nullptr;

	PasteFixture() {
		ibValueMetaObjectConfiguration* root = cfg.GetCommonMetaObject();
		if (root == nullptr)
			return;
		source = cfg.CreateMetaObject(g_metaCatalogCLSID, root, /*runObject*/ false);
		target = cfg.CreateMetaObject(g_metaCatalogCLSID, root, /*runObject*/ false);
	}
};

} // namespace

// ⚠ THE DEFECT, fixed 2026-08-31 by making the paste call ResetAll() where it
// called ResetGuid(). Two objects in one configuration answering to one metaID
// is a silent wrong answer, not a refusal — and the number names a column.
TEST(PropertyInnerModule, PasteNodeValue_ModuleCopiedFromAnotherObject_TakesAFreshMetaId)
{
	PasteFixture f;
	ASSERT_NE(nullptr, f.source);
	ASSERT_NE(nullptr, f.target);

	ibValueMetaObjectModule* sourceModule = ObjectModuleOf(f.source);
	ibValueMetaObjectModule* targetModule = ObjectModuleOf(f.target);
	ASSERT_NE(nullptr, sourceModule);
	ASSERT_NE(nullptr, targetModule);

	// Independently created objects already have distinct module ids — so a
	// failure below is the PASTE's doing, not the creation's.
	const ibMetaID sourceId = sourceModule->GetMetaID();
	const ibMetaID targetIdBefore = targetModule->GetMetaID();
	ASSERT_NE(sourceId, targetIdBefore);

	ibProperty* from = ModuleProperty(f.source);
	ibProperty* to = ModuleProperty(f.target);
	ASSERT_NE(nullptr, from);
	ASSERT_NE(nullptr, to);

	ibDataValue payload;
	ASSERT_TRUE(from->CopyNodeValue(payload));
	ASSERT_TRUE(to->PasteNodeValue(payload));

	const ibMetaID targetIdAfter = ObjectModuleOf(f.target)->GetMetaID();

	// It did NOT keep the source's number…
	EXPECT_NE(sourceId, targetIdAfter);
	// …and it is a NEW number, not the one it happened to hold before either:
	// the payload's id was adopted by LoadNode and then handed back.
	EXPECT_NE(targetIdBefore, targetIdAfter);
}

// The guid, for its own reason — bytecode is cached BY guid, and sharing the
// source's loads the wrong owner's compiled module.
TEST(PropertyInnerModule, PasteNodeValue_ModuleCopiedFromAnotherObject_TakesAFreshGuid)
{
	PasteFixture f;
	ASSERT_NE(nullptr, f.source);
	ASSERT_NE(nullptr, f.target);

	ibValueMetaObjectModule* sourceModule = ObjectModuleOf(f.source);
	ibValueMetaObjectModule* targetModule = ObjectModuleOf(f.target);
	ASSERT_NE(nullptr, sourceModule);
	ASSERT_NE(nullptr, targetModule);

	const wxString sourceGuid = sourceModule->GetGuid().str();
	const wxString targetGuidBefore = targetModule->GetGuid().str();
	ASSERT_NE(sourceGuid, targetGuidBefore);

	ibDataValue payload;
	ASSERT_TRUE(ModuleProperty(f.source)->CopyNodeValue(payload));
	ASSERT_TRUE(ModuleProperty(f.target)->PasteNodeValue(payload));

	const wxString targetGuidAfter = ObjectModuleOf(f.target)->GetGuid().str();

	EXPECT_NE(sourceGuid, targetGuidAfter);
	EXPECT_NE(targetGuidBefore, targetGuidAfter);
	EXPECT_FALSE(targetGuidAfter.IsEmpty());
}

// …and the paste is a REAL copy, not a no-op that satisfies the two tests above
// by doing nothing at all: the code travels.
TEST(PropertyInnerModule, PasteNodeValue_ModuleCopiedFromAnotherObject_CarriesTheCode)
{
	PasteFixture f;
	ASSERT_NE(nullptr, f.source);
	ASSERT_NE(nullptr, f.target);

	ibValueMetaObjectModule* sourceModule = ObjectModuleOf(f.source);
	ASSERT_NE(nullptr, sourceModule);

	const wxString code = wxT("Procedure OnWrite(Cancel)\n\t// written by the test\nEndProcedure");
	sourceModule->SetModuleText(code);
	ASSERT_EQ(code, sourceModule->GetModuleText());

	ibDataValue payload;
	ASSERT_TRUE(ModuleProperty(f.source)->CopyNodeValue(payload));
	ASSERT_TRUE(ModuleProperty(f.target)->PasteNodeValue(payload));

	ibValueMetaObjectModule* targetModule = ObjectModuleOf(f.target);
	ASSERT_NE(nullptr, targetModule);
	EXPECT_EQ(code, targetModule->GetModuleText());

	// The source is untouched by having been copied.
	EXPECT_EQ(code, sourceModule->GetModuleText());
}

// ⭐ AND AN ORDINARY VALUE PROPERTY IS THE OTHER HALF OF THE RULE: only a module
// re-mints its identity on paste. Copying a plain property carries the VALUE and
// nothing else — there is no identity in it to hand back — so this test says the
// fix is a module's rule and not a blanket one.
TEST(PropertyInnerModule, PasteNodeValue_APlainProperty_CarriesOnlyItsValue)
{
	PasteFixture f;
	ASSERT_NE(nullptr, f.source);
	ASSERT_NE(nullptr, f.target);

	// The owner's own metaID is not touched by pasting one of its properties.
	const ibMetaID targetOwnId = f.target->GetMetaID();

	ibProperty* fromComment = f.source->GetProperty(wxT("Comment"));
	ibProperty* toComment = f.target->GetProperty(wxT("Comment"));
	ASSERT_NE(nullptr, fromComment);
	ASSERT_NE(nullptr, toComment);

	f.source->SetComment(wxT("copied comment"));

	ibDataValue payload;
	ASSERT_TRUE(fromComment->CopyNodeValue(payload));
	ASSERT_TRUE(toComment->PasteNodeValue(payload));

	EXPECT_EQ(wxT("copied comment"), f.target->GetComment());
	EXPECT_EQ(targetOwnId, f.target->GetMetaID());
}
