////////////////////////////////////////////////////////////////////////////
//	Description : designer-side debug wiring for ibCodeEditor
////////////////////////////////////////////////////////////////////////////

#include "codeEditorDesigner.h"

#include "backend/debugger/debugClient.h"
#include "backend/metaCollection/metaModuleObject.h"
#include "frontend/docView/docView.h"

bool ibCodeEditorDesigner::IsDebuggerEnterLoop() const
{
	return debugClient != nullptr && debugClient->IsEnterLoop();
}

void ibCodeEditorDesigner::OnEditDebugPoint(int line)
{
	if (debugClient == nullptr || m_document == nullptr)
		return;

	const wxString filename = m_document->GetFilename();
	const int dwFlags = MarkerGet(line);
	if ((dwFlags & (1 << ibCodeEditor::Breakpoint))) {
		if (debugClient->RemoveBreakpoint(filename, line))
			MarkerDelete(line, ibCodeEditor::Breakpoint);
	}
	else if (debugClient->ToggleBreakpoint(filename, line)) {
		MarkerAdd(line, ibCodeEditor::Breakpoint);
	}
}

void ibCodeEditorDesigner::OnPatchModule(int line, int linesAdded, bool atLineStart)
{
	if (debugClient == nullptr || m_document == nullptr)
		return;
	ibValueMetaObjectModuleBase* moduleObject =
		m_document->ConvertMetaObjectToType<ibValueMetaObjectModuleBase>();
	if (moduleObject != nullptr)
		debugClient->PatchModule(moduleObject->GetDocPath(), line, linesAdded, atLineStart);
}

void ibCodeEditorDesigner::OnEvaluateAutocomplete(const wxString& fileName,
                                             const wxString& docPath,
                                             const wxString& expression,
                                             const wxString& keyword,
                                             int pos)
{
	if (debugClient == nullptr) return;
	debugClient->EvaluateAutocomplete(fileName, docPath, expression, keyword, pos);
}

void ibCodeEditorDesigner::OnEvaluateToolTip(const wxString& fileName,
                                        const wxString& docPath,
                                        const wxString& expression)
{
	if (debugClient == nullptr) return;
	debugClient->EvaluateToolTip(fileName, docPath, expression);
}

void ibCodeEditorDesigner::RefreshBreakpointMarkers()
{
	if (debugClient == nullptr || m_document == nullptr)
		return;
	for (auto& line : debugClient->GetDebugList(m_document->GetFilename())) {
		const int dwFlags = MarkerGet(line);
		if (!(dwFlags & (1 << ibCodeEditor::Breakpoint)))
			MarkerAdd(line, ibCodeEditor::Breakpoint);
	}
}
