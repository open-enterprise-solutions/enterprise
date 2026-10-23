#ifndef _DESIGNER_MODULE_EDIT_H__
#define _DESIGNER_MODULE_EDIT_H__

// A MODULE AS THE DESIGNER EDITS IT — the desktop designer's ibModuleEditDocument and ibModuleEditView
// (designer/docManager/templates/docViewModuleEditor.h) over a module metaobject: its text in a tab, edited where it is
// kept — the metaobject — so what is modified and saved is the configuration. The text is fetched by the tab, not
// drawn: a frame that carried it would send the whole module back in every patch after a keystroke's Change.
// Not the desktop's names: the client's tab copies them (designer-thin), and frmserver is loaded into the client for a
// file base — one wx class info per name.

#include "frmserver/docView/docView.h"

// The module's text, drawn by the client's code editor (the tab's Kind) — the syntax it is written in, and whether it
// takes typing.
class FRMSERVER_API ibModuleTextView : public ibMetaView {
public:

	ibModuleTextView() : ibMetaView() {}

	virtual void OnDraw(ibDataNode& frame) override;
	virtual ibProtocol::ibView::ibKind GetViewKind() const override { return ibProtocol::ibView::ibKind::Module; }
	// The text as the metaobject keeps it ({Text}); what the desktop's code editor asked the configuration, by the
	// request's Kind (ibProtocol::ibView::ibModule::ibRequest) — the names at the editor's caret, a call's form, the default procedures.
	virtual bool Fetch(const ibDataNode& request, ibDataNode& response) override;
	// Typing started (Input) — the configuration modified, as the desktop's editor marked it on the first key; the
	// text the person typed, handed over (Change {Text}) — the module's from then on; the module checked (Command
	// {Id: SyntaxControl}).
	virtual bool OnClientEvent(ibProtocol::ibEvent::ibKind event, const ibDataNode& args) override;

private:

	// Whether the text rode a frame already (OnDraw) — once, in the frame that opens the tab.
	bool m_textDrawn = false;

	wxDECLARE_DYNAMIC_CLASS(ibModuleTextView);
};

// Its document — the module metaobject's (ibMetaDocument::GetMetaObject); nothing of its own to hold. Saved and closed
// past the module's check, as the desktop's (ibModuleDocument): a module that does not compile is said, and the person
// chooses to go on.
class FRMSERVER_API ibModuleTextDocument : public ibMetaDocument {
public:

	ibModuleTextDocument() : ibMetaDocument() {}

	// The module's check, at the gate of a close — asked in the close's work, once (m_closeAllowed).
	virtual bool OnSaveModified() override;
	virtual bool Save() override;

	// THE DESKTOP EDITOR'S CHECK (ibCodeEditor::SyntaxControl), where the module is compiled — the session's
	// configuration: true when it compiles, or when the person goes on past its errors (throwMessage false — asked);
	// throwMessage true says "no errors" too.
	bool SyntaxControl(bool throwMessage) const;

private:

	// The gate let this close through — the module compiled, or the person went on past its errors. Nothing refuses a
	// module's close after its gate, so the document goes with it.
	bool m_closeAllowed = false;

	ibModuleTextDocument(const ibModuleTextDocument&) = delete;
	ibModuleTextDocument& operator=(const ibModuleTextDocument&) = delete;
	wxDECLARE_DYNAMIC_CLASS(ibModuleTextDocument);
};

#endif
