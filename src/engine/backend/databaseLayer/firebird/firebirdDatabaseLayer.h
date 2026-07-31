#ifndef __FIREBIRD_DATABASE_LAYER_H__
#define __FIREBIRD_DATABASE_LAYER_H__

#include "backend/databaseLayer/databaseLayerDef.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/databaseLayer/firebird/engine/ibase.h"

#include <memory>   // std::shared_ptr — m_pInterface is ref-counted (shared with the maintenance scheduler)

#if _USE_DYNAMIC_DATABASE_LAYER_LINKING == 1
class ibInterfaceFirebird;
#endif

class BACKEND_API ibDatabaseLayerFirebird : public ibDatabaseLayer
{
	// FB 5 supports 4096 / 8192 / 16384 / 32768. 16384 is the FB 5
	// recommendation for OLTP — bigger pages mean fewer splits on
	// document/register tables (typical OES row footprint), and the
	// max-row limit (~ page/3) headroom stops the wider rows from
	// failing CREATE TABLE. Embedded cache footprint at 16K × 2048
	// pages = 32 MB is fine. int32_t holds 32768 cleanly even though
	// the DPB field is encoded as a 2-byte big-endian value.
	const int32_t m_pageSize = 16384;

public:
	// ctor()
	ibDatabaseLayerFirebird();
	ibDatabaseLayerFirebird(const wxString& strDatabase);
	ibDatabaseLayerFirebird(const wxString& strDatabase, const wxString& strUser, const wxString& strPassword);
	ibDatabaseLayerFirebird(const wxString& strServer, const wxString& strDatabase, const wxString& strUser, const wxString& strPassword);
	ibDatabaseLayerFirebird(const wxString& strServer, const wxString& strDatabase, const wxString& strUser, const wxString& strPassword, const wxString& strRole);

	ibDatabaseLayerFirebird(isc_db_handle pDatabase) { m_pDatabase = pDatabase; }

	ibDatabaseLayerFirebird(const ibDatabaseLayerFirebird& src);

	// dtor()
	virtual ~ibDatabaseLayerFirebird();

	// open database
	virtual bool Open();
	virtual bool Open(const wxString& strDatabase);
	virtual bool Open(const wxString& strDatabase, const wxString& strUser, const wxString& strPassword);
	virtual bool Open(const wxString& strServer, const wxString& strDatabase, const wxString& strUser = wxEmptyString, const wxString& strPassword = wxEmptyString);

	// close database  
	virtual bool Close();

	// Is the connection to the database open?
	virtual bool IsOpen();

	/// clone database  
	virtual ibDatabaseLayer* Clone() { return new ibDatabaseLayerFirebird(*this); }

	// IsActiveTransaction inherits the base-class default (m_txDepth > 0).
	// Driver transaction primitives (DoBeginTransaction / DoCommit /
	// DoRollBack) are protected — see below.

	static int TranslateErrorCode(int nCode);
	//static wxString TranslateErrorCodeToString(ibInterfaceFirebird* pInterface, int nCode, ISC_STATUS_ARRAY status);
	static wxString TranslateErrorCodeToString(ibInterfaceFirebird* pInterface, int nCode, void* status);
	static bool IsAvailable();

	// Map an isc_status[1] value (the FB "primary" gds code stashed
	// into m_nErrorCode by every error path in this driver) to a
	// portable Kind. FB doesn't expose SQLSTATE — the gds codes are
	// the authoritative identifier. Common ones:
	//   isc_lock_conflict / isc_deadlock              → Deadlock
	//   isc_lock_timeout                              → Timeout
	//   isc_network_error / isc_net_*                 → ConnectionLost
	//   isc_dsql_command_err / isc_dsql_token_unk_err → Syntax
	//   isc_unique_key_violation / isc_foreign_key    → Constraint
	ibBackendDatabaseException::Kind ClassifyDatabaseError(int nativeCode) const override;

	void SetServer(const wxString& strServer) { m_strServer = strServer; }
	void SetDatabase(const wxString& strDatabase) { m_strDatabase = strDatabase; }
	void SetUser(const wxString& strUser) { m_strUser = strUser; }
	void SetPassword(const wxString& strPassword) { m_strPassword = strPassword; }
	void SetRole(const wxString& strRole) { m_strRole = strRole; }

	// Database schema API contributed by M. Szeftel (author of wxActiveRecordGenerator)
	virtual bool TableExists(const wxString& table);
	virtual bool ViewExists(const wxString& view);
	virtual wxArrayString GetTables();
	virtual wxArrayString GetViews();
	virtual wxArrayString GetColumns(const wxString& table);

	virtual int GetDatabaseLayerType() const {
		return DATABASELAYER_FIREBIRD;
	}

	static const ibDialectDictionary& Dialect();                       // FB dialect (no instance needed)
	virtual const ibDialectDictionary& GetDialect() const override;    // polymorphic access for L2

	// Derived-state materialisation (register totals). Firebird is the DEFAULT embedded
	// database, so this is the dialect most installations will actually run, and it is also
	// the one that diverges most: an accumulating upsert must be MERGE (UPDATE OR INSERT ..
	// MATCHING can only replace), and there is no date_trunc — period truncation is built
	// from EXTRACT + DATEADD. Both differences are absorbed here, in data.
	// (docs/register-totals-strategy.md)
	static const ibMaterializationDialect& MaterializationDialect();
	virtual const ibMaterializationDialect* GetMaterializationDialect() const override;

	// FB-specific: route to `ReconnectIfLeaderChanged()` so callers
	// holding long-lived connections (session registry's heartbeat /
	// snapshot jobs) can recover after a leader handoff without
	// looping forever on the dead TCP socket to the old leader's
	// spawned firebird.exe.
	virtual bool ReconnectIfStale() override;


	// Row-lock dialect now lives in the dialect dictionary (m_rowLockSuffix=" WITH LOCK",
	// m_rowLockNoWaitSuffix=""); FB's NOWAIT rides the TPB (isc_tpb_nowait via ibTxOptions::noWait).

	// Housekeeping this connection can do on its own database: sweep, and the
	// periodic backup/restore cycle. Runs whatever is due and returns whether
	// anything did.
	//
	// A METHOD rather than a service with its own copy of the parameters — the
	// interface, the path and the credentials are already here, valid for as long
	// as this connection is checked out of the pool. The previous shape cached
	// them in a process-wide singleton and then had to prove the cache outlived
	// nothing it shouldn't; a connection borrowed for the call proves that by
	// construction.
	//
	// Called by the `firebird.maintenance` job on its own session's connection,
	// so the blocking Services API calls are on a worker rather than on any
	// thread someone is waiting on.
	bool RunDueMaintenance();

protected:

	// query database
	virtual int DoRunQuery(const wxString& strQuery, bool bParseQuery);
	virtual ibDatabaseResultSet* DoRunQueryWithResults(const wxString& strQuery);

	// ibPreparedStatement support
	virtual ibPreparedStatement* DoPrepareStatement(const wxString& strQuery);

	// Driver-level transaction primitives. The base-class nesting
	// counter (m_txDepth) guarantees these fire only on outermost
	// 0↔1 transitions, so we hold a single isc_tr_handle — no stack
	// of transactions is required. (FB's native API does support
	// concurrent TXs per connection, but nothing in OES needs that.)
	virtual void DoBeginTransaction(const ibTxOptions& opts) override;
	virtual void DoCommit() override;
	virtual void DoRollBack() override;

private:

	void InterpretErrorCodes();

	// Reconnect-on-leader-handoff. When `firebirdLeaderMode` reports a
	// connect URL different from `m_currentConnectUrl` (leader died /
	// handed off / we self-promoted), close the existing FB handle
	// (best-effort) and re-attach against the new URL. Returns true
	// if a reattach happened (so caller knows the previous TX / cursor
	// state is gone), false on no-op (URL still matches or remote mode).
	//
	// Called from `DoBeginTransaction` — single point where we know
	// there's no in-flight TX / prepared statement / open cursor to
	// surprise. Mid-TX connection loss continues to surface as a
	// regular exception; the *next* BeginTransaction picks up the
	// reconnect.
	bool ReconnectIfLeaderChanged();

	// URL that the current `isc_db_handle` was attached with. Set on
	// successful `Open`; compared against `ibFirebirdLeaderMode::
	// CurrentConnectUrl()` on the reconnect path. Empty when no
	// connection / standalone mode (no leader-mode involvement).
	wxString m_currentConnectUrl;

#if _USE_DYNAMIC_DATABASE_LAYER_LINKING == 1
	// Ref-counted: the maintenance scheduler (a process singleton) borrows this
	// interface and may outlive THIS pooled connection. Shared ownership keeps
	// the fbclient function table alive until BOTH the driver and the scheduler
	// release it, so a reaped donor connection cannot dangle the worker's pointer.
	std::shared_ptr<ibInterfaceFirebird> m_pInterface;
#endif

	wxString m_strServer;
	wxString m_strDatabase;
	wxString m_strUser;
	wxString m_strPassword;
	wxString m_strRole;

	// Single active transaction handle (0 when no TX is open). The
	// base class collapses nested Begin/Commit calls onto this slot,
	// so there's no need for a stack.
	isc_tr_handle m_pTransaction = 0;

	isc_db_handle m_pDatabase;
	void *m_pStatus;
};

#endif // __FIREBIRD_DATABASE_LAYER_H__

