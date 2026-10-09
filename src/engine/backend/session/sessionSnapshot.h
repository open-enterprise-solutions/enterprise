#ifndef __IB_SESSION_SNAPSHOT_H__
#define __IB_SESSION_SNAPSHOT_H__

// ibSessionSnapshot — point-in-time copy of every sys_session row in
// the cluster, refreshed by ibSessionRegistry's JobRefreshSnapshot.
// Read by the designer's Active Users dialog and by the registry
// itself (cross-process exclusive-mode gate, sole-live check).
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

#include <wx/string.h>

// Forward-declared so the header stays independent of session.h —
// frontend / admin-UI code reads the snapshot without pulling the full
// session-registry API. Underlying type pinned to `int` to match the
// enum's definition in session.h and the m_kind field below.
enum class ibSessionKind : int;

class BACKEND_API ibSessionSnapshot {

	struct Entry {

		Entry(ibRunMode runMode, int kind, const ibDateTime& startedDateTime,
			const wxString& strUserName, const wxString& strComputerName, const wxString& strSession)
			: m_runMode(runMode), m_kind(kind), m_startedDate(startedDateTime),
			  m_strUserName(strUserName), m_strComputerName(strComputerName), m_strSession(strSession)
		{
		}

		ibRunMode  m_runMode;
		// session-layer role, numeric ibSessionKind. Stored as int so the
		// header stays independent of session.h (snapshot is read by
		// frontend code that doesn't otherwise need the registry types).
		int        m_kind;
		ibDateTime m_startedDate;
		wxString   m_strUserName;
		wxString   m_strComputerName;
		wxString   m_strSession;
		// Process-wide exclusive (monopoly) flag — true when this row
		// holds it. Filled in by JobRefreshSnapshot from sys_session.exclusive.
		bool       m_exclusive = false;
	};

public:

	ibSessionSnapshot() : m_sessionArrayHash(wxNewUniqueGuid) {}

	void AppendSession(ibRunMode runMode, int kind, const ibDateTime& startedTime,
		const wxString& strUserName, const wxString& strComputerName, const wxString& strSession) {
		m_listSession.emplace_back(runMode, kind, startedTime, strUserName, strComputerName, strSession);
	}

	const ibGuid& GetSessionArrayHash() const { return m_sessionArrayHash; }

	wxString GetUserName    (unsigned int idx) const;
	wxString GetComputerName(unsigned int idx) const;
	wxString GetSession     (unsigned int idx) const;
	wxString GetStartedDate (unsigned int idx) const;
	wxString GetApplication (unsigned int idx) const;

	void ClearSession() {
		m_sessionArrayHash = wxNewUniqueGuid;
		m_listSession.clear();
	}

	ibRunMode GetSessionApplication(unsigned int idx) const;
	int       GetSessionKind       (unsigned int idx) const;
	// Short label derived from the kind:
	//   Service / WebServer → "Server"
	//   ThinClient / ThinDesigner → "Thin client"
	//   a job → "Job"
	//   anything else, legacy 0 included → "Client"
	// Used by designer's Active Users dialog.
	wxString  GetSessionKindDescr  (unsigned int idx) const;
	unsigned int GetSessionCount() const { return static_cast<unsigned int>(m_listSession.size()); }

	// Back-fill kinds after rows were appended — used by the registry
	// snapshot builder to tolerate legacy schemas where the `kind`
	// column lives in a second optional SELECT.
	void SetKindsFromMap    (const std::unordered_map<wxString, int>&  kindBySession);
	// Back-fill exclusive flags from a second optional SELECT — same
	// pattern as kinds, tolerant of pre-migration schemas where the
	// column doesn't exist yet.
	void SetExclusiveFromMap(const std::unordered_map<wxString, bool>& exclusiveBySession);

	// Per-row exclusive flag accessor — returns the holder session (the
	// only one with m_exclusive==true), if any. Used by ProcessAdd's
	// cross-process exclusive gate and by ProcessSetExclusive's
	// sole-live check.
	bool     IsExclusive(unsigned int idx) const;
	wxString ExclusiveHolderSession() const;
	wxString ExclusiveHolderUser   () const;

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
	bool         IsUserActive(const wxString& userName) const;
	unsigned int CountByKind(ibSessionKind kind) const;

private:
	ibGuid             m_sessionArrayHash;
	std::vector<Entry> m_listSession;
};

#endif
