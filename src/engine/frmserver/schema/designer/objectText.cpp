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
	ibProtocolRefusal refusal = ibProtocolRefusal::None;
	wxString error;
	ibMetaData* const metaData = ibSchemaMetaData::FindMetaData(session, args, refusal, error);
	const ibValueMetaObject* const object = metaData != nullptr
		? ibSchemaMetaData::FindObject(metaData, args, wxT("Id"), refusal, error) : nullptr;
	if (object == nullptr)
		return;

	result.SetValue(wxT("Help"), object->GetHelpContent());
	result.SetValue(wxT("Note"), object->GetNoteContent());
}

std::function<void()> ibSchemaObjectText::Command(const ibSession& session, s32 command,
	const ibDataNode& args, ibProtocolRefusal& refusal, wxString& error) const
{
	if (static_cast<ibProtocolSchemaCommand>(command) != ibProtocolSchemaCommand::Save) {
		refusal = ibProtocolRefusal::BadParameter;
		error = wxT("an object's texts take Save alone");
		return nullptr;
	}

	ibMetaData* const metaData = ibSchemaMetaData::FindMetaData(session, args, refusal, error);
	ibValueMetaObject* const object = metaData != nullptr
		? ibSchemaMetaData::FindObject(metaData, args, wxT("Id"), refusal, error) : nullptr;
	if (object == nullptr)
		return nullptr;

	// …the one given, and the configuration modified by it — the desktop's editor marked its tree (EditOneText).
	if (args.FindField(wxT("Help")) != nullptr) {
		const wxString text = args.GetValue<wxString>(wxT("Help"));
		return [metaData, object, text]() { object->SetHelpContent(text); metaData->Modify(true); };
	}
	if (args.FindField(wxT("Note")) != nullptr) {
		const wxString text = args.GetValue<wxString>(wxT("Note"));
		return [metaData, object, text]() { object->SetNoteContent(text); metaData->Modify(true); };
	}

	refusal = ibProtocolRefusal::BadParameter;
	error = wxString::Format(wxT("which text of '%s': Help or Note"), object->GetName());
	return nullptr;
}
