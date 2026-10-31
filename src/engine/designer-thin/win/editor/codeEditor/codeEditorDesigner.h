#ifndef __IB_CODE_DESIGNER_H__
#define __IB_CODE_DESIGNER_H__

#include "frmclient/win/editor/codeEditor/codeEditor.h"

class ibModuleEditDocument;

// THE DESKTOP'S (designer/win/editor/codeEditor/codeEditorDesigner.h) — the editor of a module's tab with the debugger
// wired in: breakpoints, the module patched as lines move, a value asked while stopped. The debugger is the designer's
// own (designer-thin/backend/debugger/debugClient.h), asked by the module's address — its document's
// (ibModuleEditDocument::GetDocPath, the view's State).
class ibCodeEditorDesigner : public ibCodeEditor {
public:

	ibCodeEditorDesigner(ibModuleEditDocument* document, wxWindow* parent, wxWindowID id = wxID_ANY,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize, long style = 0);

protected:

	virtual bool IsDebuggerEnterLoop() const override;
	virtual void OnEditDebugPoint(int line) override;
	virtual void OnPatchModule(int line, int linesAdded, bool atLineStart) override;
	virtual void OnEvaluateToolTip(const wxString& expression) override;
	virtual void RefreshBreakpointMarkers() override;
	virtual void AppendDebugMenu(wxMenu& menu, int line) override;
	virtual bool GetDebugPointHint(int line, wxString& hint) override;

private:

	// The module's tab, as what it is — the base holds it as a tab.
	ibModuleEditDocument* const m_moduleDocument;
};

#endif // __IB_CODE_DESIGNER_H__
