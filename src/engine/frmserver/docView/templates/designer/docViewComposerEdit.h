#ifndef _DESIGNER_COMPOSER_EDIT_H__
#define _DESIGNER_COMPOSER_EDIT_H__

// A REPORT'S COMPOSER AS THE DESIGNER EDITS IT — the desktop designer's ibComposerEditDocument and ibComposerEditView
// (designer/docManager/templates/docViewComposer.h): a tab of its own over the composer metaobject, whose content — the
// query, the resources, the parameters, the structure and its settings — is edited by the composition panel
// (ibComposerSettingsPanel). The panel is the client's (ViewKind Composer); this view says what it edits — the
// composition in its node form and the fields its query offers — and takes back what was edited (Change {Composition}).
// Not the desktop's names — see docViewModuleEdit.h.

#include "frmserver/docView/docView.h"

class ibListSettingsChoice;

class FRMSERVER_API ibComposerCompositionView : public ibMetaView {
public:
	ibComposerCompositionView() : ibMetaView() {}
	virtual ~ibComposerCompositionView();

	// View.State {ReadOnly, Chosen — a value chosen for a cell}, View.Composition — the composer's description,
	// View.Fields and View.References — what its query offers, as the settings windows have them; View.Error — why the
	// query offers none; View.Classes — what its type cells name.
	virtual void OnDraw(ibDataNode& frame) override;
	virtual ibProtocol::ibView::ibKind GetViewKind() const override { return ibProtocol::ibView::ibKind::Composer; }

	// fetch {Tab, Request: {Kind: Fields, Text}} — the fields a query text offers, while it is being edited on the
	// panel's Query tab (the desktop's panel read them of the text and the configuration, which are here); the rest of
	// what the panel asks — ibFetchComposerRequest.
	virtual bool Fetch(const ibDataNode& request, ibDataNode& response) override;

	// Input — the panel was edited: the configuration is modified; Change {Composition} — what the panel holds, put on
	// the composer (the desktop's panel edited the metaobject's description in place); Select — a cell's "...", a
	// settings window's Act Choose: chosen through the configuration, said back with the view (State Chosen).
	virtual bool OnClientEvent(ibProtocol::ibEvent::ibKind event, const ibDataNode& args) override;

private:

	ibListSettingsChoice* m_choosing = nullptr;   // the cell a value is being chosen for (Select), counted

	wxDECLARE_DYNAMIC_CLASS(ibComposerCompositionView);
};

// Its document — the composer metaobject's (ibMetaDocument::GetMetaObject); the composition lives in the composer,
// saved with the configuration — nothing of its own to hold; its Save is the configuration's (see
// docViewInterfaceEdit.h).
class FRMSERVER_API ibComposerCompositionDocument : public ibMetaDocument {
public:
	ibComposerCompositionDocument() : ibMetaDocument() {}

	ibComposerCompositionDocument(const ibComposerCompositionDocument&) = delete;
	ibComposerCompositionDocument& operator=(const ibComposerCompositionDocument&) = delete;
	wxDECLARE_DYNAMIC_CLASS(ibComposerCompositionDocument);
};

// WHAT A COMPOSITION'S PANEL ASKS OF THE CONFIGURATION, on whichever designer tab it is — a composer's own, or a form's
// whose attribute holds a composition (ibPropertyDataComposition): the fields of a query text (Kind Fields {Text}) and
// of a reference's targets (Kind Expand {Targets}), a dynamic list's (Kind Source {Id, Text}), what a parameter may be
// given (Kind Types) and what is wrong with its expression (Kind Check {Text} — Error, or nothing). False: not one of
// these.
FRMSERVER_API bool ibFetchComposerRequest(const ibDataNode& request, ibDataNode& response, const ibMetaData* metaData);

#endif
