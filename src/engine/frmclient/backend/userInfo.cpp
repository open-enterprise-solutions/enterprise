#include "userInfo.h"

#include "frmclient/backend/metaData.h"   // the door a language list is asked through — its tab
#include "frmclient/mainFrame/mainFrame.h"
#include "protocol/protocol.h"
#include "protocol/protocolNode.h"

ibUserInfo ibUserInfo::Read(const ibGuid& userGuid)
{
	ibUserInfo info;
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	if (frame == nullptr || !userGuid.isValid())
		return info;

	ibProtocolNode params, answer;
	params.SetValue(ibProtocol::ibSchema::Schema, static_cast<long long>(ibProtocol::ibSchema::ibKind::UserList));
	params.Child(ibProtocol::ibSchema::Args).SetValue(ibProtocol::ibSchema::ibUserList::Guid, userGuid.str());
	if (!frame->Call(ibProtocol::ibMethod::Schema, params, answer))
		return info;

	const ibProtocolNode user = answer.FindChild(ibProtocol::ibSchema::ibUserList::User);
	if (user.IsEmpty())
		return info;

	info.m_strUserGuid = user.GetString(ibProtocol::ibSchema::ibUserList::Guid);
	info.m_strUserName = user.GetString(ibProtocol::ibSchema::ibUserList::Name);
	info.m_strUserFullName = user.GetString(ibProtocol::ibSchema::ibUserList::FullName);
	info.m_bPasswordSet = user.GetBool(ibProtocol::ibSchema::ibUserList::Password);
	info.m_strLanguageGuid = user.GetString(ibProtocol::ibSchema::ibUserList::Language);
	for (const ibProtocolNode& role : user.GetList(ibProtocol::ibSchema::ibUserList::Roles))
		info.m_roleArray.emplace_back().m_strRoleGuid = role.AsString();
	return info;
}

bool ibUserInfo::Save(const ibUserInfo& info)
{
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	if (frame == nullptr)
		return false;

	ibProtocolNode params, answer;
	params.SetValue(ibProtocol::ibSchema::Schema, static_cast<long long>(ibProtocol::ibSchema::ibKind::UserList));
	params.SetValue(ibProtocol::ibSchema::Command, static_cast<long long>(ibProtocol::ibSchema::ibCommand::Save));
	ibProtocolNode args = params.Child(ibProtocol::ibSchema::Args);
	args.SetValue(ibProtocol::ibSchema::ibUserList::Guid, info.m_strUserGuid);
	args.SetValue(ibProtocol::ibSchema::ibUserList::Name, info.m_strUserName);
	args.SetValue(ibProtocol::ibSchema::ibUserList::FullName, info.m_strUserFullName);
	args.SetValue(ibProtocol::ibSchema::ibUserList::Language, info.m_strLanguageGuid);
	// No list — no roles: the user is saved with none.
	for (const ibUserRole& role : info.m_roleArray)
		args.AddItem(ibProtocol::ibSchema::ibUserList::Roles, role.m_strRoleGuid);
	if (info.m_bPasswordTouched)
		args.SetValue(ibProtocol::ibSchema::ibUserList::Password, info.m_strUserPassword);
	return frame->Call(ibProtocol::ibMethod::Schema, params, answer);
}

bool ibUserInfo::Delete(const ibGuid& userGuid)
{
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	if (frame == nullptr || !userGuid.isValid())
		return false;

	ibProtocolNode params, answer;
	params.SetValue(ibProtocol::ibSchema::Schema, static_cast<long long>(ibProtocol::ibSchema::ibKind::UserList));
	params.SetValue(ibProtocol::ibSchema::Command, static_cast<long long>(ibProtocol::ibSchema::ibCommand::Remove));
	params.Child(ibProtocol::ibSchema::Args).SetValue(ibProtocol::ibSchema::ibUserList::Guid, userGuid.str());
	return frame->Call(ibProtocol::ibMethod::Schema, params, answer);
}

std::vector<ibUserInfo::Brief> ibUserInfo::ListAll()
{
	std::vector<Brief> list;
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	if (frame == nullptr)
		return list;

	ibProtocolNode params, answer;
	params.SetValue(ibProtocol::ibSchema::Schema, static_cast<long long>(ibProtocol::ibSchema::ibKind::UserList));
	if (!frame->Call(ibProtocol::ibMethod::Schema, params, answer))
		return list;

	for (const ibProtocolNode& row : answer.FindChild(ibProtocol::ibSchema::ibUserList::Users).Children()) {
		Brief entry;
		entry.m_strUserGuid = row.GetString(ibProtocol::ibSchema::ibUserList::Guid);
		entry.m_strUserName = row.GetString(ibProtocol::ibSchema::ibUserList::Name);
		entry.m_strUserFullName = row.GetString(ibProtocol::ibSchema::ibUserList::FullName);
		list.emplace_back(std::move(entry));
	}
	return list;
}

std::vector<ibUserInfo::RoleBrief> ibUserInfo::ListRoles()
{
	std::vector<RoleBrief> list;
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	if (frame == nullptr)
		return list;

	ibProtocolNode params, answer;
	params.SetValue(ibProtocol::ibSchema::Schema, static_cast<long long>(ibProtocol::ibSchema::ibKind::UserList));
	if (!frame->Call(ibProtocol::ibMethod::Schema, params, answer))
		return list;

	for (const ibProtocolNode& row : answer.FindChild(ibProtocol::ibSchema::ibUserList::Roles).Children()) {
		RoleBrief entry;
		entry.m_strRoleGuid = row.GetString(ibProtocol::ibSchema::ibUserList::Guid);
		entry.m_strRoleName = row.GetString(ibProtocol::ibSchema::ibUserList::Name);
		entry.m_strRoleTitle = row.GetString(ibProtocol::ibSchema::ibUserList::Title);
		entry.m_mode = static_cast<ibRoleCompositionMode>(row.GetInt(ibProtocol::ibSchema::ibUserList::Mode, ibRoleCompositionMode_Union));
		list.emplace_back(std::move(entry));
	}
	return list;
}

std::vector<ibUserInfo::LanguageBrief> ibUserInfo::ListLanguages(const ibMetaData* metaData)
{
	std::vector<LanguageBrief> list;
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	if (frame == nullptr)
		return list;

	ibProtocolNode params, answer;
	params.SetValue(ibProtocol::ibSchema::Schema, static_cast<long long>(ibProtocol::ibSchema::ibKind::Languages));
	if (metaData != nullptr)
		params.Child(ibProtocol::ibSchema::Args).SetValue(ibProtocol::ibSchema::ibLanguages::Tab, metaData->GetTabId());
	if (!frame->Call(ibProtocol::ibMethod::Schema, params, answer))
		return list;

	for (const ibProtocolNode& row : answer.FindChild(ibProtocol::ibSchema::ibLanguages::Languages).Children()) {
		LanguageBrief entry;
		entry.m_strLanguageGuid = row.GetString(ibProtocol::ibSchema::ibUserList::Guid);
		entry.m_strLanguageTitle = row.GetString(ibProtocol::ibSchema::ibUserList::Title);
		entry.m_strLanguageCode = row.GetString(ibProtocol::ibSchema::ibLanguages::Code);
		entry.m_default = row.GetBool(ibProtocol::ibSchema::ibLanguages::Default);
		list.emplace_back(std::move(entry));
	}
	return list;
}
