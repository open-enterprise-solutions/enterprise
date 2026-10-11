// =============================================================================
// OES Enterprise — ibSession composition + holder façade tests
//
// Focus: ibSession owns ibDatabaseConnectionHolder by composition (not
// inheritance) and exposes Holder() / EnsureConnection / OpenConnectionScope
// / DatabaseLayer as façade. Pool/connection-driven behaviour (auto-bind,
// scope lifecycle) is integration-test scope — needs full appData wiring +
// a live pool — and is not covered here.
// =============================================================================

#include <gtest/gtest.h>

#include <set>
#include <vector>

#include <wx/init.h>

#include "backend/session/session.h"
#include "backend/databaseLayer/connectionHolder.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#include "backend/system/systemManager.h"
#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/metaLanguageObject.h"
#include "backend/metaCollection/partial/commonObject.h"
#include "backend/metaCollection/partial/reference/reference.h"
#include "backend/compiler/compileCode.h"
#include "backend/appData.h"
#include "backend/backend_exception.h"
#include "backend/query/columnLayout.h"
#include "backend/query/dataQueryBuilder.h"

#include <type_traits>

// ---------------------------------------------------------------------------
// Composition: ibSession owns a holder, does NOT inherit one
// ---------------------------------------------------------------------------

TEST(SessionHolder, IsNotADatabaseConnectionHolder) {
    // Composition over inheritance — ibSession should NOT be derived from
    // ibDatabaseConnectionHolder. The holder is a member, accessed via
    // Holder().
    EXPECT_FALSE((std::is_base_of<ibDatabaseConnectionHolder, ibSession>::value));
}

TEST(SessionHolder, HolderAccessorReturnsNonNull) {
    ibSession sess(wxT("test-id"), ibSessionKind::Designer);
    EXPECT_NE(sess.Holder(), nullptr);
}

TEST(SessionHolder, HolderAccessorIsStable) {
    // Holder() must return the same pointer across calls — it's the
    // pool's identity key for reservations. A different pointer per call
    // would scatter TX/scope bindings across phantom holders.
    ibSession sess(wxT("test-id"), ibSessionKind::Designer);
    EXPECT_EQ(sess.Holder(), sess.Holder());
}

TEST(SessionHolder, ConstHolderAccessorMatchesNonConst) {
    ibSession sess(wxT("test-id"), ibSessionKind::Designer);
    const ibSession& csess = sess;
    EXPECT_EQ(sess.Holder(), csess.Holder());
}

TEST(SessionHolder, DistinctSessionsHaveDistinctHolders) {
    // Each session must have its own holder — pool keys reservations by
    // address, two sessions sharing one holder would conflict on TX pin.
    ibSession a(wxT("session-a"), ibSessionKind::Designer);
    ibSession b(wxT("session-b"), ibSessionKind::Designer);
    EXPECT_NE(a.Holder(), b.Holder());
}

// ---------------------------------------------------------------------------
// DatabaseLayer static — backs the ses_query macro
// ---------------------------------------------------------------------------

TEST(PlatformLanguage, CurrentLanguageFindsTheLanguageByCode) {
    ibMetaDataConfigurationFile cfg;
    ibValueMetaObject* root = cfg.GetCommonMetaObject();
    ibValueMetaObject* created = cfg.CreateMetaObject(g_metaLanguageCLSID, root, false);
    ASSERT_NE(created, nullptr);
    auto* language = dynamic_cast<ibValueMetaObjectLanguage*>(created);
    ASSERT_NE(language, nullptr);
    language->SetName(wxT("Ukrainian"));
    language->SetLangCode(wxT("uk"));

    ibValue found = ibValueSystemFunction::LanguageObject(&cfg, wxT("uk"));
    ibValueMetaObjectLanguage* asLanguage = nullptr;
    ASSERT_TRUE(found.ConvertToValue(asLanguage));
    EXPECT_EQ(asLanguage->GetName(), wxT("Ukrainian"));
    EXPECT_TRUE(ibValueSystemFunction::LanguageObject(&cfg, wxT("de")).IsEmpty());
}

TEST(PlatformPrivilege, ScriptModeDoesNotClearATrustScope) {
    // The two flags are OR'd on the session. Current() stays null until a
    // host exists, so this talks to the session itself; the global function
    // is that answer, and it refuses when there is no session.
    ibSession sess(wxT("priv"), ibSessionKind::Designer);
    EXPECT_FALSE(sess.PrivilegedMode());

    sess.SetPrivilegedMode(true);
    EXPECT_TRUE(sess.PrivilegedMode());
    {
        // A trust scope is still privileged after script turns its own flag off.
        ibAccessTrustScope trust(&sess);
        sess.SetPrivilegedMode(false);
        EXPECT_TRUE(sess.PrivilegedMode());
    }
    EXPECT_FALSE(sess.PrivilegedMode());

    // And a trust scope ending does not clear a mode script turned on.
    sess.SetPrivilegedMode(true);
    {
        ibAccessTrustScope trust(&sess);
    }
    EXPECT_TRUE(sess.PrivilegedMode());
    sess.SetPrivilegedMode(false);
    EXPECT_FALSE(sess.PrivilegedMode());
}

TEST(PlatformPrivilege, SetPrivilegedModeWithoutASessionIsNamed) {
    try {
        ibValueSystemFunction::SetPrivilegedMode(true);
        FAIL() << "a session-less call must be refused";
    } catch (const ibBackendException& err) {
        EXPECT_TRUE(err.GetErrorDescription().Contains(wxT("session")));
    }
}

TEST(PlatformPrivilege, TheNamesCompile) {
    ibCompileCode cc(wxT("test"), wxT("memory"), false);
    // Globals are the methods of the system context the host installs.
    ibValueSystemFunction valueSystem;
    cc.AddContextVariable(wxT("System"), &valueSystem, true);
    const wxString src =
        wxT("Procedure Check()\n")
        wxT("    var lang;\n")
        wxT("    lang = CurrentLanguage();\n")
        wxT("    If Not PrivilegedMode() Then\n")
        wxT("        SetPrivilegedMode(True);\n")
        wxT("    EndIf;\n")
        wxT("EndProcedure\n");
    try {
        ASSERT_TRUE(cc.Compile(src));
    } catch (const ibBackendException& err) {
        FAIL() << err.GetErrorDescription().ToStdString();
    }
}

TEST(PlatformPredefined, ACatalogPredefinedItemAndEmptyRefResolve) {
    ibMetaDataConfigurationFile cfg;
    ibValueMetaObject* root = cfg.GetCommonMetaObject();
    ibValueMetaObject* created = cfg.CreateMetaObject(g_metaCatalogCLSID, root, false);
    ASSERT_NE(created, nullptr);
    created->SetName(wxT("Warehouses"));
    auto* catalog = dynamic_cast<ibValueMetaObjectRecordDataHierarchyMutableRef*>(created);
    ASSERT_NE(catalog, nullptr);
    catalog->AppendPredefinedValue(wxT("Main"), wxT(""), wxT("Main warehouse"));
    const ibGuid guid = catalog->FindPredefinedValue(wxT("Main"))->GetPredefinedGuid();

    ibValue found = ibValueSystemFunction::PredefinedValue(&cfg, wxT("Catalog.Warehouses.Main"));
    ibValueReferenceDataObject* ref = nullptr;
    ASSERT_TRUE(found.ConvertToValue(ref));
    EXPECT_EQ(ref->GetGuid(), guid);

    ibValue empty = ibValueSystemFunction::PredefinedValue(&cfg, wxT("Catalog.Warehouses.EmptyRef"));
    ibValueReferenceDataObject* emptyRef = nullptr;
    ASSERT_TRUE(empty.ConvertToValue(emptyRef));
    EXPECT_FALSE(emptyRef->GetGuid().isValid());

    try {
        ibValueSystemFunction::PredefinedValue(&cfg, wxT("Catalog.Warehouses.Missing"));
        FAIL() << "an unknown predefined name must be refused";
    } catch (const ibBackendException& err) {
        EXPECT_TRUE(err.GetErrorDescription().Contains(wxT("Missing")));
    }
    try {
        ibValueSystemFunction::PredefinedValue(&cfg, wxT("NotAKind.Warehouses.Main"));
        FAIL() << "an unknown kind must be refused";
    } catch (const ibBackendException& err) {
        EXPECT_TRUE(err.GetErrorDescription().Contains(wxT("NotAKind")));
    }
}

TEST(PlatformLock, AReferenceIsNamedBeforeTheSession) {
    try {
        ibValueSystemFunction::LockDataForEdit(ibValue(wxT("not-a-ref")));
        FAIL() << "a string must be refused";
    } catch (const ibBackendException& err) {
        EXPECT_TRUE(err.GetErrorDescription().Contains(wxT("reference")));
    }
    try {
        ibValueSystemFunction::UnlockDataForEdit(ibValue(wxT("not-a-ref")));
        FAIL() << "a string must be refused";
    } catch (const ibBackendException& err) {
        EXPECT_TRUE(err.GetErrorDescription().Contains(wxT("reference")));
    }

    ibMetaDataConfigurationFile cfg;
    ibValueMetaObject* root = cfg.GetCommonMetaObject();
    ibValueMetaObject* created = cfg.CreateMetaObject(g_metaCatalogCLSID, root, false);
    ASSERT_NE(created, nullptr);
    created->SetName(wxT("Warehouses"));
    auto* catalog = dynamic_cast<ibValueMetaObjectRecordDataHierarchyMutableRef*>(created);
    ASSERT_NE(catalog, nullptr);
    catalog->AppendPredefinedValue(wxT("Main"), wxT(""), wxT("Main warehouse"));

    ibValue empty = ibValueSystemFunction::PredefinedValue(&cfg, wxT("Catalog.Warehouses.EmptyRef"));
    try {
        ibValueSystemFunction::LockDataForEdit(empty);
        FAIL() << "an empty reference must be refused";
    } catch (const ibBackendException& err) {
        EXPECT_TRUE(err.GetErrorDescription().Contains(wxT("empty")));
    }

    ibValue found = ibValueSystemFunction::PredefinedValue(&cfg, wxT("Catalog.Warehouses.Main"));
    try {
        ibValueSystemFunction::LockDataForEdit(found);
        FAIL() << "a reference with no session must be refused";
    } catch (const ibBackendException& err) {
        EXPECT_TRUE(err.GetErrorDescription().Contains(wxT("session")));
    }
}

TEST(PlatformLock, TheNamesCompile) {
    ibCompileCode cc(wxT("test"), wxT("memory"), false);
    ibValueSystemFunction valueSystem;
    cc.AddContextVariable(wxT("System"), &valueSystem, true);
    const wxString src =
        wxT("Procedure Check()\n")
        wxT("    LockDataForEdit(Undefined);\n")
        wxT("    UnlockDataForEdit(Undefined);\n")
        wxT("EndProcedure\n");
    try {
        ASSERT_TRUE(cc.Compile(src));
    } catch (const ibBackendException& err) {
        FAIL() << err.GetErrorDescription().ToStdString();
    }
}

TEST(PlatformLock, AVersionIsComparedWithTheStoredRow) {
    wxInitializer wxInit;
    if (!wxInit.IsOk())
        GTEST_SKIP() << "wxBase init failed";
    if (!ibApplicationInstance::CreateAppDataEnv(ibRunMode::eFILE_MODE))
        GTEST_SKIP() << "appData env unavailable";
    struct Stop {
        ~Stop() {
            if (ibApplicationInstance::Get() != nullptr)
                ibApplicationInstance::DestroyAppDataEnv();
        }
    } stop;

    auto db = std::make_shared<ibDatabaseLayerSQLite>();
    if (!db->Open(wxT(":memory:")))
        GTEST_SKIP() << "in-memory SQLite open failed";
    ibConnectionPool* pool = ibApplicationInstance::GetConnectionPool();
    ASSERT_NE(pool, nullptr);
    pool->Init(db, 1, 0);

    ibMetaDataConfigurationFile cfg;
    auto* catalog = dynamic_cast<ibValueMetaObjectRecordDataHierarchyMutableRef*>(
        cfg.CreateMetaObject(g_metaCatalogCLSID, cfg.GetCommonMetaObject(), false));
    ASSERT_NE(catalog, nullptr);
    catalog->SetName(wxT("Warehouses"));
    catalog->AppendPredefinedValue(wxT("Main"), wxT(""), wxT("Main warehouse"));
    ASSERT_TRUE(cfg.RunDatabase());

    const ibBackendQueryable* table = catalog->GetQueryable();
    ASSERT_NE(table, nullptr);
    wxString fields;
    std::set<wxString> seen;
    for (const ibBackendQueryColumn* column : table->GetColumns()) {
        for (const wxString& field : ColumnFieldNames(column)) {
            if (!seen.insert(field).second)
                continue;
            fields << (fields.IsEmpty() ? wxT("") : wxT(", ")) << field;
        }
    }
    ASSERT_FALSE(fields.IsEmpty());
    db->RunQuery(wxT("%s"), wxT("CREATE TABLE ") + table->GetQueryTableName() + wxT(" (") + fields + wxT(")"));

    auto session = std::make_shared<ibSession>(wxT("lock-version"), ibSessionKind::Enterprise);
    ibSessionScope scope(session.get());
    ibValue found = ibValueSystemFunction::PredefinedValue(&cfg, wxT("Catalog.Warehouses.Main"));
    ibDataQueryBuilder writer;
    writer.From(table);
    writer.SetValue(catalog->GetDataReference()->GetQueryColumn(), found);
    writer.SetValue(catalog->GetDataVersion()->GetQueryColumn(), ibValue(wxT("stamp-1")));
    ASSERT_TRUE(writer.Insert());
    db->RunQuery(wxT("%s"), wxT(
        "CREATE TABLE sys_lock (lockGuid TEXT, sessionGuid TEXT, namespace TEXT, "
        "keyHash TEXT, keyData TEXT, lockMode INTEGER, acquiredAt TEXT, userName TEXT, computer TEXT)"));

    try {
        ibValueSystemFunction::LockDataForEdit(found, ibValue(wxT("stamp-2")));
        FAIL() << "a stale version must be refused";
    } catch (const ibBackendLockException& err) {
        EXPECT_EQ(err.GetKind(), ibBackendLockException::Kind::VersionChanged);
        EXPECT_TRUE(err.GetErrorDescription().Contains(wxT("stamp-2")));
        EXPECT_TRUE(err.GetErrorDescription().Contains(wxT("stamp-1")));
    }
    ibValueSystemFunction::LockDataForEdit(found, ibValue(wxT("stamp-1")));
    ibValueSystemFunction::UnlockDataForEdit(found);
}

TEST(SessionDbLayer, ThrowsWhenNoCurrentSession) {
    // No SessionScope active on this thread → ibSession::Current() is
    // null → DatabaseLayer() throws an explicit error rather than
    // silently returning the wrong conn.
    EXPECT_THROW(
        ibSession::DatabaseLayer(),
        ibBackendException);
}
