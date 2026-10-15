#include "activeUser.h"

#include "backend/appData.h"
#include "core/picturePredefined.h"
#include "backend/lock/lockManager.h"
#include "backend/metadataConfiguration.h"
#include "core/diagnostics/journal.h"   // ibJournalWarning — a command that failed, as the desktop's dialog had it
#include "core/serialize/dataBuilder.h"
#include "backend/session/session.h"
#include "backend/session/sessionRegistry.h"
#include "backend/session/sessionSnapshot.h"
#include "frmserver/client/clientFrame.h"   // ibClientFrame::SendPicture — the dialog's pictures, by their ids

bool ibSchemaActiveUser::AccessRight(const ibSession& session) const
{
	const ibMetaDataConfigurationBase* const metaData = session.GetMetaData();
	return metaData != nullptr && metaData->AccessRight_ActiveUsers();
}

void ibSchemaActiveUser::Build(const ibSession& session, const ibDataNode& WXUNUSED(args), ibDataNode& result) const
{
	const ibApplicationInstance* const applicationInstance = session.GetApplicationInstance();

	// The dialog's pictures, as the desktop's drew them — the window's, and the one beside each user.
	result.SetValue(wxT("Icon"), ibClientFrame::SendPicture(g_picUserActiveCLSID));
	result.SetValue(wxT("Picture"), ibClientFrame::SendPicture(g_picUserCLSID));

	// The sessions — every process's in the base, not this server's alone.
	ibDataNode& sessions = result.Child(wxT("Sessions"));
	if (ibSessionRegistry* const registry = ibApplicationInstance::GetSessionRegistry(applicationInstance)) {
		const ibSessionSnapshot snapshot = registry->GetClusterSnapshot();
		for (unsigned int idx = 0; idx < snapshot.GetSessionCount(); idx++) {
			ibDataNode& row = sessions.AddChild(0, static_cast<ibMetaID>(idx));
			row.SetValue(wxT("User"), snapshot.GetUserName(idx));
			row.SetValue(wxT("Application"), snapshot.GetApplication(idx));
			row.SetValue(wxT("Type"), snapshot.GetSessionKindDescr(idx));
			row.SetValue(wxT("Started"), snapshot.GetStartedDate(idx));
			row.SetValue(wxT("Computer"), snapshot.GetComputerName(idx));
			row.SetValue(wxT("Session"), snapshot.GetSession(idx));
			row.SetValue(wxT("Activity"), snapshot.GetActivity(idx));
		}
	}

	// The locks held — sys_lock as it stands. A read that fails shows none, as the dialog's did.
	std::vector<ibLockSnapshotRow> rows;
	try {
		if (ibLockManager* const lockManager = ibApplicationInstance::GetLockManager(applicationInstance))
			rows = lockManager->GetSnapshot();
	}
	catch (...) {
		rows.clear();
	}

	ibDataNode& locks = result.Child(wxT("Locks"));
	for (std::size_t idx = 0; idx < rows.size(); idx++) {
		const ibLockSnapshotRow& lock = rows[idx];
		ibDataNode& row = locks.AddChild(0, static_cast<ibMetaID>(idx));
		row.SetValue(wxT("Namespace"), lock.namespaceName);
		row.SetValue(wxT("Key"), lock.keyData);
		row.SetValue(wxT("Mode"), lock.lockMode == ibLockMode::Shared ? wxString(_("Shared")) : wxString(_("Exclusive")));
		row.SetValue(wxT("User"), lock.userName);
		row.SetValue(wxT("Acquired"), !lock.acquiredAt.IsEmpty() ? lock.acquiredAt.ToWxDateTime().FormatISOCombined() : wxString());
		row.SetValue(wxT("Lock"), lock.lockGuid.str());
	}
}

std::function<void()> ibSchemaActiveUser::Command(const ibSession& session, s32 command,
	const ibDataNode& args, ibProtocolRefusal& refusal, wxString& error) const
{
	// The designer's actions — the desktop's dialog offered them in the designer alone (DesignerMode there).
	if (!IsDesignerSessionKind(session.GetKind())) {
		refusal = ibProtocolRefusal::NotFound;
		error = wxString::Format(wxT("active users take no command %d here"), command);
		return nullptr;
	}

	const ibApplicationInstance* const applicationInstance = session.GetApplicationInstance();

	// Done in the asking — the work is nothing more. A failed one is the journal's, as the desktop's dialog had it.
	const auto done = []() {};

	switch (static_cast<ibProtocolSchemaCommand>(command)) {
	case ibProtocolSchemaCommand::Kick: {
		const wxString guid = args.GetValue<wxString>(wxT("Session"));

		// Self-kick guard: kicking the asking session only closes the designer that asked, not a peer.
		if (guid == session.GetId()) {
			refusal = ibProtocolRefusal::Failed;
			error = _("Cannot kick the current session. Close the application instead.");
			return nullptr;
		}

		ibSessionRegistry* const registry = ibApplicationInstance::GetSessionRegistry(applicationInstance);
		if (registry == nullptr || !registry->Kick(guid))
			ibJournalWarning(wxT("ui"), _("Failed to queue kick for session %s"), guid);
		return done;
	}
	case ibProtocolSchemaCommand::Reload: {
		const wxString guid = args.GetValue<wxString>(wxT("Session"));

		// Same self-guard as kick: reloading the asking session would just close the designer that asked.
		if (guid == session.GetId()) {
			refusal = ibProtocolRefusal::Failed;
			error = _("Cannot reload the current session.");
			return nullptr;
		}

		ibSessionRegistry* const registry = ibApplicationInstance::GetSessionRegistry(applicationInstance);
		if (registry == nullptr || !registry->Reload(guid))
			ibJournalWarning(wxT("ui"), _("Failed to queue reload for session %s"), guid);
		return done;
	}
	case ibProtocolSchemaCommand::Release: {
		const wxString lockGuid = args.GetValue<wxString>(wxT("Lock"));
		try {
			const ibGuid guid(lockGuid);
			if (!guid.isValid()) {
				ibJournalWarning(wxT("ui"), _("Invalid lock guid: %s"), lockGuid);
				return done;
			}
			ibLockManager* const lockManager = ibApplicationInstance::GetLockManager(applicationInstance);
			if (lockManager == nullptr) {
				ibJournalWarning(wxT("ui"), _("Lock manager not available"));
				return done;
			}
			lockManager->ReleaseRows(std::vector<ibGuid>{ guid });
		}
		catch (const ibCoreException& err) {
			ibJournalWarning(wxT("ui"), _("Failed to release lock %s: %s"), lockGuid, err.GetErrorDescription());
		}
		catch (...) {
			ibJournalWarning(wxT("ui"), _("Failed to release lock %s"), lockGuid);
		}
		return done;
	}
	default:
		break;
	}

	refusal = ibProtocolRefusal::NotFound;
	error = wxString::Format(wxT("active users have no command %d"), command);
	return nullptr;
}
