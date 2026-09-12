////////////////////////////////////////////////////////////////////////////
//	Description : designer-side debug wiring for ibCodeEditor
////////////////////////////////////////////////////////////////////////////

#include "codeEditorDesigner.h"

#include "backend/debugger/debugClient.h"
#include "backend/metaCollection/metaModuleObject.h"
#include "frontend/docView/docView.h"

#include <wx/menu.h>
#include <wx/textdlg.h>   // wxTextEntryDialog - the breakpoint's condition

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
	if ((dwFlags & ((1 << ibCodeEditor::Breakpoint) | (1 << ibCodeEditor::ConditionalBreakpoint)))) {
		if (debugClient->RemoveBreakpoint(filename, line)) {
			MarkerDelete(line, ibCodeEditor::Breakpoint);
			MarkerDelete(line, ibCodeEditor::ConditionalBreakpoint);
		}
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

bool ibCodeEditorDesigner::GetDebugPointHint(int line, wxString& hint)
{
	if (debugClient == nullptr || m_document == nullptr || line < 0)
		return false;
	const std::map<unsigned int, wxString> breakpoints = debugClient->GetDebugList(m_document->GetFilename());
	const auto it = breakpoints.find((unsigned int)line);
	if (it == breakpoints.end() || it->second.IsEmpty())
		return false;
	hint = wxString::Format(_("Breakpoint condition: %s"), it->second);
	return true;
}

// ⭐ A CONDITION IS A PROPERTY OF THE BREAKPOINT, set where the breakpoint is: on its line. Asking for one
// on a line without a breakpoint puts one there with it; an empty answer leaves an ordinary breakpoint.
// Available on the same terms as the margin click (OnMarginClick): a module that cannot be edited takes no
// breakpoints, so it takes no condition either. A refusal (a line the running application does not have)
// is said the way the margin's is - the client puts it through the platform's Message.
void ibCodeEditorDesigner::AppendDebugMenu(wxMenu& menu, int line)
{
	if (debugClient == nullptr || m_document == nullptr)
		return;

	menu.AppendSeparator();
	wxMenuItem* item = menu.Append(wxID_ANY, _("Breakpoint condition..."));
	item->Enable(IsEditable());

	menu.Bind(wxEVT_MENU, [this, line](wxCommandEvent&) {
		const wxString filename = m_document->GetFilename();
		const std::map<unsigned int, wxString> breakpoints = debugClient->GetDebugList(filename);
		const auto current = breakpoints.find((unsigned int)line);
		wxTextEntryDialog dialog(this,
			_("Stop on this line only when the condition is true, for example: i > 500. Leave it empty to stop every time."),
			_("Breakpoint condition"), current != breakpoints.end() ? current->second : wxString());
		if (dialog.ShowModal() != wxID_OK)
			return;
		// Redrawn from the client rather than patched here: the marker's colour follows the condition.
		if (debugClient->ToggleBreakpoint(filename, line, nullptr, dialog.GetValue().Strip(wxString::both)))
			RefreshBreakpoint();
	}, item->GetId());
}

void ibCodeEditorDesigner::RefreshBreakpointMarkers()
{
	if (debugClient == nullptr || m_document == nullptr)
		return;
	// A breakpoint with a condition is drawn in its own colour (ibCodeEditor::ConditionalBreakpoint).
	for (const auto& breakpoint : debugClient->GetDebugList(m_document->GetFilename())) {
		const int marker = breakpoint.second.IsEmpty() ? ibCodeEditor::Breakpoint : ibCodeEditor::ConditionalBreakpoint;
		if (!(MarkerGet(breakpoint.first) & (1 << marker)))
			MarkerAdd(breakpoint.first, marker);
	}
}
