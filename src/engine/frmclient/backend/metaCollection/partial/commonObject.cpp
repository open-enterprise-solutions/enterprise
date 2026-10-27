#include "commonObject.h"
#include "frmclient/backend/metaData.h"
#include "frmclient/mainFrame/mainFrame.h"
#include "protocol/protocol.h"       // ibProtocol::ibSchema::ibKind::Predefined — the editor the server holds
#include "protocol/protocolNode.h"

// The predefined editor's three edits, as the engine makes them (backend/metaCollection/partial/commonObject.cpp) —
// done by the server's editor (ibProtocol::ibSchema::ibKind::Predefined), and what the object declares read back.

namespace {

// The editor's params over `object` — its id, said in its tree's tab — `args` filled.
ibProtocolNode PredefinedParams(const ibValueMetaObject* object, ibProtocolNode args)
{
	args.SetValue(ibProtocol::ibSchema::ibPredefined::Id, static_cast<long long>(object->GetMetaID()));
	const long long tabId = object->GetMetaData()->GetTabId();
	if (tabId != 0)
		args.SetValue(ibProtocol::ibSchema::ibPredefined::Tab, tabId);

	ibProtocolNode params;
	params.SetValue(ibProtocol::ibSchema::Schema, static_cast<long long>(ibProtocol::ibSchema::ibKind::Predefined));
	params.SetValue(ibProtocol::ibSchema::Args, args);
	return params;
}

// …a command of it said. False: refused, and the person told why.
bool Command(const ibValueMetaObject* object, ibProtocol::ibSchema::ibCommand command, const ibProtocolNode& args)
{
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	if (frame == nullptr)
		return false;

	ibProtocolNode params = PredefinedParams(object, args);
	params.SetValue(ibProtocol::ibSchema::Command, static_cast<long long>(command));
	ibProtocolNode answer;
	return frame->Call(ibProtocol::ibMethod::Schema, params, answer);
}

typedef ibValueMetaObjectRecordDataHierarchyMutableRef::ibPredefinedValueObject ibPredefinedItem;

// What an item is given, as Create and Set take it.
ibProtocolNode ItemArgs(const ibString& strPredefinedName, const ibString& strCode, const ibString& strDescription,
	bool valueIsFolder, const wxObjectDataPtr<ibPredefinedItem>& valueParent)
{
	ibProtocolNode args;
	args.SetValue(ibProtocol::ibSchema::ibPredefined::Name, strPredefinedName);
	args.SetValue(ibProtocol::ibSchema::ibPredefined::Code, strCode);
	args.SetValue(ibProtocol::ibSchema::ibPredefined::Description, strDescription);
	args.SetValue(ibProtocol::ibSchema::ibPredefined::IsFolder, valueIsFolder);
	if (valueParent != nullptr)
		args.SetValue(ibProtocol::ibSchema::ibPredefined::Parent, valueParent->GetPredefinedGuid().str());
	return args;
}

} // namespace

void ibValueMetaObjectRecordDataHierarchyMutableRef::AppendPredefinedValue(const ibString& strPredefinedName,
	const ibString& strCode, const ibString& strDescription,
	bool valueIsFolder, const wxObjectDataPtr<ibPredefinedValueObject>& valueParent)
{
	Command(this, ibProtocol::ibSchema::ibCommand::Create,
		ItemArgs(strPredefinedName, strCode, strDescription, valueIsFolder, valueParent));
	ReadPredefinedValues();
}

void ibValueMetaObjectRecordDataHierarchyMutableRef::SetPredefinedValue(const ibGuid& predefinedGuid,
	const ibString& strPredefinedName,
	const ibString& strCode, const ibString& strDescription,
	bool valueIsFolder, const wxObjectDataPtr<ibPredefinedValueObject>& valueParent)
{
	ibProtocolNode args = ItemArgs(strPredefinedName, strCode, strDescription, valueIsFolder, valueParent);
	args.SetValue(ibProtocol::ibSchema::ibPredefined::Guid, predefinedGuid.str());
	Command(this, ibProtocol::ibSchema::ibCommand::Set, args);
	ReadPredefinedValues();
}

void ibValueMetaObjectRecordDataHierarchyMutableRef::DeletePredefinedValue(const ibGuid& predefinedGuid) {

	ibProtocolNode args;
	args.SetValue(ibProtocol::ibSchema::ibPredefined::Guid, predefinedGuid.str());
	Command(this, ibProtocol::ibSchema::ibCommand::Remove, args);
	ReadPredefinedValues();
}

void ibValueMetaObjectRecordDataHierarchyMutableRef::ReadPredefinedValues()
{
	m_predefinedObjectVector.clear();

	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	ibProtocolNode shown;
	if (frame == nullptr || !frame->Call(ibProtocol::ibMethod::Schema, PredefinedParams(this, ibProtocolNode()), shown))
		return;

	// The parent is linked once every item is read, as the engine's ReadData links it.
	std::vector<std::pair<wxObjectDataPtr<ibPredefinedValueObject>, ibString>> parents;
	for (const ibProtocolNode& row : shown.FindChild(ibProtocol::ibSchema::ibPredefined::Items).Children()) {
		m_predefinedObjectVector.emplace_back(
			new ibPredefinedValueObject(ibGuid(row.GetString(ibProtocol::ibSchema::ibPredefined::Guid)),
				row.GetString(ibProtocol::ibSchema::ibPredefined::Name), row.GetString(ibProtocol::ibSchema::ibPredefined::Code),
				row.GetString(ibProtocol::ibSchema::ibPredefined::Description), row.GetBool(ibProtocol::ibSchema::ibPredefined::IsFolder)));
		if (row.Has(ibProtocol::ibSchema::ibPredefined::Parent))
			parents.emplace_back(m_predefinedObjectVector.back(), row.GetString(ibProtocol::ibSchema::ibPredefined::Parent));
	}
	for (const auto& parent : parents)
		parent.first->m_valueParent = FindPredefinedValue(ibGuid(parent.second));
}
