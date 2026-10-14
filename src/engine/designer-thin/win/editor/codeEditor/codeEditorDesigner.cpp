////////////////////////////////////////////////////////////////////////////
//	Description : designer-side debug wiring for ibCodeEditor
////////////////////////////////////////////////////////////////////////////

#include "codeEditorDesigner.h"

#include "designer-thin/backend/debugger/debugClient.h"
#include "designer-thin/docManager/templates/docViewModuleEditor.h"

#include <wx/menu.h>
#include <wx/textdlg.h>   // wxTextEntryDialog - the breakpoint's condition
#include "frmclient/artProvider/artProvider.h"   // wxART_BREAKPOINT_CONDITION

ibCodeEditorDesigner::ibCodeEditorDesigner(ibModuleEditDocument* document, wxWindow* parent, wxWindowID id,
	const wxPoint& pos, const wxSize& size, long style)
	: ibCodeEditor(document, parent, id, pos, size, style), m_moduleDocument(document)
{
}

bool ibCodeEditorDesigner::IsDebuggerEnterLoop() const
{
	return debugClient != nullptr && debugClient->IsEnterLoop();
}

void ibCodeEditorDesigner::OnEditDebugPoint(int line)
{
	if (debugClient == nullptr || m_moduleDocument == nullptr)
		return;

	const wxString filename = m_moduleDocument->GetDocPath();
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
	if (debugClient == nullptr || m_moduleDocument == nullptr)
		return;
	debugClient->PatchModule(m_moduleDocument->GetDocPath(), line, linesAdded, atLineStart);
}

void ibCodeEditorDesigner::OnEvaluateToolTip(const wxString& expression)
{
	if (debugClient == nullptr || m_moduleDocument == nullptr) return;
	debugClient->EvaluateToolTip(wxEmptyString, m_moduleDocument->GetDocPath(), expression);
}

bool ibCodeEditorDesigner::GetDebugPointHint(int line, wxString& hint)
{
	if (debugClient == nullptr || m_moduleDocument == nullptr || line < 0)
		return false;
	const std::map<unsigned int, wxString> breakpoints = debugClient->GetDebugList(m_moduleDocument->GetDocPath());
	const auto it = breakpoints.find((unsigned int)line);
	if (it == breakpoints.end() || it->second.IsEmpty())
		return false;
	hint = wxString::Format(_("Breakpoint condition: %s"), it->second);
	return true;
}

// ⭐ A CONDITION IS A PROPERTY OF THE BREAKPOINT, set where the breakpoint is: on its line — the desktop's.
void ibCodeEditorDesigner::AppendDebugMenu(wxMenu& menu, int line)
{
	if (debugClient == nullptr || m_moduleDocument == nullptr)
		return;

	menu.AppendSeparator();
	wxMenuItem* item = menu.Append(wxID_ANY, _("Breakpoint condition..."));
	item->SetBitmap(wxArtProvider::GetBitmap(wxART_BREAKPOINT_CONDITION, wxART_DEBUG, FromDIP(wxSize(16, 16))));
	item->Enable(IsEditable());

	menu.Bind(wxEVT_MENU, [this, line](wxCommandEvent&) {
		const wxString filename = m_moduleDocument->GetDocPath();
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
	if (debugClient == nullptr || m_moduleDocument == nullptr)
		return;
	// A breakpoint with a condition is drawn in its own colour (ibCodeEditor::ConditionalBreakpoint).
	for (const auto& breakpoint : debugClient->GetDebugList(m_moduleDocument->GetDocPath())) {
		const int marker = breakpoint.second.IsEmpty() ? ibCodeEditor::Breakpoint : ibCodeEditor::ConditionalBreakpoint;
		if (!(MarkerGet(breakpoint.first) & (1 << marker)))
			MarkerAdd(breakpoint.first, marker);
	}
}
