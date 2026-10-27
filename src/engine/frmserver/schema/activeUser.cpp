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
#include "backend/backend_picture.h"   // ibBackendPicture::SendPicture — the dialog's pictures, by their ids

bool ibSchemaActiveUser::AccessRight(const ibSession& session) const
{
	const ibMetaDataConfigurationBase* const metaData = session.GetMetaData();
	return metaData != nullptr && metaData->AccessRight_ActiveUsers();
}

void ibSchemaActiveUser::Build(const ibSession& session, const ibDataNode& /*args*/, ibDataNode& result) const
{
	const ibApplicationInstance* const applicationInstance = session.GetApplicationInstance();

	// The dialog's pictures, as the desktop's drew them — the window's, and the one beside each user.
	result.SetValue(ibProtocol::ibSchema::ibActiveUser::Icon, ibBackendPicture::SendPicture(g_picUserActiveCLSID));
	result.SetValue(ibProtocol::ibSchema::ibActiveUser::Picture, ibBackendPicture::SendPicture(g_picUserCLSID));

	// The sessions — every process's in the base, not this server's alone.
	ibDataNode& sessions = result.Child(ibProtocol::ibSchema::ibActiveUser::Sessions);
	if (ibSessionRegistry* const registry = ibApplicationInstance::GetSessionRegistry(applicationInstance)) {
		const ibSessionSnapshot snapshot = registry->GetClusterSnapshot();
		for (unsigned int idx = 0; idx < snapshot.GetSessionCount(); idx++) {
			ibDataNode& row = sessions.AddChild(0, static_cast<ibMetaID>(idx));
			row.SetValue(ibProtocol::ibSchema::ibActiveUser::User, snapshot.GetUserName(idx));
			row.SetValue(ibProtocol::ibSchema::ibActiveUser::Application, snapshot.GetApplication(idx));
			row.SetValue(ibProtocol::ibSchema::ibActiveUser::Type, snapshot.GetSessionKindDescr(idx));
			row.SetValue(ibProtocol::ibSchema::ibActiveUser::Started, snapshot.GetStartedDate(idx));
			row.SetValue(ibProtocol::ibSchema::ibActiveUser::Computer, snapshot.GetComputerName(idx));
			row.SetValue(ibProtocol::ibSchema::ibActiveUser::Session, snapshot.GetSession(idx));
			row.SetValue(ibProtocol::ibSchema::ibActiveUser::Activity, snapshot.GetActivity(idx));
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

	ibDataNode& locks = result.Child(ibProtocol::ibSchema::ibActiveUser::Locks);
	for (std::size_t idx = 0; idx < rows.size(); idx++) {
		const ibLockSnapshotRow& lock = rows[idx];
		ibDataNode& row = locks.AddChild(0, static_cast<ibMetaID>(idx));
		row.SetValue(ibProtocol::ibSchema::ibActiveUser::Namespace, lock.namespaceName);
		row.SetValue(ibProtocol::ibSchema::ibActiveUser::Key, lock.keyData);
		row.SetValue(ibProtocol::ibSchema::ibActiveUser::Mode, lock.lockMode == ibLockMode::Shared ? ibString(_("Shared")) : ibString(_("Exclusive")));
		row.SetValue(ibProtocol::ibSchema::ibActiveUser::User, lock.userName);
		row.SetValue(ibProtocol::ibSchema::ibActiveUser::Acquired, !lock.acquiredAt.IsEmpty() ? lock.acquiredAt.Format(ibT("%Y-%m-%dT%H:%M:%S")) : ibString());
		row.SetValue(ibProtocol::ibSchema::ibActiveUser::Lock, lock.lockGuid.str());
	}
}

std::function<void()> ibSchemaActiveUser::Command(const ibSession& session, std::int32_t command,
	const ibDataNode& args, ibProtocol::ibRefusal& refusal, ibString& error) const
{
	// The designer's actions — the desktop's dialog offered them in the designer alone (DesignerMode there).
	if (!IsDesignerSessionKind(session.GetKind())) {
		refusal = ibProtocol::ibRefusal::NotFound;
		error = ibString::Format(ibT("active users take no command %d here"), command);
		return nullptr;
	}

	const ibApplicationInstance* const applicationInstance = session.GetApplicationInstance();

	// Done in the asking — the work is nothing more. A failed one is the journal's, as the desktop's dialog had it.
	const auto done = []() {};

	switch (static_cast<ibProtocol::ibSchema::ibCommand>(command)) {
	case ibProtocol::ibSchema::ibCommand::Kick: {
		const ibString guid = args.GetValue<ibString>(ibProtocol::ibSchema::ibActiveUser::Session);

		// Self-kick guard: kicking the asking session only closes the designer that asked, not a peer.
		if (guid == session.GetId()) {
			refusal = ibProtocol::ibRefusal::Failed;
			error = _("Cannot kick the current session. Close the application instead.");
			return nullptr;
		}

		ibSessionRegistry* const registry = ibApplicationInstance::GetSessionRegistry(applicationInstance);
		if (registry == nullptr || !registry->Kick(guid))
			ibJournalWarning(ibT("ui"), _("Failed to queue kick for session %s"), guid);
		return done;
	}
	case ibProtocol::ibSchema::ibCommand::Reload: {
		const ibString guid = args.GetValue<ibString>(ibProtocol::ibSchema::ibActiveUser::Session);

		// Same self-guard as kick: reloading the asking session would just close the designer that asked.
		if (guid == session.GetId()) {
			refusal = ibProtocol::ibRefusal::Failed;
			error = _("Cannot reload the current session.");
			return nullptr;
		}

		ibSessionRegistry* const registry = ibApplicationInstance::GetSessionRegistry(applicationInstance);
		if (registry == nullptr || !registry->Reload(guid))
			ibJournalWarning(ibT("ui"), _("Failed to queue reload for session %s"), guid);
		return done;
	}
	case ibProtocol::ibSchema::ibCommand::Release: {
		const ibString lockGuid = args.GetValue<ibString>(ibProtocol::ibSchema::ibActiveUser::Lock);
		try {
			const ibGuid guid(lockGuid);
			if (!guid.isValid()) {
				ibJournalWarning(ibT("ui"), _("Invalid lock guid: %s"), lockGuid);
				return done;
			}
			ibLockManager* const lockManager = ibApplicationInstance::GetLockManager(applicationInstance);
			if (lockManager == nullptr) {
				ibJournalWarning(ibT("ui"), _("Lock manager not available"));
				return done;
			}
			lockManager->ReleaseRows(std::vector<ibGuid>{ guid });
		}
		catch (const ibCoreException& err) {
			ibJournalWarning(ibT("ui"), _("Failed to release lock %s: %s"), lockGuid, err.GetErrorDescription());
		}
		catch (...) {
			ibJournalWarning(ibT("ui"), _("Failed to release lock %s"), lockGuid);
		}
		return done;
	}
	default:
		break;
	}

	refusal = ibProtocol::ibRefusal::NotFound;
	error = ibString::Format(ibT("active users have no command %d"), command);
	return nullptr;
}
