#include "listSettings.h"

#include "backend/compositionDescription.h"
#include "core/serialize/dataBuilder.h"
#include "backend/tabularModel.h"          // ibValueModel + ibValueModelColumnCollection (the flat field source)

#include "frmserver/win/dlgs/settings/settingsRequest.h"   // ibRequestSettings — the window, asked until it answers

#include "protocol/protocol.h"   // ibProtocolRequestKind::ListSettings

namespace {

// THE WINDOW'S QUESTION — the fields it offers.
ibDataNode SettingsRequest(ibValueModel* model, const ibMetaData* metaData)
{
	ibDataNode request;
	request.SetValue(ibProtocolName::Kind, static_cast<s32>(ibProtocolRequestKind::ListSettings));

	// 🛑 AND THE COLUMNS ARE DESCRIBED FOR EVERY MODEL, not only for the ones with no description — the desktop's: a
	// list whose source is a METAOBJECT carries no query text to parse fields out of; its fields are its columns.
	ibDataNode& fields = request.Child(ibProtocolName::Fields);
	std::vector<ibTypeDescription> types;
	if (ibValueModel::ibValueModelColumnCollection* columns = model->GetColumnCollection()) {
		for (unsigned int i = 0; i < columns->GetColumnCount(); ++i) {
			const auto* col = columns->GetColumnInfo(i);
			if (col == nullptr)
				continue;
			// …named by how it reads (its caption), as the reader's window speaks; the path stays its name.
			ibWriteSettingsField(fields, col->GetColumnName(), col->GetColumnCaption(), static_cast<ibMetaID>(col->GetColumnID()),
				col->GetColumnTypeValue(), col->IsColumnAvailable(), metaData);
			types.push_back(col->GetColumnTypeValue());
		}
	}
	ibWriteSettingsReferences(request.Child(ibProtocolName::References), types, metaData);
	return request;
}

} // namespace

bool ibDialogListSettings::ShowUserSettings(ibValueModel* model, const ibMetaData* metaData)
{
	if (model == nullptr)
		return false;

	// ⭐⭐ TAKE THE SETTING IN FORCE — a COPY of it: the reader's own if they have set one, the author's if they have not
	// (GetCurrentSettingsDesc answers exactly that).
	ibSettingsDescription edited = model->GetModelComposer().GetCurrentSettingsDesc();
	if (!ibRequestSettings(SettingsRequest(model, metaData), edited, metaData))
		return false;   // …and Cancel leaves the composer exactly as it was: the copy is dropped

	// …AND ON OK IT BECOMES THE COMPOSER'S USER SECTION, which is what the next open will hand back. Then the model
	// re-reads, because a setting that is not read is not shown.
	model->GetModelComposer().SetUserSettingsDesc(edited);
	model->RefetchAll();
	return true;
}
