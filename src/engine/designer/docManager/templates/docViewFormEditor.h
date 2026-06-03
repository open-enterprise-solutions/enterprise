#ifndef _FORM_DESIGNER_H__
#define _FORM_DESIGNER_H__

#include "docManager/docManager.h"
#include "win/editor/visualEditor/visualEditor.h"

#include <wx/fdrepdlg.h>

// ----------------------------------------------------------------------------
// Edit form classes
// ----------------------------------------------------------------------------

// The view using a standard wxTextCtrl to show its contents
class ibFormEditView : public ibMetaView {

	void OnCopy(wxCommandEvent& WXUNUSED(event)) { m_visualNotebook->Copy(); }
	void OnPaste(wxCommandEvent& WXUNUSED(event)) { m_visualNotebook->Paste(); }
	void OnSelectAll(wxCommandEvent& WXUNUSED(event)) { m_visualNotebook->SelectAll(); }

	void OnFind(wxFindDialogEvent& event) {
		m_visualNotebook->FindText(event.GetFindString(), event.GetFlags());
	}

public:

	ibVisualEditorNotebook* GetVisualNotebook() const { return m_visualNotebook; }

public:

	ibFormEditView() : ibMetaView(), m_visualNotebook(nullptr) {}

	virtual wxPrintout* OnCreatePrintout() override;
	virtual void OnCreateToolbar(wxAuiToolBar* toolbar) override;
	virtual bool OnCreate(ibDocument* doc, long flags) override;
	virtual void OnActivateView(bool activate, ibView* activeView, ibView* deactiveView) override;
	virtual void OnUpdate(ibView* sender, wxObject* hint) override;
	virtual void OnDraw(wxDC* dc) override;
	virtual bool OnClose(bool deleteWindow = true) override;

#if wxUSE_MENUS		
	virtual wxMenuBar* CreateMenuBar() const;
#endif // wxUSE_MENUS

protected:

	void OnMenuEvent(wxCommandEvent& event);

private:

	struct ibControlData {

		wxString m_name;
		wxBitmap m_bmp;
		wxWindowID m_id;
	};

	std::vector< ibControlData> m_controlDataArray;

	ibVisualEditorNotebook* m_visualNotebook;

	wxDECLARE_EVENT_TABLE();
	wxDECLARE_DYNAMIC_CLASS(ibFormEditView);
};

// ----------------------------------------------------------------------------
// ibTextDocument: ibDocument and wxTextCtrl married
// ----------------------------------------------------------------------------

class ibFormDocument : public ibValueModuleDocument {
public:

	ibFormDocument() : ibValueModuleDocument() {}

	virtual bool OnCreate(const wxString& path, long flags) override;
	virtual bool OnOpenDocument(const wxString& filename) override;
	virtual bool OnSaveDocument(const wxString& filename) override;
	virtual bool OnSaveModified() override;
	virtual bool OnCloseDocument() override;
	virtual wxCommandProcessor* OnCreateCommandProcessor() override;

	virtual ibVisualEditorNotebook* GetVisualNotebook() const = 0;

	virtual bool IsModified() const override;
	virtual void Modify(bool mod) override;

	virtual bool Save() override;

protected:

	wxDECLARE_NO_COPY_CLASS(ibFormDocument);
	wxDECLARE_ABSTRACT_CLASS(ibFormDocument);
};

class ibFormEditDocument : public ibFormDocument
{
public:

	ibFormEditDocument() : ibFormDocument() {}

	virtual ibVisualEditorNotebook* GetVisualNotebook() const override;

	virtual void SetCurrentLine(int lineBreakpoint, bool setBreakpoint) override {

		if (lineBreakpoint > 0) {
			ibVisualEditorNotebook* const visualNotebook = GetVisualNotebook();
			if (visualNotebook != nullptr && visualNotebook->GetSelection() != wxNOTEBOOK_PAGE_CODE_EDITOR) {
				visualNotebook->SetSelection(wxNOTEBOOK_PAGE_CODE_EDITOR);
			}
		}

		ibCodeEditor* const autoComplete = GetCodeEditor();
		wxASSERT(autoComplete);
		autoComplete->SetCurrentLine(lineBreakpoint, setBreakpoint);
	}

	virtual void SetToolTip(const wxString& resultStr) override {
		//ibVisualEditorNotebook* const visualNotebook = GetVisualNotebook();
		//if (visualNotebook != nullptr && visualNotebook->GetSelection() != wxNOTEBOOK_PAGE_CODE_EDITOR) {
		//	visualNotebook->SetSelection(wxNOTEBOOK_PAGE_CODE_EDITOR);
		//}
		ibCodeEditor* const codeEditor = GetCodeEditor();
		wxASSERT(codeEditor);
		codeEditor->SetToolTip(resultStr);

	}

	virtual void ShowAutoComplete(const ibDebugAutoCompleteData& debugData) override {
		ibVisualEditorNotebook* const visualNotebook = GetVisualNotebook();
		if (visualNotebook != nullptr && visualNotebook->GetSelection() != wxNOTEBOOK_PAGE_CODE_EDITOR) {
			visualNotebook->SetSelection(wxNOTEBOOK_PAGE_CODE_EDITOR);
		}
		ibCodeEditor* const codeEditor = GetCodeEditor();
		wxASSERT(codeEditor);
		codeEditor->ShowAutoComplete(debugData);
	}

	virtual ibCodeEditor* GetCodeEditor() const {
		ibVisualEditorNotebook* const visualNotebook = GetVisualNotebook();
		if (visualNotebook != nullptr)
			return visualNotebook->GetCodeEditor();
		return nullptr;
	};

	wxDECLARE_NO_COPY_CLASS(ibFormEditDocument);
	wxDECLARE_DYNAMIC_CLASS(ibFormEditDocument);
};

#endif