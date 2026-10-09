#include "activeUser.h"

#include "backend/appData.h"
#include "backend/backend_picture.h"   // the dialog's pictures, as the desktop's drew them
#include "backend/picturePredefined.h"
#include "backend/lock/lockManager.h"
#include "backend/metadataConfiguration.h"
#include "core/serialize/dataBuilder.h"
#include "backend/session/session.h"
#include "backend/session/sessionRegistry.h"
#include "backend/session/sessionSnapshot.h"

bool ibSchemaActiveUser::AccessRight(const ibSession& session) const
{
	const ibMetaDataConfigurationBase* const metaData = session.GetMetaData();
	return metaData != nullptr && metaData->AccessRight_ActiveUsers();
}

void ibSchemaActiveUser::Build(const ibSession& session, ibDataNode& result) const
{
	const ibApplicationInstance* const applicationInstance = session.GetApplicationInstance();

	// The dialog's pictures, as the desktop's drew them — the window's, and the one beside each user.
	result.SetValue(wxT("Icon"), wxString(ibBackendPicture::GetServerPicture(g_picUserActiveCLSID).GetData()));
	result.SetValue(wxT("Picture"), wxString(ibBackendPicture::GetServerPicture(g_picUserCLSID).GetData()));

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
