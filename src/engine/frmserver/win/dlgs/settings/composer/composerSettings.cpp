#include "composerSettings.h"

#include "core/description/compositionDescription.h"
#include "backend/spreadsheetModel.h"                    // the model's composer — the setting in force, and the one set
#include "backend/system/value/valueDataComposition.h"   // the composition the model is: what it arranges, its configuration
#include "backend/query/queryConstructorModel.h"         // ibQueryFieldsOfText — the fields its query offers
#include "core/serialize/dataBuilder.h"

#include "frmserver/win/dlgs/settings/settingsRequest.h"   // ibRequestSettings — the window, asked until it answers

#include "protocol/protocol.h"   // ibProtocolRequestKind::ComposerSettings

namespace {

// THE WINDOW'S QUESTION — the composition it arranges, and the fields its query offers: the desktop's panel reads them of
// the text and the configuration (ibQueryFieldsOfText), and the configuration is here. Each reads as its source calls it.
ibDataNode SettingsRequest(const ibCompositionDescription& composition, const ibMetaData* metaData)
{
	ibDataNode request;
	request.SetValue(ibProtocolName::Kind, static_cast<s32>(ibProtocolRequestKind::ComposerSettings));
	ibCompositionDescriptionMemory::WriteNode(request.Child(ibProtocolName::Composition), composition);

	ibDataNode& fields = request.Child(ibProtocolName::Fields);
	std::vector<ibTypeDescription> types;
	for (const ibQueryConstructorField& field : ibQueryFieldsOfText(composition.m_query, metaData)) {
		// NO metaID — a field of a parsed text stands behind no metaobject attribute (the desktop's BindFieldSource).
		ibWriteSettingsField(fields, field.m_name, field.m_caption, wxNOT_FOUND, field.m_type, field.m_available, metaData);
		types.push_back(field.m_type);
	}
	ibWriteSettingsReferences(request.Child(ibProtocolName::References), types, metaData);
	return request;
}

} // namespace

bool ibDialogComposerSettings::ShowUserSettings(ibValueSpreadsheetModel* model)
{
	if (model == nullptr)
		return false;

	ibValueDataComposition* composition = dynamic_cast<ibValueDataComposition*>(model);
	if (composition == nullptr)
		return false;   // a drawn document describes nothing — there is no setting to arrange

	// ⭐ FIRST, THE COMPOSITION IS ASKED TO RESOLVE ITSELF — the author's variants reach the composer when the source is
	// built, and until then what composes is empty (the desktop's).
	composition->EnsureSourceBuilt();

	// THE COPY — what Cancel drops, and what OK becomes: the reader's where they set one, the author's where they did not.
	ibSettingsDescription edited = model->GetModelComposer().GetCurrentSettingsDesc();
	const ibMetaData* const metaData = composition->GetMetaData();
	if (!ibRequestSettings(SettingsRequest(composition->GetCompositionDesc(), metaData), edited, metaData))
		return false;

	// ⭐⭐ WHAT THE WINDOW EDITED IS WHAT IS SAVED — the composer's user section.
	model->GetModelComposer().SetUserSettingsDesc(edited);
	return true;
}
