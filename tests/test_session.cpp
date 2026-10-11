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

TEST(SessionParameters, AWriteIsKeptWhenTheSessionModuleIsNotRunning) {
    // The write used to raise unless the session module was on the stack.
    // Server code updates an error stack and a flag around a write, so a
    // closed window must still keep the value, and a second write replaces it.
    ibSession sess(wxT("params"), ibSessionKind::Designer);
    EXPECT_TRUE(sess.GetSessionParameter(wxT("ErrorStack")).IsEmpty());
    sess.SetSessionParameter(wxT("ErrorStack"), ibValue(wxT("one")));
    EXPECT_EQ(sess.GetSessionParameter(wxT("ErrorStack")).GetString(), wxT("one"));
    sess.SetSessionParameter(wxT("ErrorStack"), ibValue(wxT("two")));
    EXPECT_EQ(sess.GetSessionParameter(wxT("ErrorStack")).GetString(), wxT("two"));
}

TEST(SessionDbLayer, ThrowsWhenNoCurrentSession) {
    // No SessionScope active on this thread → ibSession::Current() is
    // null → DatabaseLayer() throws an explicit error rather than
    // silently returning the wrong conn.
    EXPECT_THROW(
        ibSession::DatabaseLayer(),
        ibBackendException);
}
