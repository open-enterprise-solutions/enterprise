#include "commonObject.h"
#include "frmclient/backend/metaData.h"
#include "frmclient/mainFrame/mainFrame.h"
#include "protocol/protocol.h"       // ibProtocolSchema::Predefined — the editor the server holds
#include "protocol/protocolNode.h"

// The predefined editor's three edits, as the engine makes them (backend/metaCollection/partial/commonObject.cpp) —
// done by the server's editor (ibProtocolSchema::Predefined), and what the object declares read back.

namespace {

// The editor's params over `object` — its id, said in its tree's tab — `args` filled.
ibProtocolNode PredefinedParams(const ibValueMetaObject* object, ibProtocolNode args)
{
	args.SetValue(ibProtocolName::Id, static_cast<long long>(object->GetMetaID()));
	const long long tabId = object->GetMetaData()->GetTabId();
	if (tabId != 0)
		args.SetValue(ibProtocolName::Tab, tabId);

	ibProtocolNode params;
	params.SetValue(ibProtocolName::Schema, static_cast<long long>(ibProtocolSchema::Predefined));
	params.SetValue(ibProtocolName::Args, args);
	return params;
}

// …a command of it said. False: refused, and the person told why.
bool Command(const ibValueMetaObject* object, ibProtocolSchemaCommand command, const ibProtocolNode& args)
{
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	if (frame == nullptr)
		return false;

	ibProtocolNode params = PredefinedParams(object, args);
	params.SetValue(ibProtocolName::Command, static_cast<long long>(command));
	ibProtocolNode answer;
	return frame->Call(ibProtocolMethod::Schema, params, answer);
}

typedef ibValueMetaObjectRecordDataHierarchyMutableRef::ibPredefinedValueObject ibPredefinedItem;

// What an item is given, as Create and Set take it.
ibProtocolNode ItemArgs(const wxString& strPredefinedName, const wxString& strCode, const wxString& strDescription,
	bool valueIsFolder, const wxObjectDataPtr<ibPredefinedItem>& valueParent)
{
	ibProtocolNode args;
	args.SetValue(ibProtocolName::Name, strPredefinedName);
	args.SetValue(ibProtocolName::Code, strCode);
	args.SetValue(ibProtocolName::Description, strDescription);
	args.SetValue(ibProtocolName::IsFolder, valueIsFolder);
	if (valueParent != nullptr)
		args.SetValue(ibProtocolName::Parent, valueParent->GetPredefinedGuid().str());
	return args;
}

} // namespace

void ibValueMetaObjectRecordDataHierarchyMutableRef::AppendPredefinedValue(const wxString& strPredefinedName,
	const wxString& strCode, const wxString& strDescription,
	bool valueIsFolder, const wxObjectDataPtr<ibPredefinedValueObject>& valueParent)
{
	Command(this, ibProtocolSchemaCommand::Create,
		ItemArgs(strPredefinedName, strCode, strDescription, valueIsFolder, valueParent));
	ReadPredefinedValues();
}

void ibValueMetaObjectRecordDataHierarchyMutableRef::SetPredefinedValue(const ibGuid& predefinedGuid,
	const wxString& strPredefinedName,
	const wxString& strCode, const wxString& strDescription,
	bool valueIsFolder, const wxObjectDataPtr<ibPredefinedValueObject>& valueParent)
{
	ibProtocolNode args = ItemArgs(strPredefinedName, strCode, strDescription, valueIsFolder, valueParent);
	args.SetValue(ibProtocolName::Guid, predefinedGuid.str());
	Command(this, ibProtocolSchemaCommand::Set, args);
	ReadPredefinedValues();
}

void ibValueMetaObjectRecordDataHierarchyMutableRef::DeletePredefinedValue(const ibGuid& predefinedGuid) {

	ibProtocolNode args;
	args.SetValue(ibProtocolName::Guid, predefinedGuid.str());
	Command(this, ibProtocolSchemaCommand::Remove, args);
	ReadPredefinedValues();
}

void ibValueMetaObjectRecordDataHierarchyMutableRef::ReadPredefinedValues()
{
	m_predefinedObjectVector.clear();

	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	ibProtocolNode shown;
	if (frame == nullptr || !frame->Call(ibProtocolMethod::Schema, PredefinedParams(this, ibProtocolNode()), shown))
		return;

	// The parent is linked once every item is read, as the engine's ReadData links it.
	std::vector<std::pair<wxObjectDataPtr<ibPredefinedValueObject>, wxString>> parents;
	for (const ibProtocolNode& row : shown.FindChild(ibProtocolName::Items).Children()) {
		m_predefinedObjectVector.emplace_back(
			new ibPredefinedValueObject(ibGuid(row.GetString(ibProtocolName::Guid)),
				row.GetString(ibProtocolName::Name), row.GetString(ibProtocolName::Code),
				row.GetString(ibProtocolName::Description), row.GetBool(ibProtocolName::IsFolder)));
		if (row.Has(ibProtocolName::Parent))
			parents.emplace_back(m_predefinedObjectVector.back(), row.GetString(ibProtocolName::Parent));
	}
	for (const auto& parent : parents)
		parent.first->m_valueParent = FindPredefinedValue(ibGuid(parent.second));
}
