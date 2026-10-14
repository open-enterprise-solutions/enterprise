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
	params.SetValue(ibProtocolName::Schema, static_cast<long long>(ibProtocolSchema::UserList));
	params.Child(ibProtocolName::Args).SetValue(ibProtocolName::Guid, userGuid.str());
	if (!frame->Call(ibProtocolMethod::Schema, params, answer))
		return info;

	const ibProtocolNode user = answer.FindChild(ibProtocolName::User);
	if (user.IsEmpty())
		return info;

	info.m_strUserGuid = user.GetString(ibProtocolName::Guid);
	info.m_strUserName = user.GetString(ibProtocolName::Name);
	info.m_strUserFullName = user.GetString(ibProtocolName::FullName);
	info.m_bPasswordSet = user.GetBool(ibProtocolName::Password);
	info.m_strLanguageGuid = user.GetString(ibProtocolName::Language);
	for (const ibProtocolNode& role : user.GetList(ibProtocolName::Roles))
		info.m_roleArray.emplace_back().m_strRoleGuid = role.AsString();
	return info;
}

bool ibUserInfo::Save(const ibUserInfo& info)
{
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	if (frame == nullptr)
		return false;

	ibProtocolNode params, answer;
	params.SetValue(ibProtocolName::Schema, static_cast<long long>(ibProtocolSchema::UserList));
	params.SetValue(ibProtocolName::Command, static_cast<long long>(ibProtocolSchemaCommand::Save));
	ibProtocolNode args = params.Child(ibProtocolName::Args);
	args.SetValue(ibProtocolName::Guid, info.m_strUserGuid);
	args.SetValue(ibProtocolName::Name, info.m_strUserName);
	args.SetValue(ibProtocolName::FullName, info.m_strUserFullName);
	args.SetValue(ibProtocolName::Language, info.m_strLanguageGuid);
	// No list — no roles: the user is saved with none.
	for (const ibUserRole& role : info.m_roleArray)
		args.AddItem(ibProtocolName::Roles, role.m_strRoleGuid);
	if (info.m_bPasswordTouched)
		args.SetValue(ibProtocolName::Password, info.m_strUserPassword);
	return frame->Call(ibProtocolMethod::Schema, params, answer);
}

bool ibUserInfo::Delete(const ibGuid& userGuid)
{
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	if (frame == nullptr || !userGuid.isValid())
		return false;

	ibProtocolNode params, answer;
	params.SetValue(ibProtocolName::Schema, static_cast<long long>(ibProtocolSchema::UserList));
	params.SetValue(ibProtocolName::Command, static_cast<long long>(ibProtocolSchemaCommand::Remove));
	params.Child(ibProtocolName::Args).SetValue(ibProtocolName::Guid, userGuid.str());
	return frame->Call(ibProtocolMethod::Schema, params, answer);
}

std::vector<ibUserInfo::Brief> ibUserInfo::ListAll()
{
	std::vector<Brief> list;
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	if (frame == nullptr)
		return list;

	ibProtocolNode params, answer;
	params.SetValue(ibProtocolName::Schema, static_cast<long long>(ibProtocolSchema::UserList));
	if (!frame->Call(ibProtocolMethod::Schema, params, answer))
		return list;

	for (const ibProtocolNode& row : answer.FindChild(ibProtocolName::Users).Children()) {
		Brief entry;
		entry.m_strUserGuid = row.GetString(ibProtocolName::Guid);
		entry.m_strUserName = row.GetString(ibProtocolName::Name);
		entry.m_strUserFullName = row.GetString(ibProtocolName::FullName);
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
	params.SetValue(ibProtocolName::Schema, static_cast<long long>(ibProtocolSchema::UserList));
	if (!frame->Call(ibProtocolMethod::Schema, params, answer))
		return list;

	for (const ibProtocolNode& row : answer.FindChild(ibProtocolName::Roles).Children()) {
		RoleBrief entry;
		entry.m_strRoleGuid = row.GetString(ibProtocolName::Guid);
		entry.m_strRoleName = row.GetString(ibProtocolName::Name);
		entry.m_strRoleTitle = row.GetString(ibProtocolName::Title);
		entry.m_mode = static_cast<ibRoleCompositionMode>(row.GetInt(ibProtocolName::Mode, ibRoleCompositionMode_Union));
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
	params.SetValue(ibProtocolName::Schema, static_cast<long long>(ibProtocolSchema::Languages));
	if (metaData != nullptr)
		params.Child(ibProtocolName::Args).SetValue(ibProtocolName::Tab, metaData->GetTabId());
	if (!frame->Call(ibProtocolMethod::Schema, params, answer))
		return list;

	for (const ibProtocolNode& row : answer.FindChild(ibProtocolName::Languages).Children()) {
		LanguageBrief entry;
		entry.m_strLanguageGuid = row.GetString(ibProtocolName::Guid);
		entry.m_strLanguageTitle = row.GetString(ibProtocolName::Title);
		entry.m_strLanguageCode = row.GetString(ibProtocolName::Code);
		list.emplace_back(std::move(entry));
	}
	return list;
}
