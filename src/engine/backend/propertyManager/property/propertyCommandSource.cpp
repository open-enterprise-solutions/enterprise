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

#include "protocol/protocol.h"   // ibProtocol::ibSchema::ibProperties — the row's keys

bool ibPropertyCommandSource::GetProtocolValue(ibDataNode& row, ibReader reader)
{
	if (!ibProperty::GetProtocolValue(row, reader))
		return false;
	if (const ibBackendCommandReceiver* const receiver = get_cell_variant<ibVariantCommandSource>()->GetOwner())
		receiver->WriteCommandTree(row.Child(ibProtocol::ibSchema::ibProperties::CommandTree));
	return true;
}
