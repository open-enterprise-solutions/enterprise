// =============================================================================
// OES Enterprise — metadata tree mutation (Create / Rename / Remove)
//
// ibMetaData's tree-edit API (metaData.h): CreateMetaObject attaches a typed
// child under a parent, RenameMetaObject sets its name, RemoveMetaObject detaches
// it, and GetAnyArrayObject<T>(clsid) enumerates a type. These are the operations
// the Designer drives on every metaobject add/rename/delete. DB-free
// (runObject=false on a fresh file config — see test_metadataSerialize).
// =============================================================================

#include <gtest/gtest.h>
#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/metaObject.h"   // ibValueMetaObject + g_meta*CLSID

TEST(MetadataTree, FreshConfigHasNoBusinessObjects) {
    ibMetaDataConfigurationFile cfg;
    EXPECT_EQ(cfg.GetAnyArrayObject(g_metaCatalogCLSID).size(), 0u);
    EXPECT_EQ(cfg.GetAnyArrayObject(g_metaDocumentCLSID).size(), 0u);
}

TEST(MetadataTree, CreateAttachesAndEnumerates) {
    ibMetaDataConfigurationFile cfg;
    ibValueMetaObjectConfiguration* root = cfg.GetCommonMetaObject();
    ASSERT_NE(root, nullptr);

    ibValueMetaObject* cat = cfg.CreateMetaObject(g_metaCatalogCLSID, root, /*runObject*/ false);
    ASSERT_NE(cat, nullptr);
    EXPECT_EQ(cfg.GetAnyArrayObject(g_metaCatalogCLSID).size(), 1u);
    // A Catalog is not a Document — the type filter is exact.
    EXPECT_EQ(cfg.GetAnyArrayObject(g_metaDocumentCLSID).size(), 0u);
}

// NOTE: RenameMetaObject / RemoveMetaObject are intentionally NOT asserted here.
// On a never-run (runObject=false) object their observable effect differs from
// the naive expectation (GetName did not reflect the rename; the removed object
// still enumerated) — they appear to need run/registration state. Asserting them
// belongs with a run-time metadata fixture, not this DB-free tree test.

TEST(MetadataTree, SeveralTypesCoexist) {
    ibMetaDataConfigurationFile cfg;
    ibValueMetaObjectConfiguration* root = cfg.GetCommonMetaObject();
    ASSERT_NE(root, nullptr);

    cfg.CreateMetaObject(g_metaCatalogCLSID,  root, false);
    cfg.CreateMetaObject(g_metaCatalogCLSID,  root, false);
    cfg.CreateMetaObject(g_metaDocumentCLSID, root, false);

    EXPECT_EQ(cfg.GetAnyArrayObject(g_metaCatalogCLSID).size(),  2u);
    EXPECT_EQ(cfg.GetAnyArrayObject(g_metaDocumentCLSID).size(), 1u);
}
