#ifndef _COMMON_ATTRIBUTE_DOC_H__
#define _COMMON_ATTRIBUTE_DOC_H__

// The document/view pair behind a COMMON ATTRIBUTE — opening one shows its
// COMPOSITION, the objects that carry it. Mirrors the section pair next door
// (docViewInterface.h), because it is the same gesture: click the thing, check the
// objects it applies to.

#include "frontend/docView/docView.h"

class ibCommonAttributeEditView : public ibMetaView {
	class ibCommonAttributeCompositionEditor* m_compositionEditor;
public:

	ibCommonAttributeEditView() : ibMetaView() {}

	virtual bool OnCreate(ibDocument* doc, long flags) override;
	virtual void OnUpdate(ibView* sender, wxObject* hint) override;
	virtual void OnDraw(wxDC* dc) override;
	virtual bool OnClose(bool deleteWindow = true) override;

private:

	wxDECLARE_EVENT_TABLE();
	wxDECLARE_DYNAMIC_CLASS(ibCommonAttributeEditView);
};

class ibCommonAttributeDocument : public ibMetaDocument
{
public:
	ibCommonAttributeDocument() : ibMetaDocument() { m_childDoc = false; }

	virtual bool OnCreate(const wxString& path, long flags) override;

	virtual bool IsModified() const override;
	virtual void Modify(bool mod) override;

protected:

	virtual bool DoSaveDocument(const wxString& filename) override;
	virtual bool DoOpenDocument(const wxString& filename) override;

	wxDECLARE_NO_COPY_CLASS(ibCommonAttributeDocument);
	wxDECLARE_ABSTRACT_CLASS(ibCommonAttributeDocument);
};

class ibCommonAttributeEditDocument : public ibCommonAttributeDocument
{
public:
	ibCommonAttributeEditDocument() : ibCommonAttributeDocument() { }

	wxDECLARE_NO_COPY_CLASS(ibCommonAttributeEditDocument);
	wxDECLARE_DYNAMIC_CLASS(ibCommonAttributeEditDocument);
};

#endif
