#ifndef _DESIGNER_SPREADSHEET_EDIT_H__
#define _DESIGNER_SPREADSHEET_EDIT_H__

// A TEMPLATE AS THE DESIGNER EDITS IT — the desktop designer's ibSpreadsheetEditDocument and ibSpreadsheetEditView
// (frontend/docView/templates/docViewSpreadsheet.h) over a template metaobject: its sheet in a tab, edited where it is
// kept — the metaobject — so what is modified and saved is the configuration. The sheet goes as it is stored: a
// parameter's cell as its name, not filled in (the file document's view fills it, ibFetchSpreadsheet), so what comes
// back is the template and not one filling of it.

#include "frmserver/docView/docView.h"

// The sheet of the template, drawn as a gridbox draws its own — its areas shown, as the desktop's editor showed them.
class FRMSERVER_API ibSpreadsheetTemplateView : public ibMetaView {
public:

	ibSpreadsheetTemplateView() : ibMetaView() {}

	virtual void OnDraw(ibDataNode& frame) override;
	virtual ibProtocolViewKind GetViewKind() const override { return ibProtocolViewKind::Spreadsheet; }
	virtual bool Fetch(const ibDataNode& request, ibDataNode& response) override;
	// The sheet the person edited on the client, handed over (Change {Sheet}) — the template's from then on.
	virtual bool OnClientEvent(ibProtocolEvent event, const ibDataNode& args) override;

	wxDECLARE_DYNAMIC_CLASS(ibSpreadsheetTemplateView);
};

// Its document — the template metaobject's (ibMetaDocument::GetMetaObject); nothing of its own to hold.
class FRMSERVER_API ibSpreadsheetTemplateDocument : public ibMetaDocument {
public:

	ibSpreadsheetTemplateDocument() : ibMetaDocument() {}

	wxDECLARE_NO_COPY_CLASS(ibSpreadsheetTemplateDocument);
	wxDECLARE_DYNAMIC_CLASS(ibSpreadsheetTemplateDocument);
};

#endif
