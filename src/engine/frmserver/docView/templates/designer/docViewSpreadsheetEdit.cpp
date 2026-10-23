#include "docViewSpreadsheetEdit.h"

#include "backend/metaCollection/metaSpreadsheetObject.h"
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
	const ibValueMetaObjectSpreadsheetBase* const creator =
		GetDocument()->ConvertMetaObjectToType<ibValueMetaObjectSpreadsheetBase>();
	if (creator == nullptr)
		return false;

	ibDataValue sheet;
	if (!ibSpreadsheetDescriptionMemory::WriteNode(sheet, creator->GetSpreadsheetDesc()))
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
	ibValueMetaObjectSpreadsheetBase* const creator =
		document->ConvertMetaObjectToType<ibValueMetaObjectSpreadsheetBase>();
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
		creator->SetSpreadsheetDesc(sheet);
		document->Modify(true);
		return true;
	}
	default:
		return false;
	}
}

// ----------------------------------------------------------------------------
// ibSpreadsheetTemplateDocument implementation
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(ibSpreadsheetTemplateDocument, ibMetaDocument);
