#include "docViewComposerEdit.h"

#include "backend/metaData.h"
#include "backend/metaCollection/metaComposerObject.h"   // the composer — its description, its configuration
#include "backend/query/queryConstructorModel.h"         // ibQueryFieldsOfText — the fields its query offers
#include "core/description/compositionDescription.h"     // ibCompositionDescriptionMemory — the node form
#include "core/serialize/dataBuilder.h"
#include "frmserver/win/dlgs/settings/settingsRequest.h" // ibWriteSettingsField / References — a field as the windows read it

#include <vector>

namespace {

// WHAT A QUERY TEXT OFFERS — written as the settings windows' questions write it (ComposerSettings: no Id, a field of a
// parsed text stands behind no attribute), with the parser's complaint when it offers none.
void WriteQueryFields(ibDataNode& into, const wxString& text, const ibMetaData* metaData)
{
	wxString error;
	ibDataNode& fields = into.Child(ibProtocolName::Fields);
	std::vector<ibTypeDescription> types;
	for (const ibQueryConstructorField& field : ibQueryFieldsOfText(text, metaData, &error)) {
		ibWriteSettingsField(fields, field.m_name, field.m_caption, wxNOT_FOUND, field.m_type, field.m_available, metaData);
		types.push_back(field.m_type);
	}
	ibWriteSettingsReferences(into.Child(ibProtocolName::References), types, metaData);
	if (!error.IsEmpty())
		into.SetValue(ibProtocolName::Error, error);
}

} // namespace

// ----------------------------------------------------------------------------
// ibComposerCompositionView implementation
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(ibComposerCompositionView, ibMetaView);

void ibComposerCompositionView::OnDraw(ibDataNode& frame)
{
	const ibValueMetaObjectComposer* const metaComposer =
		GetDocument()->ConvertMetaObjectToType<ibValueMetaObjectComposer>();
	if (metaComposer == nullptr)
		return;

	// Read-only where the configuration may not be edited — the desktop's panel took SetReadOnly(ibDOC_READONLY).
	ibDataNode& state = frame.Child(ibProtocolName::State);
	state.SetValue(ibProtocolName::ReadOnly, !metaComposer->IsEditable());

	const ibCompositionDescription& composition = metaComposer->GetCompositionDesc();
	ibCompositionDescriptionMemory::WriteNode(frame.Child(ibProtocolName::Composition), composition);
	WriteQueryFields(frame, composition.m_query, metaComposer->GetMetaData());
}

bool ibComposerCompositionView::Fetch(const ibDataNode& request, ibDataNode& response)
{
	const ibValueMetaObjectComposer* const metaComposer =
		GetDocument()->ConvertMetaObjectToType<ibValueMetaObjectComposer>();
	return metaComposer != nullptr && ibFetchComposerRequest(request, response, metaComposer->GetMetaData());
}

bool ibComposerCompositionView::OnClientEvent(ibProtocolEvent event, const ibDataNode& args)
{
	ibMetaDocument* const document = GetDocument();
	ibValueMetaObjectComposer* const metaComposer = document->ConvertMetaObjectToType<ibValueMetaObjectComposer>();
	if (metaComposer == nullptr || !metaComposer->IsEditable())
		return false;

	switch (event) {
	case ibProtocolEvent::Input:
		document->Modify(true);
		return true;
	case ibProtocolEvent::Change: {
		const ibDataNode* const node = args.FindChild(ibProtocolName::Composition);
		ibCompositionDescription composition;
		if (node == nullptr || !ibCompositionDescriptionMemory::ReadNode(*node, composition))
			return false;
		// Where the desktop's panel wrote — the composer's own description — and the configuration modified
		// (ibComposerEditor::MarkModified).
		metaComposer->SetCompositionDesc(composition);
		document->Modify(true);
		return true;
	}
	default:
		return false;
	}
}

// ----------------------------------------------------------------------------
// ibComposerCompositionDocument implementation
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(ibComposerCompositionDocument, ibMetaDocument);

// ----------------------------------------------------------------------------
// ibFetchComposerRequest
// ----------------------------------------------------------------------------

bool ibFetchComposerRequest(const ibDataNode& request, ibDataNode& response, const ibMetaData* metaData)
{
	switch (static_cast<ibProtocolComposerRequest>(request.GetValue<s32>(ibProtocolName::Kind))) {
	case ibProtocolComposerRequest::Fields:
		WriteQueryFields(response, request.GetValue<wxString>(ibProtocolName::Text), metaData);
		return true;
	case ibProtocolComposerRequest::Expand: {
		std::vector<ibMetaID> targets;
		if (const ibDataValue* const listed = request.FindField(ibProtocolName::Targets)) {
			if (listed->Kind() == ibDataKind::Array)
				for (const ibDataValue& id : listed->AsArray())
					targets.push_back(static_cast<ibMetaID>(id.AsInt()));
		}
		ibWriteSettingsTargetFields(response.Child(ibProtocolName::Expanded), response.Child(ibProtocolName::References),
			targets, metaData);
		return true;
	}
	default:
		return false;
	}
}
