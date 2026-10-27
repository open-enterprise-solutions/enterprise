#include "objectText.h"

#include "backend/metadataConfiguration.h"
#include "backend/session/session.h"
#include "core/serialize/dataBuilder.h"

#include "frmserver/schema/designer/metaData.h"   // ibSchemaMetaData — whose tree, which object

bool ibSchemaObjectText::AccessRight(const ibSession& session) const
{
	const ibMetaDataConfigurationBase* const metaData = session.GetMetaData();
	return metaData != nullptr && metaData->AccessRight_Administration();
}

void ibSchemaObjectText::Build(const ibSession& session, const ibDataNode& args, ibDataNode& result) const
{
	ibProtocol::ibRefusal refusal = ibProtocol::ibRefusal::None;
	ibString error;
	ibMetaData* const metaData = ibSchemaMetaData::FindMetaData(session, args, refusal, error);
	const ibValueMetaObject* const object = metaData != nullptr
		? ibSchemaMetaData::FindObject(metaData, args, ibProtocol::ibSchema::ibObjectText::Id, refusal, error) : nullptr;
	if (object == nullptr)
		return;

	result.SetValue(ibProtocol::ibSchema::ibObjectText::Help, object->GetHelpContent());
	result.SetValue(ibProtocol::ibSchema::ibObjectText::Note, object->GetNoteContent());
}

std::function<void()> ibSchemaObjectText::Command(const ibSession& session, std::int32_t command,
	const ibDataNode& args, ibProtocol::ibRefusal& refusal, ibString& error) const
{
	if (static_cast<ibProtocol::ibSchema::ibCommand>(command) != ibProtocol::ibSchema::ibCommand::Save) {
		refusal = ibProtocol::ibRefusal::BadParameter;
		error = ibT("an object's texts take Save alone");
		return nullptr;
	}

	ibMetaData* const metaData = ibSchemaMetaData::FindMetaData(session, args, refusal, error);
	ibValueMetaObject* const object = metaData != nullptr
		? ibSchemaMetaData::FindObject(metaData, args, ibProtocol::ibSchema::ibObjectText::Id, refusal, error) : nullptr;
	if (object == nullptr)
		return nullptr;

	// …the one given, and the configuration modified by it — the desktop's editor marked its tree (EditOneText).
	if (args.Has(ibProtocol::ibSchema::ibObjectText::Help)) {
		const ibString text = args.GetValue<ibString>(ibProtocol::ibSchema::ibObjectText::Help);
		return [metaData, object, text]() { object->SetHelpContent(text); metaData->Modify(true); };
	}
	if (args.Has(ibProtocol::ibSchema::ibObjectText::Note)) {
		const ibString text = args.GetValue<ibString>(ibProtocol::ibSchema::ibObjectText::Note);
		return [metaData, object, text]() { object->SetNoteContent(text); metaData->Modify(true); };
	}

	refusal = ibProtocol::ibRefusal::BadParameter;
	error = ibString::Format(ibT("which text of '%s': Help or Note"), object->GetName());
	return nullptr;
}
