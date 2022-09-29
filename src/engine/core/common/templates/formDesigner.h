#ifndef _FORMDESIGNER_H__
#define _FORMDESIGNER_H__

class ObjectTree;
class CVisualEditorHost;
class CFormPalette;

#include "common/formdefs.h"
#include "common/types.h"
#include "common/docInfo.h"
#include "frontend/objinspect/events.h"

#include "frontend/codeEditor/codeEditorCtrl.h"

#include <wx/aui/aui.h>
#include <wx/fdrepdlg.h>

// ----------------------------------------------------------------------------
// Edit form classes
// ----------------------------------------------------------------------------

// The view using a standard wxTextCtrl to show its contents
class CFormEditView : public CView
{
	wxAuiNotebook* m_notebook;
private:

	class CCodeEditorCtrl* m_codeEditor;
	class CVisualEditorContextForm* m_visualEditor;

private:

	void InitializeFormDesigner(long flags);
	void InitializeCodeView(long flags);

private:

	void OnCopy(wxCommandEvent& WXUNUSED(event));
	void OnPaste(wxCommandEvent& WXUNUSED(event));
	void OnSelectAll(wxCommandEvent& WXUNUSED(event));

	void OnFind(wxFindDialogEvent& event);
	void OnEventHandlerModified(wxFrameEventHandlerEvent& event);

public:

	class CVisualEditorContextForm* GetDesignerContext() const {
		return m_visualEditor;
	}

public:

	bool LoadForm();
	bool SaveForm();

	bool IsDesignerActivate() const {
		return m_notebook->GetSelection() == 0;
	}

	bool IsEditorActivate() const {
		return m_notebook->GetSelection() == 1;
	}

	void ActivateDesigner() {
		m_notebook->SetSelection(0);
	}

	void ActivateEditor() {
		m_notebook->SetSelection(1);
	}

	CCodeEditorCtrl* GetCodeCtrl() const {
		return m_codeEditor;
	}

public:

	CFormEditView() : CView() {}

	virtual bool OnCreate(CDocument* doc, long flags) override;
	virtual void OnActivateView(bool activate, wxView* activeView, wxView* deactiveView) override;
	virtual void OnUpdate(wxView* sender, wxObject* hint) override;
	virtual void OnDraw(wxDC* dc) override;
	virtual bool OnClose(bool deleteWindow = true) override;

	virtual wxPrintout* OnCreatePrintout() override;

	virtual void OnCreateToolbar(wxAuiToolBar* toolbar) override;
	virtual void OnRemoveToolbar(wxAuiToolBar* toolbar) override;

protected:

	void OnMenuClick(wxCommandEvent& event);
	void OnPageChanged(wxAuiNotebookEvent& event);
	void OnMenuClicked(wxCommandEvent& event);

private:

	wxDECLARE_EVENT_TABLE();
	wxDECLARE_DYNAMIC_CLASS(CFormEditView);
};

// ----------------------------------------------------------------------------
// CTextDocument: wxDocument and wxTextCtrl married
// ----------------------------------------------------------------------------

class CFormDocument : public CDocument
{
public:
	CFormDocument() : CDocument() { }

	virtual CCommandProcessor* CreateCommandProcessor() override;

	virtual bool OnCreate(const wxString& path, long flags) override;
	virtual bool OnOpenDocument(const wxString& filename) override;
	virtual bool OnSaveDocument(const wxString& filename) override;
	virtual bool OnSaveModified() override;
	virtual bool OnCloseDocument() override;

	virtual bool IsModified() const override;
	virtual void Modify(bool mod) override;

	virtual bool Save() override;

	virtual CFormEditView* GetFormDesigner() const = 0;

	bool IsDesignerActivate() const {
		return GetFormDesigner()->IsDesignerActivate();
	}

	bool IsEditorActivate() const {
		return GetFormDesigner()->IsEditorActivate();
	}

	CCodeEditorCtrl* GetCodeCtrl() const {
		return GetFormDesigner()->GetCodeCtrl();
	}

	//get common value object (read only)
	virtual CValue GetCommonValueObject() const;

protected:

	wxDECLARE_NO_COPY_CLASS(CFormDocument);
	wxDECLARE_ABSTRACT_CLASS(CFormDocument);
};

class CFormEditDocument : public CFormDocument
{
public:
	CFormEditDocument() : CFormDocument() {}

	virtual CFormEditView* GetFormDesigner() const override;
	virtual void SetCurrentLine(int lineBreakpoint, bool setBreakpoint) override;

	wxDECLARE_NO_COPY_CLASS(CFormEditDocument);
	wxDECLARE_DYNAMIC_CLASS(CFormEditDocument);
};

#endif