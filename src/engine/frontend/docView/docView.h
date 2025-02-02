#ifndef __OBJ_INFO_H__
#define __OBJ_INFO_H__

#include <wx/app.h>
#include <wx/docview.h>
#include <wx/msgdlg.h>

#include <vector>
#include <map>

#include "backend/backend_form.h"
#include "frontend/frontend.h"

class BACKEND_API CValue;

class BACKEND_API IMetaObject;
class BACKEND_API IMetaObjectRecordData;

class BACKEND_API CMetaObjectModule;
class BACKEND_API CMetaObjectForm;
class BACKEND_API CMetaObjectGrid;

class CMetaView;

// Document template flags
enum
{
	wxDOC_READONLY = wxDOC_SILENT + 1,
};

class FRONTEND_API CMetaDocument : public IBackendMetaDocument, public wxDocument {
	wxDECLARE_ABSTRACT_CLASS(CMetaDocument);
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

	template <class T>
	inline T* ConvertMetaObjectToType() {
		return GetMetaObject()->ConvertToType<T>();
	}

private:

	CMetaView* DoCreateView();

public:

	wxString GetModuleName() const;

	CMetaDocument(CMetaDocument* docParent = nullptr);
	virtual ~CMetaDocument();

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

	virtual void SetDocParent(CMetaDocument* docParent) {
		wxASSERT(m_docParent == nullptr);
		docParent->m_childDocs.push_back(this);
		m_docParent = docParent;
	}

	virtual wxDList<CMetaDocument> GetChild() const {
		return m_childDocs;
	}

	virtual wxCommandProcessor* CreateCommandProcessor() const {
		return nullptr;
	}

	virtual wxDocManager* GetDocumentManager() const override {
		// For child documents we use the same document manager as the parent, even
		// though we don't have our own template (as children are not opened/saved
		// directly).

		if (m_docParent != nullptr) {
			return m_docParent->GetDocumentManager();
		}

		return wxDocument::GetDocumentManager();
	}

	virtual void UpdateAllViews(wxView* sender = nullptr, wxObject* hint = nullptr) override;

	// Remove all views (because we're closing the document)
	virtual bool DeleteAllViews() override;

protected:

	CMetaDocument* m_docParent;
	wxDList<CMetaDocument> m_childDocs;

	bool m_childDoc;

	IMetaObject* m_metaObject;	//текущий объект метаданных
};

class FRONTEND_API IMetaDataDocument : public CMetaDocument {
	wxDECLARE_ABSTRACT_CLASS(IMetaDataDocument);
public:
	virtual class IMetaData* GetMetaData() const = 0;
};

class FRONTEND_API IModuleDocument : public CMetaDocument {
	wxDECLARE_ABSTRACT_CLASS(IModuleDocument);
public:

	virtual void SetCurrentLine(int lineBreakpoint, bool setBreakpoint) = 0;
	virtual void SetToolTip(const wxString& resultStr) = 0;
	virtual void ShowAutoComplete(const struct debugAutoCompleteData_t& debugData) = 0;
};

#include <wx/aui/auibar.h>

class FRONTEND_API CMetaView : public wxView {
	wxDECLARE_ABSTRACT_CLASS(CMetaView);
public:

	CMetaDocument* GetDocument() const {
		return dynamic_cast<CMetaDocument*>(m_viewDocument);
	}

	bool ShowFrame(bool show = true) {
		if (m_viewFrame != nullptr) {
			if (m_viewFrame->Show(show)) {
				m_viewFrame->Layout();
				return true;
			}
		}
		return false;
	}

#if wxUSE_MENUS	
	virtual wxMenu* CreateViewMenu() const {
		return nullptr;
	}
#endif // wxUSE_MENUS

	// Called by valueFramework if created automatically by the default document
	// manager class: gives view a chance to initialise
	virtual bool OnCreate(CMetaDocument* WXUNUSED(doc), long WXUNUSED(flags)) {
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