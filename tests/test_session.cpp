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

#include "backend/session/session.h"
#include "backend/databaseLayer/connectionHolder.h"
#include "backend/appData.h"
#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/metaSessionParameterObject.h"
#include "backend/system/value/valueArray.h"

#include <memory>
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

TEST(SessionDbLayer, ThrowsWhenNoCurrentSession) {
    // No SessionScope active on this thread → ibSession::Current() is
    // null → DatabaseLayer() throws an explicit error rather than
    // silently returning the wrong conn.
    EXPECT_THROW(
        ibSession::DatabaseLayer(),
        ibBackendException);
}

// ---------------------------------------------------------------------------
// SessionParameters.Name reads as the stored value
// ---------------------------------------------------------------------------
//
// A script writes SessionParameters.CurrentUser.Employee, compares the parameter
// with a reference, and puts it in an array. Those all see whatever GetPropVal
// returned. The holder that performs the read is not that value: it has none of
// the reference's fields, TypeOf names the holder, and '=' compares two holders.
//
// The manager is held by pointer: its member table binds `this`, and a copy would
// keep the temporary's address.

namespace {

struct SessionParameterRead : ::testing::Test {
	std::shared_ptr<ibSession> session;
	std::unique_ptr<ibSessionScope> scope;
	std::unique_ptr<ibMetaDataConfigurationFile> cfg;
	std::unique_ptr<ibValueSessionParameters> parameters;
	long index = -1;

	void SetUp() override {
		// Current() answers nothing until a base exists. The read under test asks it.
		if (!ibApplicationInstance::CreateAppDataEnv(ibRunMode::eFILE_MODE))
			GTEST_SKIP() << "appData env unavailable";
		session = std::make_shared<ibSession>(wxT("session-params"), ibSessionKind::Designer);
		scope = std::make_unique<ibSessionScope>(session.get());
		cfg = std::make_unique<ibMetaDataConfigurationFile>();
		ibValueMetaObjectConfiguration* root = cfg->GetCommonMetaObject();
		ASSERT_NE(root, nullptr);
		ibValueMetaObject* created = cfg->CreateMetaObject(g_metaSessionParameterCLSID, root, /*runObject*/ false);
		ASSERT_NE(created, nullptr);
		created->SetName(wxT("CurrentUser"));
		parameters = std::make_unique<ibValueSessionParameters>(cfg.get());
		index = parameters->FindProp(wxT("CurrentUser"));
		ASSERT_GE(index, 0);
	}

	void TearDown() override {
		scope.reset();
		session.reset();
		parameters.reset();
		cfg.reset();
		if (ibApplicationInstance::Get() != nullptr)
			ibApplicationInstance::DestroyAppDataEnv();
	}
};

} // namespace

TEST_F(SessionParameterRead, ReadIsTheDeclaredValueNotTheHolder) {
	ASSERT_EQ(session.get(), ibSession::Current());

	ibValue read;
	ASSERT_TRUE(parameters->GetPropVal(index, read));

	// An unset string parameter is the type's own empty, not a holder and not Undefined.
	EXPECT_EQ(nullptr, dynamic_cast<ibValueSessionParameter*>(read.GetRef()));
	EXPECT_EQ(ibValueTypes::TYPE_STRING, read.GetType());
	EXPECT_TRUE(read.GetString().IsEmpty());
	EXPECT_NE(wxT("SessionParameterValue"), read.GetClassName());

	// SessionParameters["CurrentUser"] is the same door.
	ibValue indexed;
	ASSERT_TRUE(parameters->GetAt(ibValue(wxT("CurrentUser")), indexed));
	EXPECT_EQ(nullptr, dynamic_cast<ibValueSessionParameter*>(indexed.GetRef()));
	EXPECT_EQ(ibValueTypes::TYPE_STRING, indexed.GetType());

	// What lands in a collection is that value. The holder used to escape with it.
	ibValueArray list;
	list.Add(read);
	ibValue cell;
	ASSERT_TRUE(list.GetAt(ibValue(0), cell));
	EXPECT_EQ(nullptr, dynamic_cast<ibValueSessionParameter*>(cell.GetRef()));
	EXPECT_EQ(ibValueTypes::TYPE_STRING, cell.GetType());
}

TEST_F(SessionParameterRead, AWriteOutsideTheSessionModuleIsRefused) {
	ASSERT_EQ(session.get(), ibSession::Current());
	try {
		parameters->SetPropVal(index, ibValue(wxT("no")));
		FAIL() << "a write outside the session module was accepted";
	}
	catch (const ibBackendException& err) {
		EXPECT_NE(wxNOT_FOUND, wxString(err.GetErrorDescription()).Find(wxT("session module")));
	}
}
