#ifndef _OBJINFO_H__
#define _OBJINFO_H__

#include <wx/app.h>
#include <wx/docview.h>
#include <wx/msgdlg.h>

#include <vector>
#include <map>

class IMetaObject;

class CValue;
class CMetaModuleObject;
class CMetaFormObject;
class CMetaGridObject;

class IMetaObjectRecordData;

class CView;
class CCommandProcessor;

// Document template flags
enum
{
	wxDOC_READONLY = wxDOC_SILENT + 1,
};

class CDocument : public wxDocument {
	wxDECLARE_ABSTRACT_CLASS(CDocument);
private:
	wxIcon m_docIcon;
public:

	void SetIcon(const wxIcon& icon) { m_docIcon = icon; }
	wxIcon GetIcon() const { return m_docIcon; }

	void SetMetaObject(IMetaObject *metaObject) { m_metaObject = metaObject; }
	IMetaObject *GetMetaObject() const;

	virtual CCommandProcessor *CreateCommandProcessor();
	virtual CCommandProcessor *GetCommandProcessor();

private:

	CView *DoCreateView();

public:

	wxString GetModuleName() const;

	CDocument(CDocument *docParent = NULL);
	virtual ~CDocument();

	virtual bool OnCreate(const wxString& WXUNUSED(path), long flags) override;

	virtual bool OnSaveModified() override;
	virtual bool OnSaveDocument(const wxString& filename) override;
	virtual bool OnCloseDocument() override;

	virtual bool IsModified() const override;
	virtual void Modify(bool mod) override;
	virtual bool Save() override;
	virtual bool SaveAs() override;
	virtual bool Close() override;

	virtual bool IsChildDocument() const {
		return m_childDoc;
	}

	virtual void SetDocParent(CDocument *docParent)
	{
		wxASSERT(m_docParent == NULL);
		docParent->m_childDocs.push_back(this);
		m_docParent = docParent;
	}

	virtual wxDocManager *GetDocumentManager() const override
	{
		// For child documents we use the same document manager as the parent, even
		// though we don't have our own template (as children are not opened/saved
		// directly).

		if (m_docParent) {
			return m_docParent->GetDocumentManager();
		}

		return wxDocument::GetDocumentManager();
	}

	virtual void UpdateAllViews(wxView* sender = NULL, wxObject* hint = NULL) override;
	// Remove all views (because we're closing the document)
	virtual bool DeleteAllViews() override;

protected:

	CDocument *m_docParent;
	wxDList<CDocument> m_childDocs;

	bool m_childDoc;

	IMetaObject *m_metaObject;	//текущий объект метаданных
};

#include <wx/aui/auibar.h>

class CView : public wxView
{
	wxDECLARE_ABSTRACT_CLASS(CView);

public:

	CView() : wxView() {}

	CDocument *GetDocument() const { return dynamic_cast<CDocument *>(m_viewDocument); }
	bool ShowFrame(bool show = true) { if (m_viewFrame) { return m_viewFrame->Show(show); } return false; }

	// Called by valueFramework if created automatically by the default document
	// manager class: gives view a chance to initialise
	virtual bool OnCreate(CDocument *WXUNUSED(doc), long WXUNUSED(flags));
	virtual void OnActivateView(bool activate, wxView *activeView, wxView *deactiveView) override;
	virtual void OnCreateToolbar(wxAuiToolBar *toolbar);
	virtual void OnRemoveToolbar(wxAuiToolBar *toolbar);

	virtual void OnDraw(wxDC *dc) override {}
	virtual bool OnClose(bool deleteWindow = true) override { return wxView::OnClose(deleteWindow); }

	// A view's window can call this to notify the view it is (in)active.
	// The function then notifies the document manager.
	virtual void Activate(bool activate) override;
};

class IMetaDocument {
public:
	virtual class IMetadata *GetMetadata() const = 0;
};

#endif