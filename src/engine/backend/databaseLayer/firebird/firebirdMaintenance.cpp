#include <atomic>   // std::atomic — MSVC supplied this transitively
#include "firebirdMaintenance.h"
#include "firebirdInterface.h"

#include <wx/filename.h>
#include <wx/log.h>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

namespace {

// Resolve credentials with a log-warning fallback to SYSDBA/masterkey
// when caller passed empty fields. Default creds are the FB OOTB
// secret — fine for embedded dev/test, but a hostile network
// neighbour can attach to service_mgr the moment the spawned
// firebird.exe accepts external connections. Caller should set
// real creds in production.
void ResolveCredentials(const ibFirebirdMaintenance::ServiceConnection& conn,
                        wxString& user, wxString& pwd) {
	if (conn.username.IsEmpty() || conn.password.IsEmpty()) {
		wxLogWarning(wxT("ibFirebirdMaintenance: empty credentials, falling ")
		             wxT("back to SYSDBA/masterkey - set real creds for ")
		             wxT("production deploys"));
	}
	user = conn.username.IsEmpty() ? wxT("SYSDBA")    : conn.username;
	pwd  = conn.password.IsEmpty() ? wxT("masterkey") : conn.password;
}

// Build the SPB (Service Parameter Block) for service_mgr attach.
// Format: version + isc_spb_user_name(N + bytes) + isc_spb_password(N + bytes).
std::string BuildAttachSpb(const wxString& user, const wxString& password) {
	std::string spb;
	spb.push_back(isc_spb_version);
	spb.push_back(isc_spb_current_version);

	const auto userBytes = user.utf8_str();
	const size_t userLen = std::min<size_t>(strlen(userBytes.data()), 255);
	spb.push_back(isc_spb_user_name);
	spb.push_back((char)userLen);
	spb.append(userBytes.data(), userLen);

	const auto pwdBytes = password.utf8_str();
	const size_t pwdLen = std::min<size_t>(strlen(pwdBytes.data()), 255);
	spb.push_back(isc_spb_password);
	spb.push_back((char)pwdLen);
	spb.append(pwdBytes.data(), pwdLen);

	return spb;
}

// Append a length-prefixed string field to an action SPB.
//
// Wire format for service-action string params (sweep / backup /
// restore): one tag byte + 2-byte little-endian length + bytes.
void AppendSpbString(std::string& spb, char tag, const wxString& value) {
	const auto bytes = value.utf8_str();
	const size_t n = std::min<size_t>(strlen(bytes.data()), 65535);
	spb.push_back(tag);
	spb.push_back((char)(n & 0xFF));
	spb.push_back((char)((n >> 8) & 0xFF));
	spb.append(bytes.data(), n);
}

// Append a 4-byte little-endian integer field.
void AppendSpbInt32(std::string& spb, char tag, int32_t value) {
	spb.push_back(tag);
	spb.push_back((char)(value         & 0xFF));
	spb.push_back((char)((value >> 8 ) & 0xFF));
	spb.push_back((char)((value >> 16) & 0xFF));
	spb.push_back((char)((value >> 24) & 0xFF));
}

// Wait for the service operation to complete by polling
// `isc_info_svc_running`. Returns true on clean completion, false
// on timeout, query failure, or external cancel.
//
// Response buffer is sized large enough that `isc_info_truncated`
// is rare even for verbose gbak output. On truncation we re-issue
// the query: leaving the stdout chunks un-drained stalls the
// service thread once its internal write buffer fills (long gbak
// runs hit this within a few minutes).
//
// `cancelToken` — when not nullptr and *cancelToken == true the
// loop bails out fast (returns false). The caller treats this as
// the same exit path as timeout — service handle is detached,
// status is Timeout. The token must remain alive for the whole
// call; the scheduler owns the atomic and outlives the worker.
bool WaitForServiceCompletion(ibInterfaceFirebird* iface,
                              isc_svc_handle& svc,
                              ISC_STATUS_ARRAY& status,
                              int timeoutSeconds,
                              const std::atomic<bool>* cancelToken)
{
	using namespace std::chrono;
	const auto deadline = steady_clock::now() + seconds(timeoutSeconds);
	auto canceled = [cancelToken]() {
		return cancelToken != nullptr
		    && cancelToken->load(std::memory_order_acquire);
	};

	// We query both `isc_info_svc_running` (still going?) and
	// `isc_info_svc_line` (drain stdout-equivalent) so the service
	// thread doesn't stall on an unread output buffer.
	const char req[] = {
		(char)isc_info_svc_line,
		(char)isc_info_svc_running
	};
	// 256 KB — large enough for ordinary gbak verbose output between
	// 200 ms poll ticks. Truncation handled by the retry loop below.
	std::vector<char> resp(256 * 1024);

	while (steady_clock::now() < deadline) {
		if (canceled()) return false;

		bool truncated = true;
		bool sawRunningTag = false;
		bool stillRunning = false;

		// Inner retry loop — drain all currently-buffered output by
		// re-issuing the query until the response fits without
		// truncation. Bounded by an iteration cap so a server stuck
		// in pathological output-flood doesn't spin us forever.
		// Cancel check first inside drain too — gbak can spew tens
		// of KB per tick and the drain loop would otherwise sit
		// there for a non-trivial slice ignoring shutdown.
		for (int drain = 0; drain < 16 && truncated; ++drain) {
			if (canceled()) return false;
			const ISC_STATUS rc = iface->GetIscServiceQuery()(
				status, &svc, nullptr,
				0, nullptr,
				sizeof(req), req,
				(unsigned short)resp.size(), resp.data());
			if (rc != 0)
				return false;

			truncated = false;

			// Parse the response — token-stream of (tag, len, payload).
			// We're looking for `isc_info_svc_running` (0x3D) followed
			// by 4-byte int 0 = done, 1 = still running.
			size_t i = 0;
			while (i < resp.size()) {
				const unsigned char tag = (unsigned char)resp[i++];
				if (tag == (unsigned char)isc_info_end)
					break;
				if (tag == (unsigned char)isc_info_svc_running) {
					if (i + 4 > resp.size()) break;
					const uint32_t v =
						  ((uint32_t)(unsigned char)resp[i  ])
						| ((uint32_t)(unsigned char)resp[i+1] << 8)
						| ((uint32_t)(unsigned char)resp[i+2] << 16)
						| ((uint32_t)(unsigned char)resp[i+3] << 24);
					stillRunning = (v != 0);
					sawRunningTag = true;
					i += 4;
				} else if (tag == (unsigned char)isc_info_svc_line ||
				           tag == (unsigned char)isc_info_svc_to_eof) {
					if (i + 2 > resp.size()) break;
					const uint16_t len =
						  ((uint16_t)(unsigned char)resp[i  ])
						| ((uint16_t)(unsigned char)resp[i+1] << 8);
					i += 2 + len;
				} else if (tag == (unsigned char)isc_info_truncated) {
					// Response didn't fit — buffered output remains
					// on the service side; re-issue the query to
					// drain it. Without this the service thread
					// blocks on write once its internal buffer fills.
					truncated = true;
					break;
				} else {
					// Skip a 2-byte length + payload for unknown tags.
					if (i + 2 > resp.size()) break;
					const uint16_t len =
						  ((uint16_t)(unsigned char)resp[i  ])
						| ((uint16_t)(unsigned char)resp[i+1] << 8);
					i += 2 + len;
				}
			}
		}

		if (sawRunningTag && !stillRunning)
			return true;
		std::this_thread::sleep_for(milliseconds(200));
	}
	return false;
}

// Best-effort removal of a maintenance temp file (.tmp.fbk / .tmp.new.fdb / .bak). After
// isc_service_detach the embedded Firebird engine can still hold the backup file for a moment — it
// closes its OS handle asynchronously — so an immediate delete races and fails with a sharing
// violation (Win32 error 32). That is harmless: a leftover temp is reclaimed by the next cycle's
// pre-clean. Retry a few times, then give up SILENTLY — wxLogNull suppresses wxRemoveFile's
// wxLogSysError so a benign shutdown race never pops a modal "couldn't be removed" dialog.
void TryRemoveTempFile(const wxString& path) {
	if (!wxFileExists(path)) return;
	wxLogNull noLog;
	for (int attempt = 0; attempt < 5; ++attempt) {
		if (wxRemoveFile(path) || !wxFileExists(path))
			return;
		std::this_thread::sleep_for(std::chrono::milliseconds(40));
	}
	// Still locked — leave it; the next RunBackupRestoreCycle pre-clean (or a manual sweep) gets it.
}

} // namespace

ibFirebirdMaintenance::Status ibFirebirdMaintenance::RunSweep(
	ibInterfaceFirebird* iface,
	const wxString& databasePath,
	const ServiceConnection& conn,
	const std::atomic<bool>* cancelToken)
{
	if (iface == nullptr
	 || iface->GetIscServiceAttach() == nullptr
	 || iface->GetIscServiceStart()  == nullptr
	 || iface->GetIscServiceQuery()  == nullptr
	 || iface->GetIscServiceDetach() == nullptr)
		return Status::ServicesApiUnavailable;

	wxString user, pwd;
	ResolveCredentials(conn, user, pwd);

	// Service manager attach target: `service_mgr` for local,
	// `host:service_mgr` for remote. Embedded path uses the bare
	// form.
	wxString svcTarget = wxT("service_mgr");
	if (!conn.server.IsEmpty())
		svcTarget = conn.server + wxT(":service_mgr");

	const auto svcUtf8 = svcTarget.utf8_str();
	const size_t svcLen = strlen(svcUtf8.data());

	const std::string attachSpb = BuildAttachSpb(user, pwd);

	ISC_STATUS_ARRAY status{};
	isc_svc_handle svc = 0;
	if (iface->GetIscServiceAttach()(
			status,
			(unsigned short)svcLen,
			svcUtf8.data(),
			&svc,
			(unsigned short)attachSpb.size(),
			attachSpb.c_str()) != 0)
		return Status::AttachFailed;

	// Action: isc_action_svc_repair + isc_spb_options bit
	// `isc_spb_rpr_sweep_db`. This is the official "sweep now"
	// request via Services API.
	std::string actionSpb;
	actionSpb.push_back((char)isc_action_svc_repair);
	AppendSpbString(actionSpb, isc_spb_dbname, databasePath);
	AppendSpbInt32(actionSpb, isc_spb_options, isc_spb_rpr_sweep_db);

	if (iface->GetIscServiceStart()(
			status, &svc, nullptr,
			(unsigned short)actionSpb.size(),
			actionSpb.c_str()) != 0) {
		iface->GetIscServiceDetach()(status, &svc);
		return Status::StartFailed;
	}

	// Sweep on a multi-GB DB can take several minutes; give it 30.
	const bool ok = WaitForServiceCompletion(
		iface, svc, status, /*timeoutSeconds=*/1800, cancelToken);

	iface->GetIscServiceDetach()(status, &svc);
	return ok ? Status::Ok : Status::Timeout;
}

ibFirebirdMaintenance::Status ibFirebirdMaintenance::RunBackupRestoreCycle(
	ibInterfaceFirebird* iface,
	const wxString& databasePath,
	const ServiceConnection& conn,
	const std::atomic<bool>* cancelToken)
{
	if (iface == nullptr
	 || iface->GetIscServiceAttach() == nullptr
	 || iface->GetIscServiceStart()  == nullptr
	 || iface->GetIscServiceQuery()  == nullptr
	 || iface->GetIscServiceDetach() == nullptr)
		return Status::ServicesApiUnavailable;

	wxString user, pwd;
	ResolveCredentials(conn, user, pwd);
	wxString svcTarget = wxT("service_mgr");
	if (!conn.server.IsEmpty())
		svcTarget = conn.server + wxT(":service_mgr");

	wxFileName dbFn(databasePath);
	const wxString backupPath  = dbFn.GetFullPath() + wxT(".tmp.fbk");
	const wxString restorePath = dbFn.GetFullPath() + wxT(".tmp.new.fdb");
	const wxString rollbackPath = dbFn.GetFullPath() + wxT(".bak");

	// Clean up any leftovers from a prior aborted cycle.
	TryRemoveTempFile(backupPath);
	TryRemoveTempFile(restorePath);

	const auto svcUtf8 = svcTarget.utf8_str();
	const size_t svcLen = strlen(svcUtf8.data());
	const std::string attachSpb = BuildAttachSpb(user, pwd);

	// === BACKUP ===
	{
		ISC_STATUS_ARRAY status{};
		isc_svc_handle svc = 0;
		if (iface->GetIscServiceAttach()(
				status,
				(unsigned short)svcLen, svcUtf8.data(),
				&svc,
				(unsigned short)attachSpb.size(), attachSpb.c_str()) != 0)
			return Status::AttachFailed;

		std::string spb;
		spb.push_back((char)isc_action_svc_backup);
		AppendSpbString(spb, isc_spb_dbname,    databasePath);
		AppendSpbString(spb, isc_spb_bkp_file,  backupPath);
		// isc_spb_options = 0 — no special flags. (isc_spb_bkp_ignore_checksums
		// could be set for slightly faster backup, but we want the
		// integrity check so a corrupt DB doesn't silently survive
		// the BR cycle with damaged pages re-encoded.)
		AppendSpbInt32(spb, isc_spb_options, 0);

		if (iface->GetIscServiceStart()(
				status, &svc, nullptr,
				(unsigned short)spb.size(), spb.c_str()) != 0) {
			iface->GetIscServiceDetach()(status, &svc);
			return Status::StartFailed;
		}

		// Backup on a 100 GB DB: ~5-15 minutes on modern SSD. Give
		// it 30.
		const bool ok = WaitForServiceCompletion(
			iface, svc, status, 1800, cancelToken);
		iface->GetIscServiceDetach()(status, &svc);

		if (!ok) {
			TryRemoveTempFile(backupPath);
			return Status::Timeout;
		}
	}

	// === RESTORE ===
	{
		ISC_STATUS_ARRAY status{};
		isc_svc_handle svc = 0;
		if (iface->GetIscServiceAttach()(
				status,
				(unsigned short)svcLen, svcUtf8.data(),
				&svc,
				(unsigned short)attachSpb.size(), attachSpb.c_str()) != 0) {
			TryRemoveTempFile(backupPath);
			return Status::AttachFailed;
		}

		std::string spb;
		spb.push_back((char)isc_action_svc_restore);
		AppendSpbString(spb, isc_spb_bkp_file,  backupPath);
		AppendSpbString(spb, isc_spb_dbname,    restorePath);
		// isc_spb_options:
		//   isc_spb_res_create — fail if target file exists. We pre-
		//                        deleted restorePath above so this
		//                        path is clean; no REPLACE needed.
		AppendSpbInt32(spb, isc_spb_options, isc_spb_res_create);

		if (iface->GetIscServiceStart()(
				status, &svc, nullptr,
				(unsigned short)spb.size(), spb.c_str()) != 0) {
			iface->GetIscServiceDetach()(status, &svc);
			TryRemoveTempFile(backupPath);
			return Status::StartFailed;
		}

		const bool ok = WaitForServiceCompletion(
			iface, svc, status, 1800, cancelToken);
		iface->GetIscServiceDetach()(status, &svc);

		if (!ok) {
			TryRemoveTempFile(backupPath);
			TryRemoveTempFile(restorePath);
			return Status::RestoreFailed;
		}
	}

	// === SWAP ===
	// Rename original → .bak, restored → original. Atomic on a
	// single filesystem; cross-volume falls back to copy + delete
	// which is not crash-safe — caller responsibility to keep the
	// .fdb on a local disk for this operation.
	TryRemoveTempFile(rollbackPath);
	if (!wxRenameFile(databasePath, rollbackPath)) {
		TryRemoveTempFile(backupPath);
		TryRemoveTempFile(restorePath);
		return Status::IOError;
	}
	if (!wxRenameFile(restorePath, databasePath)) {
		// Rollback: original is still under .bak. Restore it.
		wxRenameFile(rollbackPath, databasePath);
		TryRemoveTempFile(backupPath);
		TryRemoveTempFile(restorePath);
		return Status::IOError;
	}

	// Success — drop the .bak and the intermediate .fbk.
	TryRemoveTempFile(rollbackPath);
	TryRemoveTempFile(backupPath);
	return Status::Ok;
}

wxString ibFirebirdMaintenance::StatusToString(Status s) {
	switch (s) {
		case Status::Ok:                     return wxT("Ok");
		case Status::ServicesApiUnavailable: return wxT("Services API unavailable in fbclient");
		case Status::AttachFailed:           return wxT("service_mgr attach failed (auth?)");
		case Status::StartFailed:            return wxT("service_start failed (bad SPB?)");
		case Status::QueryFailed:            return wxT("service_query mid-operation failed");
		case Status::Timeout:                return wxT("operation didn't complete in time budget");
		case Status::RestoreFailed:          return wxT("restore step failed - original file kept under .bak");
		case Status::IOError:                return wxT("local file rename/delete failed");
	}
	return wxT("(unknown)");
}
