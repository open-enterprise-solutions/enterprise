#ifndef _FUNCTIONAL_OPTION_DOC_H__
#define _FUNCTIONAL_OPTION_DOC_H__

// The document/view pair behind a FUNCTIONAL OPTION — opening one shows its MEMBERS, what is not shown while
// it is off. Mirrors the common attribute's pair next door (docViewCommonAttribute.h), because it is the
// same gesture: click the thing, tick what it applies to.

#include "frontend/docView/docView.h"

class ibFunctionalOptionEditView : public ibMetaView {
	class ibFunctionalOptionEditor* m_membersEditor;
public:

	ibFunctionalOptionEditView() : ibMetaView() {}

	virtual bool OnCreate(ibDocument* doc, long flags) override;
	virtual void OnUpdate(ibView* sender, wxObject* hint) override;
	virtual void OnDraw(wxDC* dc) override;
	virtual bool OnClose(bool deleteWindow = true) override;

private:

	wxDECLARE_EVENT_TABLE();
	wxDECLARE_DYNAMIC_CLASS(ibFunctionalOptionEditView);
};

class ibFunctionalOptionDocument : public ibMetaDocument
{
public:
	ibFunctionalOptionDocument() : ibMetaDocument() { m_childDoc = false; }

	virtual bool OnCreate(const wxString& path, long flags) override;

	virtual bool IsModified() const override;
	virtual void Modify(bool mod) override;

protected:

	virtual bool DoSaveDocument(const wxString& filename) override;
	virtual bool DoOpenDocument(const wxString& filename) override;

	wxDECLARE_NO_COPY_CLASS(ibFunctionalOptionDocument);
	wxDECLARE_ABSTRACT_CLASS(ibFunctionalOptionDocument);
};

class ibFunctionalOptionEditDocument : public ibFunctionalOptionDocument
{
public:
	ibFunctionalOptionEditDocument() : ibFunctionalOptionDocument() { }

	wxDECLARE_NO_COPY_CLASS(ibFunctionalOptionEditDocument);
	wxDECLARE_DYNAMIC_CLASS(ibFunctionalOptionEditDocument);
};

#endif
