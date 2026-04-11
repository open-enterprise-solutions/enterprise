#ifndef __OBJ_INFO_H__
#define __OBJ_INFO_H__

#include <wx/app.h>
#include <wx/cmdproc.h>
#include <wx/docview.h>
#include <wx/dlist.h>
#include <wx/msgdlg.h>

#include <vector>
#include <map>

#include "backend/backend_form.h"
#include "frontend/frontend.h"

class BACKEND_API ibValue;

#include "backend/metaCollection/metaObject.h"
class BACKEND_API ibValueMetaObject;
class BACKEND_API ibValueMetaObjectRecordData;

class BACKEND_API ibValueMetaObjectModule;
class BACKEND_API ibValueMetaObjectForm;
class BACKEND_API ibValueMetaObjectGrid;

class ibMetaView;

// Document template flags
enum
{
	wxDOC_READONLY = wxDOC_SILENT + 1,
};

class FRONTEND_API ibMetaDocument : public ibBackendMetaDocument, public wxDocument {
	wxDECLARE_ABSTRACT_CLASS(ibMetaDocument);
public:

	virtual void SetIcon(const wxIcon& icon) { m_docIcon = icon; }
	virtual wxIcon GetIcon() const { return m_docIcon; }

	virtual void SetMetaObject(ibValueMetaObject* metaObject) { m_metaObject = metaObject; }
	virtual ibValueMetaObject* GetMetaObject() const { return m_metaObject; }

	template <class T>
	inline T* ConvertMetaObjectToType() {
		return GetMetaObject()->ConvertToType<T>();
	}

protected:
	virtual ibMetaView* DoCreateView();
public:

	wxString GetModuleName() const;

	ibMetaDocument(ibMetaDocument* docParent = nullptr);
	virtual ~ibMetaDocument();

	// Called after a view is added or removed. The default implementation
	// deletes the document if this is there are no more views.
	virtual void OnChangedViewList() { if (m_documentViews.empty()) delete this; }

	virtual bool OnCreate(const wxString& WXUNUSED(path), long flags) override;

	virtual bool OnSaveModified() override;
	virtual bool OnSaveDocument(const wxString& filename) override;
	virtual bool OnCloseDocument() override;

	virtual bool IsModified() const override;
	virtual void Modify(bool mod) override;
	virtual bool Save() override;
	virtual bool SaveAs() override;
	virtual bool Close() override;

	virtual bool IsChildDocument() const { return m_childDoc; }

	virtual void SetDocParent(ibMetaDocument* docParent) {
		if (docParent != nullptr) {
			docParent->m_childDocs.Append(this);
			m_documentParent = docParent;
		}
		else {
			auto it = m_documentParent->m_childDocs.Find(this);
			//wxASSERT(it != m_documentParent->m_childDocs.end());
			if (it->GetData() != nullptr) {
				m_documentParent->m_childDocs.Erase(it);
				m_documentParent = nullptr;
			}
		}
	}

	virtual bool IsCloseOnOwnerClose() const { return true; }

	virtual wxDList<ibMetaDocument> GetChild() const { return m_childDocs; }
	virtual wxDocManager* GetDocumentManager() const override {
		// For child documents we use the same document manager as the parent, even
		// though we don't have our own template (as children are not opened/saved
		// directly).

		if (m_documentParent != nullptr) {
			return m_documentParent->GetDocumentManager();
		}

		return wxDocument::GetDocumentManager();
	}

	virtual void UpdateAllViews(wxView* sender = nullptr, wxObject* hint = nullptr) override;

	// Remove all views (because we're closing the document)
	virtual bool DeleteAllViews() override;

protected:

	ibMetaDocument* m_documentParent;
	ibValueMetaObject* m_metaObject;	// current metadata object

	wxDList<ibMetaDocument> m_childDocs;
	bool m_childDoc;

private:
	wxIcon m_docIcon;
};

class FRONTEND_API ibMetaDataDocument : public ibMetaDocument {
	wxDECLARE_ABSTRACT_CLASS(ibMetaDataDocument);
public:
	virtual class ibMetaData* GetMetaData() const = 0;
};

class FRONTEND_API ibValueModulibDocument : public ibMetaDocument {
	wxDECLARE_ABSTRACT_CLASS(ibValueModulibDocument);
public:

	virtual void SetCurrentLine(int lineBreakpoint, bool setBreakpoint) = 0;
	virtual void SetToolTip(const wxString& resultStr) = 0;
	virtual void ShowAutoComplete(const struct ibDebugAutoCompleteData& debugData) = 0;
};

#include <wx/aui/auibar.h>

class FRONTEND_API ibMetaView : public wxView {
	wxDECLARE_ABSTRACT_CLASS(ibMetaView);
public:

	ibMetaDocument* GetDocument() const {
		return dynamic_cast<ibMetaDocument*>(m_viewDocument);
	}

	bool ShowFrame(bool show = true) {

		if (m_viewFrame != nullptr &&
			m_viewFrame->Show(show)) {
			return true;
		}

		return false;
	}

#if wxUSE_MENUS	
	virtual wxMenuBar* CreateMenuBar() const { return nullptr; }
#endif // wxUSE_MENUS

	// Called by valueFramework if created automatically by the default document
	// manager class: gives view a chance to initialise
	virtual bool OnCreate(ibMetaDocument* WXUNUSED(doc), long WXUNUSED(flags)) { return true; }
	virtual void OnCreateToolbar(wxAuiToolBar* toolbar) {}

	virtual void OnActivateView(bool activate, wxView* activeView, wxView* deactiveView) override;
	virtual void OnUpdate(wxView* sender, wxObject* hint = NULL) {}

	virtual void OnDraw(wxDC* dc) override {}
	virtual bool OnClose(bool deleteWindow = true) override { return wxView::OnClose(deleteWindow); }

	// A view's window can call this to notify the view it is (in)active.
	// The function then notifies the document manager.
	virtual void Activate(bool activate) override;
};

#endif