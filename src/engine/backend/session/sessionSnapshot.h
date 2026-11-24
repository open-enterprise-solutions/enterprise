#ifndef __IB_SESSION_SNAPSHOT_H__
#define __IB_SESSION_SNAPSHOT_H__

// ibSessionSnapshot — point-in-time copy of every sys_session row in
// the cluster, refreshed by ibSessionRegistry's JobRefreshSnapshot.
// Read by the designer's Active Users dialog and by the registry
// itself (cross-process single-user-mode gate, sole-live check).
//
// Was named ibApplicationDataSessionArray while it lived in appData.h;
// the type is session-domain, not application-domain — sole writer is
// the registry thread, sole reader contract is "snapshot, not live".

#include "backend/backend.h"
#include "backend/backend_core.h"
#include "backend/appData.h"   // ibRunMode (plain enum, can't forward-declare cleanly)

#include <unordered_map>
#include <vector>
#include <string>

// Forward-declared so the header stays independent of session.h —
// frontend / admin-UI code reads the snapshot without pulling the full
// session-registry API. Underlying type pinned to `int` to match the
// enum's definition in session.h and the m_kind field below.
enum class ibSessionKind : int;

class BACKEND_API ibSessionSnapshot {

	struct Entry {

		Entry(ibRunMode runMode, int kind, const ibDateTime& startedDateTime,
			const ibString& strUserName, const ibString& strComputerName, const ibString& strSession,
			bool singleUser, const ibString& activity)
			: m_runMode(runMode), m_kind(kind), m_startedDate(startedDateTime),
			  m_strUserName(strUserName), m_strComputerName(strComputerName), m_strSession(strSession),
			  m_activity(activity), m_singleUser(singleUser)
		{
		}

		ibRunMode  m_runMode;
		// session-layer role, numeric ibSessionKind. Stored as int so the
		// header stays independent of session.h (snapshot is read by
		// frontend code that doesn't otherwise need the registry types).
		int        m_kind;
		ibDateTime m_startedDate;
		ibString   m_strUserName;
		ibString   m_strComputerName;
		ibString   m_strSession;
		// sys_session.currentActivity, or empty. A dropped thin client says "detached until HH:MM:SS"
		// for the resume window.
		ibString   m_activity;
		// Process-wide single-user (monopoly) flag — true when this row
		// holds it. Filled in by JobRefreshSnapshot from sys_session.singleUser.
		bool       m_singleUser = false;
	};

public:

	ibSessionSnapshot() : m_sessionArrayHash(ibNewUniqueGuid) {}

	// A row of sys_session, whole — the registry reads every column in one SELECT.
	void AppendSession(ibRunMode runMode, int kind, const ibDateTime& startedTime,
		const ibString& strUserName, const ibString& strComputerName, const ibString& strSession,
		bool singleUser, const ibString& activity) {
		m_listSession.emplace_back(runMode, kind, startedTime, strUserName, strComputerName, strSession,
			singleUser, activity);
	}

	const ibGuid& GetSessionArrayHash() const { return m_sessionArrayHash; }

	ibString GetUserName    (unsigned int idx) const;
	ibString GetComputerName(unsigned int idx) const;
	ibString GetSession     (unsigned int idx) const;
	ibString GetStartedDate (unsigned int idx) const;
	ibString GetProgram     (unsigned int idx) const;
	// sys_session.currentActivity, or empty. A detached thin client reads "detached until …".
	ibString GetActivity    (unsigned int idx) const;

	void ClearSession() {
		m_sessionArrayHash = ibNewUniqueGuid;
		m_listSession.clear();
	}

	ibRunMode GetSessionApplication(unsigned int idx) const;
	int       GetSessionKind       (unsigned int idx) const;
	// Short label derived from the kind:
	//   Service / WebServer → "Server"
	//   ThinClient / ThinDesigner → "Thin client"
	//   a job → "Job"
	//   anything else → "Client"
	// Used by designer's Active Users dialog.
	ibString  GetSessionKindDescr  (unsigned int idx) const;
	unsigned int GetSessionCount() const { return static_cast<unsigned int>(m_listSession.size()); }

	// Per-row single-user flag accessor — returns the holder session (the
	// only one with m_singleUser==true), if any. Used by ProcessAdd's
	// cross-process single-user gate and by ProcessSetSingleUser's
	// sole-live check.
	bool     IsSingleUser(unsigned int idx) const;
	ibString SingleUserHolderSession() const;
	ibString SingleUserHolderUser   () const;

	// Cluster-wide aggregate queries. Predicate-style helpers built on
	// top of the snapshot for callers that don't want to iterate
	// themselves.
	//
	// HasActiveUsers — true if any row carries a non-empty userName.
	//   Distinguishes "people are logged in" from "the wes process has
	//   its technical row" (technical rows have empty userName).
	// IsUserActive   — true if the named user is logged in anywhere in
	//   the cluster (any process owning a row with this userName).
	//   Case-sensitive match — usernames in sys_user are stored
	//   verbatim.
	// CountByKind    — how many rows carry the given session kind
	//   (Designer / WebClient / Enterprise / ...). Useful for admin
	//   gauges and for predicates like "any designer attached?".
	bool         HasActiveUsers() const;
	bool         IsUserActive(const ibString& userName) const;
	unsigned int CountByKind(ibSessionKind kind) const;

private:
	ibGuid             m_sessionArrayHash;
	std::vector<Entry> m_listSession;
};

// Is a person working in a session of this kind — not a job, which carries the user name of whoever started it
// (the reason is at its definition). `kind` is the row's numeric ibSessionKind (GetSessionKind).
BACKEND_API bool ibIsPersonSession(int kind);

#endif
