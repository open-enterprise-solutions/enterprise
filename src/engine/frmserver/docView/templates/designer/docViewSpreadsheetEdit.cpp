#include "docViewSpreadsheetEdit.h"
#include "docViewComposerEdit.h"   // a composer template's view

#include "backend/metaCollection/metaTemplateObject.h"
#include "core/description/spreadsheetDescription.h"   // ibSpreadsheetDescriptionMemory — the sheet as it is stored
#include "core/serialize/dataBuilder.h"

#include "frmserver/visualView/ctrl/gridBox.h"   // g_controlGridBoxCLSID — the node a client draws a sheet from

// ----------------------------------------------------------------------------
// ibSpreadsheetTemplateView implementation
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(ibSpreadsheetTemplateView, ibMetaView);

void ibSpreadsheetTemplateView::OnDraw(ibDataNode& frame)
{
	// A GRIDBOX'S NODE, as the file document's view draws it — its grid lines shown, and its areas: the desktop's
	// editor showed them for a template (ibSpreadsheetEditView::OnCreate).
	frame.SetClsid(g_controlGridBoxCLSID);
	ibDataNode& state = frame.Child(ibProtocol::ibView::State);
	state.SetValue(ibProtocol::ibForm::ibSpreadsheet::GridLines, true);
	state.SetValue(ibProtocol::ibForm::ibSpreadsheet::GridArea, true);
}

bool ibSpreadsheetTemplateView::Fetch(const ibDataNode& /*request*/, ibDataNode& response)
{
	const ibValueMetaObjectTemplateBase* const creator =
		GetDocument()->ConvertMetaObjectToType<ibValueMetaObjectTemplateBase>();
	if (creator == nullptr)
		return false;

	ibDataValue sheet;
	if (!ibSpreadsheetDescriptionMemory::WriteNode(sheet, creator->GetTemplateDesc().GetSpreadsheetDesc()))
		return false;
	response.SetField(ibProtocol::ibForm::ibSpreadsheet::Sheet, sheet);
	// …and read-only where the configuration may not be edited, as the desktop's editor was.
	if (!creator->IsEditable())
		response.SetValue(ibProtocol::ibForm::ibSpreadsheet::ReadOnly, true);
	return true;
}

bool ibSpreadsheetTemplateView::OnClientEvent(ibProtocol::ibEvent::ibKind event, const ibDataNode& args)
{
	ibMetaDocument* const document = GetDocument();
	ibValueMetaObjectTemplateBase* const creator =
		document->ConvertMetaObjectToType<ibValueMetaObjectTemplateBase>();
	if (creator == nullptr)
		return false;

	switch (event) {
	case ibProtocol::ibEvent::ibKind::Cell:
		// A cell clicked is the edit's, as on the desktop's sheet being edited.
		return true;
	case ibProtocol::ibEvent::ibKind::Change: {
		ibSpreadsheetDescription sheet;
		if (!creator->IsEditable() || !ibSpreadsheetDescriptionMemory::ReadNode(args.GetProperty(ibProtocol::ibForm::ibSpreadsheet::Sheet), sheet))
			return false;
		// Where the desktop's editor wrote — the metaobject's sheet — and the configuration modified (ibMetaDocument::Modify).
		creator->SetTemplateDesc(sheet);
		document->Modify(true);
		return true;
	}
	default:
		return false;
	}
}

// ----------------------------------------------------------------------------
// ibTemplateDocument implementation
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(ibTemplateDocument, ibMetaDocument);

ibView* ibTemplateDocument::DoCreateView()
{
	const ibValueMetaObjectTemplateBase* const creator = ConvertMetaObjectToType<ibValueMetaObjectTemplateBase>();
	if (creator != nullptr && creator->GetTemplateDesc().GetType() == ibTemplateType::Composer)
		return new ibComposerCompositionView();
	return ibMetaDocument::DoCreateView();
}
