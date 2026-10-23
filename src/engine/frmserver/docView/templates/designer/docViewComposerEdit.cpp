#include "docViewComposerEdit.h"

#include "backend/metaData.h"
#include "backend/metaCollection/metaComposerObject.h"   // the composer — its description, its configuration
#include "backend/query/queryConstructorModel.h"         // ibQueryFieldsOfText — the fields its query offers
#include "backend/query/queryableFactory.h"              // a list's main table — its descriptor
#include "backend/srcDataObject.h"                       // …which describes itself (ibSourceExplorer)
#include "core/description/compositionDescription.h"     // ibCompositionDescriptionMemory — the node form
#include "core/serialize/dataBuilder.h"
#include "frmserver/win/dlgs/settings/settingsRequest.h" // ibWriteSettingsField / References — a field as the windows read it
#include "queryConstructorRequest.h"                      // ibFetchQueryConstructorRequest — the query constructor over its query

#include <vector>

namespace {

// WHAT A QUERY TEXT OFFERS — written as the settings windows' questions write it (ComposerSettings: no Id, a field of a
// parsed text stands behind no attribute), with the parser's complaint when it offers none.
void WriteQueryFields(ibDataNode& into, const wxString& text, const ibMetaData* metaData)
{
	wxString error;
	ibDataNode& fields = into.Child(ibProtocol::ibView::ibComposer::Fields);
	std::vector<ibTypeDescription> types;
	for (const ibQueryConstructorField& field : ibQueryFieldsOfText(text, metaData, &error)) {
		ibWriteSettingsField(fields, field.m_name, field.m_caption, wxNOT_FOUND, field.m_type, field.m_available,
			ibPictureDescription(), metaData);
		types.push_back(field.m_type);
	}
	ibWriteSettingsReferences(into.Child(ibProtocol::ibView::ibComposer::References), types, metaData);
	if (!error.IsEmpty())
		into.SetValue(ibProtocol::ibView::ibComposer::Error, error);
}


// WHAT A DYNAMIC LIST OFFERS — the desktop's list settings window's answer (ibListSettingsPanel::BindFieldSource): the
// fields of the table the list IS, and what its query adds on top; a name the table already offers is not added twice.
void WriteListSourceFields(ibDataNode& into, ibMetaID mainTable, const wxString& text, const ibMetaData* metaData)
{
	ibDataNode& fields = into.Child(ibProtocol::ibView::ibComposer::Fields);
	std::vector<ibTypeDescription> types;
	std::vector<wxString> names;

	ibQueryableFactory* const factory = metaData != nullptr ? metaData->GetSourceFactory() : nullptr;
	if (const ibQueryableSourceDescriptor* const descriptor = (factory != nullptr && mainTable != wxNOT_FOUND)
		? factory->ResolveDescriptorById(mainTable) : nullptr) {
		ibSourceDataObject::ibSourceExplorer explorer;
		descriptor->FillSourceExplorer(explorer);
		for (unsigned int i = 0; i < explorer.GetHelperCount(); ++i) {
			const ibSourceDataObject::ibSourceExplorer* const node = explorer.GetHelper(i);
			if (node == nullptr || node->IsTableSection())
				continue;
			ibWriteSettingsField(fields, node->GetSourceName(), node->GetSourceSynonym(),
				static_cast<ibMetaID>(node->GetSourceId()), node->GetTypeValueDesc(),
				node->GetColumn() == nullptr || node->GetColumn()->IsAvailable(), node->GetSourcePicture(), metaData);
			types.push_back(node->GetTypeValueDesc());
			names.push_back(node->GetSourceName());
		}
	}

	wxString error;
	if (!text.IsEmpty()) {
		for (const ibQueryConstructorField& field : ibQueryFieldsOfText(text, metaData, &error)) {
			bool already = false;
			for (const wxString& name : names)
				already = already || name.IsSameAs(field.m_name, false);
			if (already)
				continue;
			ibWriteSettingsField(fields, field.m_name, field.m_caption, wxNOT_FOUND, field.m_type, field.m_available,
				ibPictureDescription(), metaData);
			types.push_back(field.m_type);
		}
	}
	ibWriteSettingsReferences(into.Child(ibProtocol::ibView::ibComposer::References), types, metaData);
	if (!error.IsEmpty())
		into.SetValue(ibProtocol::ibView::ibComposer::Error, error);
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
	ibDataNode& state = frame.Child(ibProtocol::ibView::State);
	state.SetValue(ibProtocol::ibView::ibComposer::ReadOnly, !metaComposer->IsEditable());

	const ibCompositionDescription& composition = metaComposer->GetCompositionDesc();
	ibCompositionDescriptionMemory::WriteNode(frame.Child(ibProtocol::ibView::ibComposer::Composition), composition);
	WriteQueryFields(frame, composition.m_query, metaComposer->GetMetaData());
}

bool ibComposerCompositionView::Fetch(const ibDataNode& request, ibDataNode& response)
{
	const ibValueMetaObjectComposer* const metaComposer =
		GetDocument()->ConvertMetaObjectToType<ibValueMetaObjectComposer>();
	return metaComposer != nullptr
		&& (ibFetchQueryConstructorRequest(request, response, metaComposer->GetMetaData())
		    || ibFetchComposerRequest(request, response, metaComposer->GetMetaData()));
}

bool ibComposerCompositionView::OnClientEvent(ibProtocol::ibEvent::ibKind event, const ibDataNode& args)
{
	ibMetaDocument* const document = GetDocument();
	ibValueMetaObjectComposer* const metaComposer = document->ConvertMetaObjectToType<ibValueMetaObjectComposer>();
	if (metaComposer == nullptr || !metaComposer->IsEditable())
		return false;

	switch (event) {
	case ibProtocol::ibEvent::ibKind::Input:
		document->Modify(true);
		return true;
	case ibProtocol::ibEvent::ibKind::Change: {
		const ibDataNode* const node = args.FindChild(ibProtocol::ibView::ibComposer::Composition);
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
	switch (static_cast<ibProtocol::ibView::ibComposer::ibRequest>(request.GetValue<std::int32_t>(ibProtocol::ibView::ibComposer::Kind))) {
	case ibProtocol::ibView::ibComposer::ibRequest::Fields:
		WriteQueryFields(response, request.GetValue<wxString>(ibProtocol::ibView::ibComposer::Text), metaData);
		return true;
	case ibProtocol::ibView::ibComposer::ibRequest::Expand: {
		std::vector<ibMetaID> targets;
		if (const ibDataValue* const listed = request.FindField(ibProtocol::ibView::ibComposer::Targets)) {
			if (listed->Kind() == ibDataKind::Array)
				for (const ibDataValue& id : listed->AsArray())
					targets.push_back(static_cast<ibMetaID>(id.AsInt()));
		}
		ibWriteSettingsTargetFields(response.Child(ibProtocol::ibView::ibComposer::Expanded), response.Child(ibProtocol::ibView::ibComposer::References),
			targets, metaData);
		return true;
	}
	case ibProtocol::ibView::ibComposer::ibRequest::Source:
		WriteListSourceFields(response, request.FindField(ibProtocol::ibView::ibComposer::Id) != nullptr
			? static_cast<ibMetaID>(request.GetValue<std::int32_t>(ibProtocol::ibView::ibComposer::Id)) : static_cast<ibMetaID>(wxNOT_FOUND),
			request.GetValue<wxString>(ibProtocol::ibView::ibComposer::Text), metaData);
		return true;
	default:
		return false;
	}
}
