////////////////////////////////////////////////////////////////////////////
//	Description : ibStandardCommandDescription — the action a tool runs, written down
////////////////////////////////////////////////////////////////////////////

#include "standardCommandDescription.h"

#include "core/serialize/dataBuilder.h"   // ibDataValue — node value

// node form: a Child { Action: <system id>, Name: <custom handler> }.
bool ibActionDescriptionMemory::ReadNode(const ibDataValue& value, ibStandardCommandDescription& actionDesc)
{
	const std::shared_ptr<ibDataNode>& root = value.AsChild();
	if (!root)
		return false;
	actionDesc.m_lAction = root->GetValue<std::int32_t>(wxT("Action"));
	actionDesc.m_strAction = root->GetValue<wxString>(wxT("Name"));
	return true;
}

bool ibActionDescriptionMemory::WriteNode(ibDataValue& value, const ibStandardCommandDescription& actionDesc)
{
	auto root = std::make_shared<ibDataNode>();
	root->SetValue(wxT("Action"), (std::int32_t)actionDesc.m_lAction);
	root->SetValue(wxT("Name"),   actionDesc.m_strAction);
	value = ibDataValue::Child(root);
	return true;
}
