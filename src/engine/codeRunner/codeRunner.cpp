#include "codeRunner.h"
#include "backend/compiler/compileCode.h"
#include "backend/compiler/procUnit.h"
#include "frontend/artProvider/artProvider.h"
#include "frontend/mainFrame/settings/editorsettings.h"
#include "frontend/mainFrame/settings/fontcolorsettings.h"

#include <wx/filedlg.h>
#include <wx/aboutdlg.h>
#include <wx/wfstream.h>
#include <wx/textfile.h>
#include <wx/msgdlg.h>

enum {
	wxID_CR_ADD_COMMENTS = wxID_HIGHEST + 11000,
	wxID_CR_REMOVE_COMMENTS,
	wxID_CR_FORMAT_CODE,
	wxID_CR_INCREASE_INDENT,
	wxID_CR_DECREASE_INDENT,
	wxID_CR_GOTO_LINE,
	wxID_CR_PROC_AND_FUNC
};

void ibFrameCodeRunner::OnToolbarCommand(wxCommandEvent& event)
{
	switch (event.GetId()) {
		case wxID_CR_ADD_COMMENTS:    m_codeEditor->AddCommentsToSelection();      break;
		case wxID_CR_REMOVE_COMMENTS: m_codeEditor->RemoveCommentsFromSelection(); break;
		case wxID_CR_FORMAT_CODE:     m_codeEditor->FormatSelection();             break;
		case wxID_CR_INCREASE_INDENT: m_codeEditor->IncreaseIndent();              break;
		case wxID_CR_DECREASE_INDENT: m_codeEditor->DecreaseIndent();              break;
		case wxID_CR_GOTO_LINE:       m_codeEditor->ShowGotoLine();                break;
		case wxID_CR_PROC_AND_FUNC:   m_codeEditor->ShowMethods();                 break;
	}
}

void ibFrameCodeRunner::OnMenuOpen(wxCommandEvent&)
{
	wxFileDialog dlg(this, _("Open script"), wxEmptyString, wxEmptyString,
		_("Text files (*.txt)|*.txt|All files (*.*)|*.*"),
		wxFD_OPEN | wxFD_FILE_MUST_EXIST);
	if (dlg.ShowModal() != wxID_OK) return;

	wxTextFile file(dlg.GetPath());
	if (!file.Open()) {
		wxMessageBox(_("Cannot open file"), _("Code runner"), wxOK | wxICON_ERROR, this);
		return;
	}
	wxString text;
	for (size_t i = 0; i < file.GetLineCount(); i++) {
		if (i > 0) text += wxT("\n");
		text += file.GetLine(i);
	}
	file.Close();

	m_codeEditor->ClearAll();
	m_codeEditor->SetText(text);
	m_currentFile = dlg.GetPath();
	SetTitle(_("Code runner") + wxT(" - ") + m_currentFile);
}

void ibFrameCodeRunner::OnMenuSave(wxCommandEvent& event)
{
	if (m_currentFile.IsEmpty()) {
		OnMenuSaveAs(event);
		return;
	}
	wxFile file(m_currentFile, wxFile::write);
	if (!file.IsOpened()) {
		wxMessageBox(_("Cannot save file"), _("Code runner"), wxOK | wxICON_ERROR, this);
		return;
	}
	const wxString text = m_codeEditor->GetText();
	file.Write(text);
}

void ibFrameCodeRunner::OnMenuSaveAs(wxCommandEvent&)
{
	wxFileDialog dlg(this, _("Save script as"), wxEmptyString, m_currentFile,
		_("Text files (*.txt)|*.txt|All files (*.*)|*.*"),
		wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
	if (dlg.ShowModal() != wxID_OK) return;

	wxFile file(dlg.GetPath(), wxFile::write);
	if (!file.IsOpened()) {
		wxMessageBox(_("Cannot save file"), _("Code runner"), wxOK | wxICON_ERROR, this);
		return;
	}
	file.Write(m_codeEditor->GetText());
	m_currentFile = dlg.GetPath();
	SetTitle(_("Code runner") + wxT(" - ") + m_currentFile);
}

void ibFrameCodeRunner::OnMenuExit(wxCommandEvent&)
{
	Close(true);
}

void ibFrameCodeRunner::OnMenuAbout(wxCommandEvent&)
{
	wxAboutDialogInfo info;
	info.SetName(_("OES Code Runner"));
	info.SetVersion(wxT("1.0"));
	info.SetDescription(_("Standalone OES script runner - write and execute scripts without metadata."));
	info.SetCopyright(wxT("(C) Open Enterprise Solutions"));
	wxAboutBox(info, this);
}

void ibFrameCodeRunner::OnMenuEdit(wxCommandEvent& event)
{
	// Forward standard wxID_UNDO / wxID_REDO / wxID_CUT / wxID_COPY /
	// wxID_PASTE / wxID_SELECTALL to the wxStyledTextCtrl base — it
	// implements all of them. Frame's own menu shortcuts dispatch
	// here when focus isn't already inside the editor (which would
	// route them directly).
	switch (event.GetId()) {
		case wxID_UNDO:      m_codeEditor->Undo();        break;
		case wxID_REDO:      m_codeEditor->Redo();        break;
		case wxID_CUT:       m_codeEditor->Cut();         break;
		case wxID_COPY:      m_codeEditor->Copy();        break;
		case wxID_PASTE:     m_codeEditor->Paste();       break;
		case wxID_SELECTALL: m_codeEditor->SelectAll();   break;
	}
}

///////////////////////////////////////////////////////////////////////////

void ibFrameCodeRunner::SyntaxCheckOnButtonClick(wxCommandEvent& event)
{
	try {
		m_compileCode->Compile(m_codeEditor->GetText());
		ibFrameCodeRunner::AppendOutput(_("No syntax errors detected!"));
	}
	catch (const ibBackendException& err) {
		ibFrameCodeRunner::AppendOutput(err.GetErrorDescription());
	}

	event.Skip();
}

void ibFrameCodeRunner::RunCodeOnButtonClick(wxCommandEvent& event)
{
	try {
		m_compileCode->Compile(m_codeEditor->GetText());
		// Build binder from compile-side staging so EnumManager / Catalogs /
		// other context bindings registered through AddContextVariable
		// reach the runtime — Execute(bc) alone constructs an EMPTY
		// binder and the pre-flight gate refuses any required slot.
		ibByteBinder binder = m_compileCode->CreateBinder();
		m_procUnit->Execute(m_compileCode->m_cByteCode, binder);
	}
	catch (const ibBackendException& err) {
		ibFrameCodeRunner::AppendOutput(err.GetErrorDescription());
	}

	event.Skip();
}

void ibFrameCodeRunner::ClearOutputOnButtonClick(wxCommandEvent& event)
{
	m_output->SetReadOnly(false);
	m_output->ClearAll();
	m_output->SetReadOnly(true);
	event.Skip();
}

void ibFrameCodeRunner::SyntaxChoiceOnChange(wxCommandEvent& event)
{
	// Items are aligned with the dropdown population order: 0 = VES,
	// 1 = CES. Mode is process-global on ibCompileCode (gs_codeStyle),
	// so updating it here affects every subsequent Compile call AND
	// the fold parser's mode-aware dispatch (VES keyword-folds + CES
	// brace-folds — both read GetCodeStyle on next CalcFoldLevel).
	// Trigger an editor refresh so the visual switch is immediate.
	const int sel = m_syntaxChoice->GetSelection();
	ibCompileCode::SetCodeStyle(sel == 1 ? CODE_CES : CODE_VES);
	if (m_codeEditor) {
		m_codeEditor->Colourise(0, -1);
	}
	event.Skip();
}

///////////////////////////////////////////////////////////////////////////

ibFrameCodeRunner::ibFrameCodeRunner(wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style) :
	// Sessionless by design: codeRunner opens no infobase — it compiles and
	// runs a script buffer, and inherits ibBackendDocFrame only so backend
	// diagnostics land in its output pane. The empty holder says that out
	// loud; the base class has no default constructor precisely so that
	// "no session" is a statement, never an omission.
	ibBackendDocFrame(ibSessionHolder()),
	wxFrame(parent, id, title, pos, size, style), m_compileCode(new ibCompileCode), m_procUnit(new ibProcUnit)
{
	this->SetSizeHints(wxDefaultSize, wxDefaultSize);

	// Menu bar — File / Edit / Help. Standard wxID_* IDs let wxSTC
	// pick up Edit shortcuts directly when the editor has focus.
	wxMenuBar* menuBar = new wxMenuBar();

	wxMenu* menuFile = new wxMenu();
	menuFile->Append(wxID_OPEN,    _("&Open...\tCtrl+O"));
	menuFile->Append(wxID_SAVE,    _("&Save\tCtrl+S"));
	menuFile->Append(wxID_SAVEAS,  _("Save &as..."));
	menuFile->AppendSeparator();
	menuFile->Append(wxID_EXIT,    _("E&xit\tAlt+F4"));
	menuBar->Append(menuFile, _("&File"));

	wxMenu* menuEdit = new wxMenu();
	menuEdit->Append(wxID_UNDO,      _("&Undo\tCtrl+Z"));
	menuEdit->Append(wxID_REDO,      _("&Redo\tCtrl+Y"));
	menuEdit->AppendSeparator();
	menuEdit->Append(wxID_CUT,       _("Cu&t\tCtrl+X"));
	menuEdit->Append(wxID_COPY,      _("&Copy\tCtrl+C"));
	menuEdit->Append(wxID_PASTE,     _("&Paste\tCtrl+V"));
	menuEdit->AppendSeparator();
	menuEdit->Append(wxID_SELECTALL, _("Select &all\tCtrl+A"));
	menuBar->Append(menuEdit, _("&Edit"));

	wxMenu* menuHelp = new wxMenu();
	menuHelp->Append(wxID_ABOUT, _("&About"));
	menuBar->Append(menuHelp, _("&Help"));

	SetMenuBar(menuBar);

	Connect(wxID_OPEN,      wxEVT_MENU, wxCommandEventHandler(ibFrameCodeRunner::OnMenuOpen));
	Connect(wxID_SAVE,      wxEVT_MENU, wxCommandEventHandler(ibFrameCodeRunner::OnMenuSave));
	Connect(wxID_SAVEAS,    wxEVT_MENU, wxCommandEventHandler(ibFrameCodeRunner::OnMenuSaveAs));
	Connect(wxID_EXIT,      wxEVT_MENU, wxCommandEventHandler(ibFrameCodeRunner::OnMenuExit));
	Connect(wxID_ABOUT,     wxEVT_MENU, wxCommandEventHandler(ibFrameCodeRunner::OnMenuAbout));
	Connect(wxID_UNDO,      wxEVT_MENU, wxCommandEventHandler(ibFrameCodeRunner::OnMenuEdit));
	Connect(wxID_REDO,      wxEVT_MENU, wxCommandEventHandler(ibFrameCodeRunner::OnMenuEdit));
	Connect(wxID_CUT,       wxEVT_MENU, wxCommandEventHandler(ibFrameCodeRunner::OnMenuEdit));
	Connect(wxID_COPY,      wxEVT_MENU, wxCommandEventHandler(ibFrameCodeRunner::OnMenuEdit));
	Connect(wxID_PASTE,     wxEVT_MENU, wxCommandEventHandler(ibFrameCodeRunner::OnMenuEdit));
	Connect(wxID_SELECTALL, wxEVT_MENU, wxCommandEventHandler(ibFrameCodeRunner::OnMenuEdit));

	// Top toolbar — same command set as designer's module-editor
	// docview minus document-only items (Goto line / Procedures &
	// functions don't apply without a backing module).
	wxToolBar* toolbar = CreateToolBar(wxTB_FLAT | wxTB_HORIZONTAL);
	toolbar->AddTool(wxID_CR_ADD_COMMENTS,    _("Add comments"),    wxArtProvider::GetBitmapBundle(wxART_ADD_COMMENT,   wxART_DOC_MODULE), _("Add"));
	toolbar->AddTool(wxID_CR_REMOVE_COMMENTS, _("Remove comments"), wxArtProvider::GetBitmapBundle(wxART_REMOVE_COMMENT, wxART_DOC_MODULE), _("Remove"));
	toolbar->AddSeparator();
	toolbar->AddTool(wxID_CR_FORMAT_CODE,     _("Format selection"), wxArtProvider::GetBitmapBundle(wxART_FORMAT_CODE,   wxART_DOC_MODULE), _("Format"));
	toolbar->AddTool(wxID_CR_INCREASE_INDENT, _("Increase indent"),  wxArtProvider::GetBitmapBundle(wxART_GO_FORWARD,    wxART_TOOLBAR),    _("Indent"));
	toolbar->AddTool(wxID_CR_DECREASE_INDENT, _("Decrease indent"),  wxArtProvider::GetBitmapBundle(wxART_GO_BACK,       wxART_TOOLBAR),    _("Unindent"));
	toolbar->AddSeparator();
	toolbar->AddTool(wxID_CR_GOTO_LINE,       _("Goto line"),                wxArtProvider::GetBitmapBundle(wxART_GOTO_LINE,    wxART_DOC_MODULE), _("Goto"));
	toolbar->AddTool(wxID_CR_PROC_AND_FUNC,   _("Procedures and functions"), wxArtProvider::GetBitmapBundle(wxART_PROC_AND_FUNC, wxART_DOC_MODULE), _("Procedures and functions"));
	toolbar->Realize();

	Connect(wxID_CR_ADD_COMMENTS,    wxEVT_TOOL, wxCommandEventHandler(ibFrameCodeRunner::OnToolbarCommand));
	Connect(wxID_CR_REMOVE_COMMENTS, wxEVT_TOOL, wxCommandEventHandler(ibFrameCodeRunner::OnToolbarCommand));
	Connect(wxID_CR_FORMAT_CODE,     wxEVT_TOOL, wxCommandEventHandler(ibFrameCodeRunner::OnToolbarCommand));
	Connect(wxID_CR_INCREASE_INDENT, wxEVT_TOOL, wxCommandEventHandler(ibFrameCodeRunner::OnToolbarCommand));
	Connect(wxID_CR_DECREASE_INDENT, wxEVT_TOOL, wxCommandEventHandler(ibFrameCodeRunner::OnToolbarCommand));
	Connect(wxID_CR_GOTO_LINE,       wxEVT_TOOL, wxCommandEventHandler(ibFrameCodeRunner::OnToolbarCommand));
	Connect(wxID_CR_PROC_AND_FUNC,   wxEVT_TOOL, wxCommandEventHandler(ibFrameCodeRunner::OnToolbarCommand));

	wxBoxSizer* bSizerMain = new wxBoxSizer(wxVERTICAL);

	// Frontend's full code editor — sessionless / document-less for the
	// codeRunner host. Highlighter / fold / auto-indent / format & indent
	// commands all live in ibCodeEditor; only debugger integration is
	// gated off (designer's ibCodeEditorDesigner subclass wires that).
	m_codeEditor = new ibCodeEditor(/*document=*/nullptr, this, wxID_ANY,
		wxDefaultPosition, wxDefaultSize, wxBORDER_THEME);

	// Apply default editor + font/color settings — codeRunner has no
	// mainFrame singleton to pull them from, so go through the public
	// setters with a default-constructed pair.
	m_codeEditor->SetEditorSettings(ibEditorSettings{});
	m_codeEditor->SetFontColorSettings(ibFontColorSettings{});

	bSizerMain->Add(m_codeEditor, 3, wxEXPAND | wxALL, 5);

	wxBoxSizer* bSizerButton = new wxBoxSizer(wxHORIZONTAL);

	// Syntax-mode dropdown — VES (legacy-dialect, with EndProcedure /
	// EndFunction keyword fences) vs CES (C-like, brace-fenced blocks,
	// default). Selection writes through to ibCompileCode::SetCodeStyle
	// so the compiler picks up the mode on the next Compile call.
	wxArrayString syntaxItems;
	syntaxItems.Add(wxT("VES"));
	syntaxItems.Add(wxT("CES"));
	m_syntaxChoice = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, syntaxItems);
	m_syntaxChoice->SetSelection(ibCompileCode::GetCodeStyle() == CODE_CES ? 1 : 0);
	m_syntaxChoice->SetToolTip(_("Script syntax mode"));
	bSizerButton->Add(m_syntaxChoice, 0, wxALL, 5);

	m_buttonSyntaxCheck = new wxButton(this, wxID_ANY, _("Syntax control"));
	m_buttonSyntaxCheck->SetForegroundColour(wxColour(255, 0, 0));
	bSizerButton->Add(m_buttonSyntaxCheck, 0, wxALL, 5);

	m_buttonRunCode = new wxButton(this, wxID_ANY, _("Run code"));
	m_buttonRunCode->SetBackgroundColour(wxColour(128, 255, 128));
	bSizerButton->Add(m_buttonRunCode, 1, wxALL, 5);

	m_buttonClearOutput = new wxButton(this, wxID_ANY, _("Clear output"));
	m_buttonClearOutput->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHT));
	m_buttonClearOutput->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
	bSizerButton->Add(m_buttonClearOutput, 0, wxALL, 5);

	bSizerMain->Add(bSizerButton, 0, wxEXPAND, 5);

	// Output panel — read-only STC for script Message() output.
	m_output = new wxStyledTextCtrl(this, wxID_ANY);
	m_output->SetUseTabs(true);
	m_output->SetTabWidth(4);
	m_output->SetIndent(4);
	m_output->SetTabIndents(true);
	m_output->SetBackSpaceUnIndents(true);
	m_output->SetViewEOL(false);
	m_output->SetViewWhiteSpace(false);
	m_output->SetMarginWidth(2, 0);
	m_output->SetIndentationGuides(true);
	m_output->SetReadOnly(true);
	m_output->SetMarginType(0, wxSTC_MARGIN_NUMBER);
	m_output->SetMarginWidth(0, m_output->TextWidth(wxSTC_STYLE_LINENUMBER, wxT("_99999")));
	m_output->SetSelBackground(true, wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHT));
	m_output->SetSelForeground(true, wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHTTEXT));
	m_output->SetCodePage(wxSTC_CP_UTF8);

	for (int margin = 0; margin < m_output->GetMarginCount(); margin++)
		m_output->SetMarginCursor(margin, wxSTC_CURSORARROW);

	bSizerMain->Add(m_output, 1, wxEXPAND | wxALL, 5);

	this->SetSizer(bSizerMain);
	this->Layout();
	m_statusBar = this->CreateStatusBar(1, wxSTB_SIZEGRIP, wxID_ANY);
	this->Centre(wxBOTH);

	// Register every globally-registered context object (Catalogs,
	// Documents, EnumManager, …) so script can reference them. They
	// flow through CreateBinder into the runtime binder at Execute time.
	for (auto ctor : ibValue::GetListCtorsByType(ibCtorObjectType_object_context)) {
		m_compileCode->AddContextVariable(ctor->GetClassName(), ctor->CreateObject());
	}

	// Connect button events.
	m_buttonSyntaxCheck->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(ibFrameCodeRunner::SyntaxCheckOnButtonClick), nullptr, this);
	m_buttonRunCode->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(ibFrameCodeRunner::RunCodeOnButtonClick), nullptr, this);
	m_buttonClearOutput->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(ibFrameCodeRunner::ClearOutputOnButtonClick), nullptr, this);
	m_syntaxChoice->Connect(wxEVT_COMMAND_CHOICE_SELECTED, wxCommandEventHandler(ibFrameCodeRunner::SyntaxChoiceOnChange), nullptr, this);

	m_codeEditor->SetText("Message(\"Hello world!\");");
}

ibFrameCodeRunner::~ibFrameCodeRunner()
{
	wxDELETE(m_compileCode);
	wxDELETE(m_procUnit);
}
