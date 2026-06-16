#ifndef __IB_CODE_DESIGNER_H__
#define __IB_CODE_DESIGNER_H__

#include "frontend/win/editor/codeEditor/codeEditor.h"

// ibCodeEditorDesigner — designer-side wrapper around frontend's ibCodeEditor
// that wires the debugger integration. Frontend's editor is sessionless
// and knows nothing about debugClient; codeRunner uses it as-is. Designer
// instantiates this subclass instead so breakpoints, autocomplete-eval,
// tooltip-eval, and module-patch notifications reach the running script.
class ibCodeEditorDesigner : public ibCodeEditor {
public:
	using ibCodeEditor::ibCodeEditor;

protected:

	bool IsDebuggerEnterLoop() const override;
	void OnEditDebugPoint(int line) override;
	void OnPatchModule(int line, int linesAdded, bool atLineStart) override;
	void OnEvaluateAutocomplete(const wxString& fileName,
	                             const wxString& docPath,
	                             const wxString& expression,
	                             const wxString& keyword,
	                             int pos) override;
	void OnEvaluateToolTip(const wxString& fileName,
	                        const wxString& docPath,
	                        const wxString& expression) override;
	void RefreshBreakpointMarkers() override;
};

#endif // __IB_CODE_DESIGNER_H__
