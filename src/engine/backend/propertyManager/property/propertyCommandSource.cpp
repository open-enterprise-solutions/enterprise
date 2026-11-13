#include "propertyCommandSource.h"
#include "backend/propertyManager/property/variant/variantCommandSource.h"   // ibVariantCommandSource (holds the hop path)
#include "backend/backend_command.h"          // ibBackendCommandReceiver — the factory the variant casts to (+ dtor anchor)
#include "core/serialize/dataBuilder.h"   // ibDataValue — node value (Binary blob of the hop path)

// The command-source factory interface's vtable anchor — see backend_command.h (one exported vtable + typeinfo in
// backend.dll so the variant's cross-DLL dynamic_cast to it resolves the frontend button / bar item).
ibBackendCommandReceiver::~ibBackendCommandReceiver() {}

////////////////////////////////////////////////////////////////////////

ibVariant ibPropertyCommandSource::CreateVariantData(const ibPropertyObject* property, const ibCommandDescription& desc, const ibString& display)
{
	// The owning button / bar item IS-A command-source factory (WalkCommand validates the binding) — the variant keeps
	// it and checks existence SERVER-SIDE (runtime too), exactly as ibVariantSource keeps its source factory.
	const ibBackendCommandReceiver* owner = dynamic_cast<const ibBackendCommandReceiver*>(property);
	return ibVariant(new ibVariantCommandSource(owner, desc, display));
}

////////////////////////////////////////////////////////////////////////

ibCommandDescription& ibPropertyCommandSource::GetValueAsCommandDesc() const
{
	return get_cell_variant<ibVariantCommandSource>()->GetCommandDesc();
}

ibString ibPropertyCommandSource::GetValueAsString() const
{
	return m_propValue.GetString();
}

void ibPropertyCommandSource::SetValue(const ibCommandDescription& desc, const ibString& display)
{
	m_propValue = CreateVariantData(GetPropertyObject(), desc, display);
}

////////////////////////////////////////////////////////////////////////

bool ibPropertyCommandSource::IsEmptyProperty() const
{
	return get_cell_variant<ibVariantCommandSource>()->IsEmptyCommand();
}

bool ibPropertyCommandSource::SetDataValue(const ibValue& /*varPropVal*/)
{
	return false;   // a command source is bound + run by the button, never assigned as a script value
}

bool ibPropertyCommandSource::GetDataValue(ibValue& /*pvarPropVal*/) const
{
	return false;
}

////////////////////////////////////////////////////////////////////////
// Load & save — the DUMB command-hop path, straight into the variant's desc. Identical shape to
// ibPropertySource::Read/WriteNodeValue (one line each — GetValueAsCommandDesc is the mutable ref).

bool ibPropertyCommandSource::ReadNodeValue(const ibDataValue& value)
{
	return ibCommandDescriptionMemory::ReadNode(value, GetValueAsCommandDesc());
}

bool ibPropertyCommandSource::WriteNodeValue(ibDataValue& value) const
{
	return ibCommandDescriptionMemory::WriteNode(value, GetValueAsCommandDesc());
}

#include "backend/metaData.h"                           // FindAnyObjectByFilter / MetaIdByGuid — a command by id, back by guid
#include "backend/metaCollection/metaCommandObject.h"   // ibValueMetaObjectCommand — what a hop names
#include "core/memory/memoryStream.h"                   // ibReaderMemory / ibWriterMemory — the guid-keyed blob

// COPY / PASTE — ibPropertySource's pair on the command side. A hop is asked as the walk asks it
// (ibValueForm::GetCommandByHop): a COMMAND, config-wide, by its id; one that is rides its guid — GetCommonGuid, the
// copy-guid while its object is being copied — and the paste resolves it back through MetaIdByGuid onto the pasted
// command. Every other hop stays as it is: the form's own command (its ids are a range of their own,
// kFormCommandIdBase), a control, a standard action.
static const unsigned char kHopRaw = 0, kHopGuid = 1;

bool ibPropertyCommandSource::CopyNodeValue(ibDataValue& value) const
{
	const ibPropertyObject* const owner = m_owner;   // CONST: the non-const GetMetaData wxFAILs
	const ibMetaData* const metaData = owner != nullptr ? owner->GetMetaData() : nullptr;
	const ibCommandDescription& desc = GetValueAsCommandDesc();
	ibWriterMemory writer;
	writer.w_u32((unsigned int)desc.GetHopCount());
	for (const ibCommandHop& hop : desc.GetPath()) {
		const ibValueMetaObjectCommand* const command = metaData != nullptr
			? metaData->FindAnyObjectByFilter<ibValueMetaObjectCommand>(hop.m_id, { g_metaCommonCommandCLSID, g_metaCommandCLSID }, true)
			: nullptr;
		if (command != nullptr) { writer.w_u8(kHopGuid); writer.w_stringZ(command->GetCommonGuid().str()); }
		else                    { writer.w_u8(kHopRaw);  writer.w_u32((unsigned int)hop.m_id); }
	}
	writer.w_u32((unsigned int)desc.GetCommandType());
	value = ibDataValue::Binary(writer.buffer());
	return true;
}

bool ibPropertyCommandSource::PasteNodeValue(const ibDataValue& value)
{
	const ibMemoryBuffer& data = value.AsBinary();
	if (data.GetDataLen() == 0)
		return true;
	const ibPropertyObject* const owner = m_owner;
	const ibMetaData* const metaData = owner != nullptr ? owner->GetMetaData() : nullptr;
	ibCommandDescription& desc = GetValueAsCommandDesc();
	desc.ClearCommand();
	ibReaderMemory reader(data);
	if (reader.elapsed() < static_cast<int>(sizeof(std::uint32_t)))
		return true;
	const unsigned int count = reader.r_u32();
	for (unsigned int i = 0; i < count; i++) {
		// A hop is variable-length (a tag, then a guid string or a raw id): a blob shorter than its count stops with
		// the hops it really had, as ibPropertySource::PasteNodeValue does.
		if (reader.eof())
			break;
		if (reader.r_u8() == kHopGuid) {
			const ibGuid guid(reader.r_stringZ());
			desc.AppendCommand(metaData != nullptr ? metaData->MetaIdByGuid(guid) : ibMetaID(ibNOT_FOUND));   // the pasted command's
		}
		else {
			desc.AppendCommand((ibMetaID)reader.r_u32());
		}
	}
	if (!reader.eof())
		desc.SetCommandType((ibInterfaceCommandType)reader.r_u32());
	return true;
}

#include "protocol/protocolDesigner.h"   // ibProtocol::ibSchema::ibProperties — the row's keys

bool ibPropertyCommandSource::GetProtocolValue(ibDataNode& row, ibReader reader)
{
	if (!ibProperty::GetProtocolValue(row, reader))
		return false;
	if (const ibBackendCommandReceiver* const receiver = get_cell_variant<ibVariantCommandSource>()->GetOwner())
		receiver->WriteCommandTree(row.Child(ibProtocol::ibSchema::ibProperties::CommandTree));
	return true;
}
