// =============================================================================
// OES Enterprise — exceptions: one hierarchy, not two
//
// The engine's exceptions used to derive from nothing, in parallel with the
// standard hierarchy every generic boundary catches. A thread body, an event
// handler or a library callback writes `catch (const std::exception&)`, so a
// project exception landed in `catch (...)` — where there is no object to ask,
// and the sentence it carried was lost at the exact moment it was needed. That
// cost a terminated process reporting "unknown exception" while holding
// "Object 'ConstantObject.Attribute3' is not exist".
//
// These tests pin the contract that fixed it: ibBackendException IS a
// std::exception, what() says the same thing GetErrorDescription() does, and the
// varieties stay separately catchable — because a caller's whole reason to catch
// one and not another is that they mean different things (docs/exceptions.md).
// =============================================================================

#include <gtest/gtest.h>

#include <exception>
#include <type_traits>

#include "backend/backend_exception.h"

// --- the structural claim, checked at compile time ----------------------------
TEST(BackendException, IsAStdException) {
	static_assert(std::is_base_of<std::exception, ibBackendException>::value,
		"a generic boundary catches std::exception — a project exception that is not one is invisible there");
	static_assert(std::is_base_of<ibBackendException, ibBackendCoreException>::value, "");
	static_assert(std::is_base_of<ibBackendException, ibBackendInterruptException>::value, "");
	static_assert(std::is_base_of<ibBackendException, ibBackendAccessException>::value, "");
	SUCCEED();
}

// what() must never throw — it is called while an exception is in flight, and by
// std::terminate handlers when things are already going badly.
TEST(BackendException, WhatIsNoexcept) {
	static_assert(noexcept(std::declval<const ibBackendException&>().what()),
		"what() runs on paths where a throw would terminate the process");
	SUCCEED();
}

// --- the text survives the trip through the standard base ---------------------
TEST(BackendException, WhatCarriesTheDescription) {
	try {
		ibBackendCoreException::Error(_("Object '%s' is not exist"), wxT("ConstantObject.Rate"));
		FAIL() << "Error() must throw";
	}
	catch (const ibBackendException& err) {
		const wxString description = err.GetErrorDescription();
		EXPECT_TRUE(description.Contains(wxT("ConstantObject.Rate")));

		// The same sentence, through the standard door. This is what a generic
		// handler prints, so it must not be empty and must not be a different text.
		EXPECT_STREQ(err.what(), description.utf8_str().data());
	}
}

// --- caught as std::exception: the case that used to be a black hole ----------
TEST(BackendException, IsCaughtByAGenericHandler) {
	bool caughtAsStd = false;
	wxString whatText;

	try {
		ibBackendCoreException::Error(wxT("registry thread failed"));
	}
	catch (const std::exception& e) {          // no knowledge of the project's types
		caughtAsStd = true;
		whatText = wxString::FromUTF8(e.what());
	}
	catch (...) {
		FAIL() << "fell through to catch(...) — the description would be unreachable";
	}

	EXPECT_TRUE(caughtAsStd);
	EXPECT_TRUE(whatText.Contains(wxT("registry thread failed")));
}

// --- the varieties stay separately catchable ---------------------------------
// A handler catches the ONE failure it knows how to answer and lets the rest
// travel; that is what the subclasses are for.
TEST(BackendException, VarietiesAreCaughtByTheirOwnType) {
	try {
		ibBackendInterruptException::Error();
		FAIL() << "Error() must throw";
	}
	catch (const ibBackendInterruptException&) {
		SUCCEED();   // the user stopping the program is NOT an error — see docs/exceptions.md
	}
	catch (const ibBackendException&) {
		FAIL() << "an interrupt must be distinguishable from a failure";
	}

	try {
		ibBackendAccessException::Error(wxT("writing register 'Sales'"));
		FAIL() << "Error() must throw";
	}
	catch (const ibBackendAccessException& err) {
		EXPECT_TRUE(err.GetErrorDescription().Contains(wxT("Sales")));
	}
	catch (const ibBackendException&) {
		FAIL() << "a refusal by rights must be distinguishable from a malfunction";
	}
}

// --- ordering: derived before base, and the standard base LAST ----------------
// Now that the project base derives from std::exception, a handler that lists
// std::exception first would swallow everything. This test states the required
// order by exercising it.
TEST(BackendException, DerivedHandlerWinsOverTheStandardBase) {
	enum class Landed { None, Project, Std } landed = Landed::None;

	try {
		ibBackendCoreException::Error(wxT("boom"));
	}
	catch (const ibBackendException&) { landed = Landed::Project; }
	catch (const std::exception&)     { landed = Landed::Std; }

	EXPECT_EQ(landed, Landed::Project);
}

// --- a rethrow keeps the same object -----------------------------------------
// `throw;` propagates the in-flight exception, which is how ProcessError avoids
// formatting one failure twice (m_errorHandled rides along).
TEST(BackendException, BareRethrowPreservesTheDescription) {
	wxString seen;
	try {
		try {
			ibBackendCoreException::Error(wxT("inner failure"));
		}
		catch (const ibBackendException&) {
			throw;                       // same object, not a copy of a new one
		}
	}
	catch (const ibBackendException& err) {
		seen = err.GetErrorDescription();
	}

	EXPECT_TRUE(seen.Contains(wxT("inner failure")));
}
