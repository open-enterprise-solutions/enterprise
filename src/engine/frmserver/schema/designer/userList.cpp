#include "userList.h"

#include <algorithm>

#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/metaObjectMetadata.h"   // AccessRight_Administration of a role — the item's OK check
#include "backend/metaCollection/metaLanguageObject.h"
#include "backend/metaCollection/metaRoleObject.h"
#include "backend/session/session.h"
#include "backend/userInfo.h"
#include "backend/utils/passwordHash.hpp"
#include "core/serialize/dataBuilder.h"

bool ibSchemaUserList::AccessRight(const ibSession& session) const
{
	const ibMetaDataConfigurationBase* const metaData = session.GetMetaData();
	return metaData != nullptr && metaData->AccessRight_DataAdministration();
}

void ibSchemaUserList::Build(const ibSession& session, const ibDataNode& args, ibDataNode& result) const
{
	const wxString guid = args.GetValue<wxString>(ibProtocol::ibSchema::ibUserList::Guid);

	// The list — the rows ibUserListModel::PrepareData read, a Brief each — and what a user's record is chosen from:
	// the roles ibDialogUserItem listed off the configuration, which the client does not hold (its languages are
	// every translation's — ibSchemaLanguages).
	if (guid.IsEmpty()) {
		ibDataNode& users = result.Child(ibProtocol::ibSchema::ibUserList::Users);
		const std::vector<ibUserInfo::Brief> list = ibUserInfo::ListAll();
		for (std::size_t idx = 0; idx < list.size(); idx++) {
			ibDataNode& row = users.AddChild(0, static_cast<ibMetaID>(idx));
			row.SetValue(ibProtocol::ibSchema::ibUserList::Guid, list[idx].m_strUserGuid);
			row.SetValue(ibProtocol::ibSchema::ibUserList::Name, list[idx].m_strUserName);
			row.SetValue(ibProtocol::ibSchema::ibUserList::FullName, list[idx].m_strUserFullName);
		}

		const ibMetaDataConfigurationBase* const metaData = session.GetMetaData();
		if (metaData == nullptr)
			return;

		ibDataNode& roles = result.Child(ibProtocol::ibSchema::ibUserList::Roles);
		ibMetaID idx = 0;
		for (const auto object : metaData->GetAnyArrayObject<ibValueMetaObjectRole>(g_metaRoleCLSID)) {
			ibDataNode& row = roles.AddChild(0, idx++);
			row.SetValue(ibProtocol::ibSchema::ibUserList::Guid, object->GetDocPath());
			row.SetValue(ibProtocol::ibSchema::ibUserList::Name, object->GetName());
			row.SetValue(ibProtocol::ibSchema::ibUserList::Title, object->GetSynonym());
			row.SetValue(ibProtocol::ibSchema::ibUserList::Mode, static_cast<std::int32_t>(object->GetRoleCompositionMode()));
		}
		return;
	}

	// One user — what ibDialogUserItem::ReadUserData read; none of it when there is no such user. The stored password is
	// said, not given.
	const ibUserInfo userInfo = ibUserInfo::Read(ibGuid(guid));
	if (!userInfo.IsOk())
		return;

	ibDataNode& user = result.Child(ibProtocol::ibSchema::ibUserList::User);
	user.SetValue(ibProtocol::ibSchema::ibUserList::Guid, userInfo.m_strUserGuid);
	user.SetValue(ibProtocol::ibSchema::ibUserList::Name, userInfo.m_strUserName);
	user.SetValue(ibProtocol::ibSchema::ibUserList::FullName, userInfo.m_strUserFullName);
	user.SetValue(ibProtocol::ibSchema::ibUserList::Password, userInfo.IsSetPassword());
	if (userInfo.IsSetLanguage())
		user.SetValue(ibProtocol::ibSchema::ibUserList::Language, userInfo.m_strLanguageGuid);
	std::vector<ibDataValue> roles;
	for (const ibUserInfo::ibUserRole& role : userInfo.m_roleArray)
		roles.push_back(ibDataValue::String(role.m_strRoleGuid));
	user.AddField(ibProtocol::ibSchema::ibUserList::Roles, ibDataValue::Array(std::move(roles)));
}

std::function<void()> ibSchemaUserList::Command(const ibSession& session, std::int32_t command,
	const ibDataNode& args, ibProtocol::ibRefusal& refusal, wxString& error) const
{
	const ibMetaDataConfigurationBase* const metaData = session.GetMetaData();
	if (metaData == nullptr) {
		refusal = ibProtocol::ibRefusal::NoSession;
		error = wxT("the session has no configuration");
		return nullptr;
	}

	// Done in the asking — the work is nothing more: the frame the client is answered with.
	const auto done = []() {};

	switch (static_cast<ibProtocol::ibSchema::ibCommand>(command)) {
	case ibProtocol::ibSchema::ibCommand::Save: {
		const ibGuid userGuid(args.GetValue<wxString>(ibProtocol::ibSchema::ibUserList::Guid));
		if (!userGuid.isValid()) {
			refusal = ibProtocol::ibRefusal::NotFound;
			error = wxT("no user guid");
			return nullptr;
		}

		try {
			ibUserInfo userInfo;

			userInfo.m_strUserGuid = userGuid.str();
			userInfo.m_strUserName = args.GetValue<wxString>(ibProtocol::ibSchema::ibUserList::Name);
			userInfo.m_strUserFullName = args.GetValue<wxString>(ibProtocol::ibSchema::ibUserList::FullName);

			// The hash the item kept untouched is the stored one — read here, where it stays. A copy is a new guid: it
			// has none stored, and starts without a password, as the desktop's copy did.
			const ibDataValue* const password = args.FindField(ibProtocol::ibSchema::ibUserList::Password);
			userInfo.m_strUserPassword = ibPasswordHash::PasswordToStore(ibUserInfo::Read(userGuid).m_strUserPassword,
				password != nullptr, password != nullptr ? password->AsString().ToWxString() : wxString());

			const ibValueMetaObjectConfiguration* commonObject = metaData->GetCommonMetaObject();
			wxASSERT(commonObject);

			// The language and the roles the item offered — the configuration's, found by the guids it sent.
			const wxString languageGuid = args.GetValue<wxString>(ibProtocol::ibSchema::ibUserList::Language);
			for (const auto object : metaData->GetAnyArrayObject<ibValueMetaObjectLanguage>(g_metaLanguageCLSID)) {
				if (object->GetDocPath() != languageGuid)
					continue;
				userInfo.m_strLanguageGuid = object->GetDocPath();
				userInfo.m_strLanguageCode = object->GetLangCode();
				break;
			}

			std::vector<wxString> checked;
			if (const ibDataValue* const roles = args.FindField(ibProtocol::ibSchema::ibUserList::Roles)) {
				for (const ibDataValue& role : roles->AsArray())
					checked.push_back(role.AsString());
			}

			bool access_right = false;

			for (const auto object : metaData->GetAnyArrayObject<ibValueMetaObjectRole>(g_metaRoleCLSID)) {
				if (std::find(checked.begin(), checked.end(), object->GetDocPath()) == checked.end())
					continue;
				auto& entry = userInfo.m_roleArray.emplace_back();
				entry.m_strRoleGuid = object->GetDocPath();
				entry.m_strRoleName = object->GetName();
				entry.m_miRoleId = object->GetMetaID();
				entry.m_mode = object->GetRoleCompositionMode();
				// Parenthesised as it always behaved: a role grants the right only if it carries
				// BOTH administration flags, and any one such role is enough for the user.
				access_right = access_right || (commonObject->AccessRight_Administration(entry.m_miRoleId) &&
					commonObject->AccessRight_DataAdministration(entry.m_miRoleId));
			}

			// …or another user keeps one. The desktop's loop went on past a user who did, and the last user's roles
			// decided; any one is enough.
			if (!access_right) {
				for (const auto& brief : ibUserInfo::ListAll()) {
					const ibGuid otherGuid(brief.m_strUserGuid);
					if (otherGuid == userGuid)
						continue;
					const ibUserInfo userEntry = ibUserInfo::Read(otherGuid);
					for (const auto& role : userEntry.m_roleArray) {   // by reference — a role is a struct, not a handle
						access_right = commonObject->AccessRight_Administration(role.m_miRoleId) &&
							commonObject->AccessRight_DataAdministration(role.m_miRoleId);
						if (access_right) break;
					}
					if (access_right) break;
				}
			}

			if (!access_right) {
				refusal = ibProtocol::ibRefusal::Failed;
				error = _("After recording, there would not be a single role with admin rights. The action is impossible!");
				return nullptr;
			}

			if (ibUserInfo::Save(userInfo))
				return done;
			error = BackendErrorChain();
		}
		catch (const ibCoreException& e) {
			error = BackendErrorChain(e.GetErrorDescription());
		}
		refusal = ibProtocol::ibRefusal::Failed;
		return nullptr;
	}
	case ibProtocol::ibSchema::ibCommand::Remove: {
		if (ibUserInfo::Delete(ibGuid(args.GetValue<wxString>(ibProtocol::ibSchema::ibUserList::Guid))))
			return done;
		refusal = ibProtocol::ibRefusal::Failed;
		error = BackendErrorChain();
		return nullptr;
	}
	default:
		break;
	}

	refusal = ibProtocol::ibRefusal::NotFound;
	error = wxString::Format(wxT("the users take no command %d"), command);
	return nullptr;
}
