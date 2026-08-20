#ifndef _COMPOSER_H__
#define _COMPOSER_H__

#include "frontend/docView/docView.h"

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

	virtual bool OnCreate(ibDocument* doc, long flags) override;
	virtual void OnUpdate(ibView* sender, wxObject* hint) override;
	virtual void OnDraw(wxDC* dc) override;
	virtual bool OnClose(bool deleteWindow = true) override;

private:

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

#endif // !_COMPOSER_H__
