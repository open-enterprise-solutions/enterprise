#ifndef __OBJ_INFO_H__
#define __OBJ_INFO_H__

#include <wx/app.h>
#include <wx/docview.h>
#include <wx/msgdlg.h>

#include <vector>
#include <map>

class CValue;

class IMetaObject;
class IMetaObjectRecordData;

class CMetaModuleObject;
class CMetaFormObject;
class CMetaGridObject;

class CCodeEditorCtrl;
class CView;

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

	virtual void SetIcon(const wxIcon& icon) {
		m_docIcon = icon;
	}

	virtual wxIcon GetIcon() const {
		return m_docIcon;
	}

	virtual void SetMetaObject(IMetaObject* metaObject) {
		m_metaObject = metaObject;
	}

	virtual IMetaObject* GetMetaObject() const {
		return m_metaObject;
	}

private:

	CView* DoCreateView();

public:

	wxString GetModuleName() const;

	CDocument(CDocument* docParent = NULL);
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

	virtual void SetDocParent(CDocument* docParent) {
		wxASSERT(m_docParent == NULL);
		docParent->m_childDocs.push_back(this);
		m_docParent = docParent;
	}

	virtual wxCommandProcessor* CreateCommandProcessor() const {
		return NULL;
	}

	virtual wxDocManager* GetDocumentManager() const override {
		// For child documents we use the same document manager as the parent, even
		// though we don't have our own template (as children are not opened/saved
		// directly).

		if (m_docParent != NULL) {
			return m_docParent->GetDocumentManager();
		}

		return wxDocument::GetDocumentManager();
	}

	virtual void UpdateAllViews(wxView* sender = NULL, wxObject* hint = NULL) override;

	// Remove all views (because we're closing the document)
	virtual bool DeleteAllViews() override;

protected:

	CDocument* m_docParent;
	wxDList<CDocument> m_childDocs;

	bool m_childDoc;

	IMetaObject* m_metaObject;	//������� ������ ����������
};

class IMetaDataDocument : public CDocument {
	wxDECLARE_ABSTRACT_CLASS(IMetaDataDocument);
public:
	virtual class IMetadata* GetMetadata() const = 0;
};

class IModuleDocument : public CDocument {
	wxDECLARE_ABSTRACT_CLASS(IModuleDocument);
public:

	virtual void SetCurrentLine(int lineBreakpoint, bool setBreakpoint);
	virtual void SetToolTip(const wxString& resultStr);

	virtual CCodeEditorCtrl* GetCodeEditor() const = 0;
};

#include <wx/aui/auibar.h>

class CView : public wxView {
	wxDECLARE_ABSTRACT_CLASS(CView);
public:

	CDocument* GetDocument() const {
		return dynamic_cast<CDocument*>(m_viewDocument);
	}

	bool ShowFrame(bool show = true) {
		if (m_viewFrame != NULL) {
			if (m_viewFrame->Show(show)) {
				m_viewFrame->Layout();
				return true;
			}
		}
		return false;
	}

#if wxUSE_MENUS	
	virtual wxMenu* CreateViewMenu() const {
		return NULL;
	}
#endif // wxUSE_MENUS

	// Called by valueFramework if created automatically by the default document
	// manager class: gives view a chance to initialise
	virtual bool OnCreate(CDocument* WXUNUSED(doc), long WXUNUSED(flags)) {
		return true;	
	}

	virtual void OnActivateView(bool activate, wxView* activeView, wxView* deactiveView) override;
	
	virtual void OnCreateToolbar(wxAuiToolBar* toolbar) {}
	virtual void OnRemoveToolbar(wxAuiToolBar* toolbar) {}

	virtual void OnMenuItemClicked(int id) {
	}

	virtual void OnDraw(wxDC* dc) override {}
	virtual bool OnClose(bool deleteWindow = true) override {
		return wxView::OnClose(deleteWindow);
	}

	// A view's window can call this to notify the view it is (in)active.
	// The function then notifies the document manager.
	virtual void Activate(bool activate) override;

protected:
	void OnViewMenuClicked(wxCommandEvent& event);
};

#endif