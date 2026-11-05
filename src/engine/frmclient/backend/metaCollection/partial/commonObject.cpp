#include "commonObject.h"
#include "frmclient/backend/metaData.h"
#include "frmclient/backend/compiler/value.h"           // ibValue::ReadClasses — the types a type is picked from, named
#include "frmclient/backend/serialize/dataProtocol.h"   // ibReadProtocolValue / ibWriteProtocolNode — what an item declares, as it travels
#include "frmclient/mainFrame/mainFrame.h"
#include "protocol/protocol.h"       // ibProtocol::ibSchema::ibKind::Predefined — the editor the server holds
#include "protocol/protocolDesigner.h"   // ibProtocol::ibSchema::ibPredefined
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

// What an item is given, as Create and Set take it: the item in the node form of the one schema
// (ibPredefinedItemDescriptionMemory) — its fields and the whole of what it declares; a section not there leaves
// its rows the data's.
ibProtocolNode ItemArgs(const ibPredefinedItemDescription& item)
{
	ibDataValue written;
	ibPredefinedItemDescriptionMemory::WriteNode(written, item);
	ibProtocolNode args;
	ibWriteProtocolNode(*written.AsChild(), args);
	return args;
}

} // namespace

bool ibValueMetaObjectRecordDataHierarchyMutableRef::AppendPredefinedValue(const ibPredefinedItemDescription& item)
{
	const bool done = Command(this, ibProtocol::ibSchema::ibCommand::Create, ItemArgs(item));
	ReadPredefinedValues();
	return done;
}

bool ibValueMetaObjectRecordDataHierarchyMutableRef::SetPredefinedValue(const ibPredefinedItemDescription& item)
{
	const bool done = Command(this, ibProtocol::ibSchema::ibCommand::Set, ItemArgs(item));
	ReadPredefinedValues();
	return done;
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

	ibDataValue declares;
	m_predefinedDesc = ibPredefinedDescription();
	if (ibReadProtocolValue(shown, ibProtocol::ibSchema::ibPredefined::Declares, declares))
		ibPredefinedDescriptionMemory::ReadNode(declares, m_predefinedDesc);
	ibValue::ReadClasses(shown.FindChild(ibProtocol::ibSchema::ibPredefined::Classes));

	// The schema's items as rows of the tree; the parent is linked once every row is made, as an item may stand under a
	// later one.
	for (const ibPredefinedItemDescription& item : m_predefinedDesc.m_items)
		m_predefinedObjectVector.emplace_back(new ibPredefinedItem(item));
	for (const wxObjectDataPtr<ibPredefinedItem>& row : m_predefinedObjectVector)
		row->m_valueParent = FindPredefinedValue(row->m_item.m_parent);
}
