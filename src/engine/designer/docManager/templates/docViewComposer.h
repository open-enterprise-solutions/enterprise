#ifndef _COMPOSER_H__
#define _COMPOSER_H__

#include "frontend/docView/docView.h"
// (NOTHING FROM THE COMPOSITION SIDE. This view neither holds a description nor runs one — it
//  decides WHERE the editor lives and hands it the document.)

// ----------------------------------------------------------------------------
// A COMPOSER OPENS ON A TAB OF ITS OWN, like a form or a template.
//
// What it declares — the query, the resources, the parameters, the output structure — is edited by
// the SAME panel a composition on a form is edited by (ibComposerSettingsPanel). The panel was split
// out of the modal dialog for exactly this: one content, two hosts, so a setting cannot mean one
// thing in the designer and another on a form.
//
// Accepting the tab is Commit() on that panel — the embedded filter / sort live in a transactional
// buffer until then, and a panel that objects keeps the tab open on what it objected to.
// ----------------------------------------------------------------------------

class ibComposerEditView : public ibMetaView {
	class ibComposerSettingsPanel* m_composerPanel = nullptr;
public:
	ibComposerEditView() : ibMetaView() {}

	// ⭐ PUT THE BUFFER ONTO THE COMPOSITION — the one verb, said in one place, for both gestures
	// that accept this tab: closing it, and SAVING while it is still open. Until this existed only
	// the first of the two landed anything, so a person who added a grouping and pressed the
	// diskette wrote a report that still held the settings from before the edit.
	//
	// Answers false when the panel objects (a half-written condition): the caller must keep the tab
	// open on what it objected to, and must not write the tree.
	bool Commit();

	virtual bool OnCreate(ibDocument* doc, long flags) override;
	virtual void OnUpdate(ibView* sender, wxObject* hint) override;
	virtual void OnDraw(wxDC* dc) override;
	virtual bool OnClose(bool deleteWindow = true) override;

private:

	// (THE COMPOSITION IS GONE, and with it the last runtime object on this road. It was kept for
	//  the questions "which fields does the query offer" and "why did it refuse" — which turned out
	//  to be questions about a TEXT and a CONFIGURATION, answered by ibQueryFieldsOfText without
	//  anything running. A composition is the facade the FORM road holds; a designer tab holds a
	//  document, and the editor takes both of its answers from that.)

	// (AND NO STAND-IN DESCRIPTION EITHER. This view held one so the panel always had something to
	//  bind to; the editor reaches the description through the document now, and owns that answer —
	//  including what to bind to when a tab was somehow opened on no composer at all.)

	wxDECLARE_EVENT_TABLE();
	wxDECLARE_DYNAMIC_CLASS(ibComposerEditView);
};

class ibComposerDocument : public ibMetaDocument
{
public:
	ibComposerDocument() : ibMetaDocument() {}

	virtual bool OnCreate(const wxString& path, long flags) override;

	virtual bool IsModified() const override;
	virtual void Modify(bool mod) override;

	// SAVING THIS TAB ACCEPTS IT — the same house call the module and the form editors make
	// (ibModuleDocument::Save / ibFormDocument::Save flush their editor before the base writes).
	// This covers the configuration's Ctrl+S while a composer tab has the focus; an external report
	// is saved from its own tree and lands through ibCommitOpenComposers.
	virtual bool Save() override;

protected:

	virtual bool DoSaveDocument(const wxString& filename) override;
	virtual bool DoOpenDocument(const wxString& filename) override;

	wxDECLARE_NO_COPY_CLASS(ibComposerDocument);
	wxDECLARE_ABSTRACT_CLASS(ibComposerDocument);
};

class ibComposerEditDocument : public ibComposerDocument
{
public:
	ibComposerEditDocument() : ibComposerDocument() {}

	wxDECLARE_NO_COPY_CLASS(ibComposerEditDocument);
	wxDECLARE_DYNAMIC_CLASS(ibComposerEditDocument);
};

// ⭐ LAND EVERY OPEN COMPOSER TAB, then the tree may be written.
//
// Asked of EVERY open document rather than of the one being saved: the tab the diskette is pressed
// on is rarely the tab that was edited — an external report is saved from its own tree while the
// composer sits on the tab beside it. A composition edited there is not on the metaobject yet, and
// serialising the tree without asking writes the report as it was BEFORE the edit.
//
// Answers false when a panel objected; the save must then not happen, exactly as a close does not.
bool ibCommitOpenComposers();

#endif // !_COMPOSER_H__
