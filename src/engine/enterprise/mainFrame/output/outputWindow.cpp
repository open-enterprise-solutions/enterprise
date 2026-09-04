////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : output window
////////////////////////////////////////////////////////////////////////////

#include "outputWindow.h"

#include "mainFrame/mainFrameEnterprise.h"
#include "frontend/mainFrame/settings/fontcolorsettings.h"

#include "backend/debugger/debugServer.h"   // …and on to whoever is debugging this run

/** Enumeration of commands and child windows. */
enum
{
	idcmdUndo = 10,
	idcmdRedo = 11,
	idcmdCut = 12,
	idcmdCopy = 13,
	idcmdPaste = 14,
	idcmdDelete = 15,
	idcmdSelectAll = 16,

	idcmdClear = 17,
};

wxBEGIN_EVENT_TABLE(ibOutputWindow, wxStyledTextCtrl)
EVT_LEFT_DCLICK(ibOutputWindow::OnDoubleClick)
EVT_KEY_DOWN(ibOutputWindow::OnKeyDown)
EVT_CONTEXT_MENU(ibOutputWindow::OnContextMenu)
EVT_MENU(idcmdClear, ibOutputWindow::OnClearOutput)
wxEND_EVENT_TABLE()

#define DEF_LINENUMBER_ID 0
#define DEF_IMAGE_ID 1

ibOutputWindow::ibOutputWindow(class ibFrontendMainFrame* parent, wxWindowID winid)
	: wxStyledTextCtrl(parent, winid, wxDefaultPosition, wxDefaultSize)
{
	// initialize styles
	StyleClearAll();

	//set Lexer to LEX_CONTAINER: This will trigger the styleneeded event so you can do your own highlighting
	SetLexer(wxSTC_LEX_CONTAINER);

	//Set margin cursor
	for (int margin = 0; margin < GetMarginCount(); margin++)
		SetMarginCursor(margin, wxSTC_CURSORARROW);

	MarkerDefine(ibStatusMessage_Information, wxSTC_MARK_SHORTARROW, *wxWHITE, *wxBLACK);
	MarkerDefine(ibStatusMessage_Warning, wxSTC_MARK_SHORTARROW, *wxWHITE, *wxYELLOW);
	MarkerDefine(ibStatusMessage_Error, wxSTC_MARK_SHORTARROW, *wxWHITE, *wxRED);

	wxAcceleratorEntry entries[2];
	entries[0].Set(wxACCEL_CTRL, (int)'A', idcmdSelectAll);
	entries[1].Set(wxACCEL_CTRL, (int)'C', idcmdCopy);

	wxAcceleratorTable accel(2, entries);
	SetAcceleratorTable(accel);

	if (parent != nullptr)
		SetFontColorSettings(parent->GetFontColorSettings());
}

///////////////////////////////////////////////////////////////////////////////

ibOutputWindow* ibOutputWindow::GetOutputWindow()
{
	if (ibFrontendMainFrameEnterprise::GetFrame())
		return mainFrame->GetOutputWindow();
	return nullptr;
}

///////////////////////////////////////////////////////////////////////////////

void ibOutputWindow::SetFontColorSettings(const ibFontColorSettings& settings)
{
	// For some reason StyleSetFont takes a (non-const) reference, so we need to make
	// a copy before passing it in.
	wxFont font = settings.GetFont();

	StyleClearAll();
	StyleSetFont(wxSTC_STYLE_DEFAULT, font);

	SetSelForeground(true, settings.GetColors(ibFontColorSettings::DisplayItem_Selection).foreColor);
	SetSelBackground(true, settings.GetColors(ibFontColorSettings::DisplayItem_Selection).backColor);

	font = settings.GetFont(ibFontColorSettings::DisplayItem_Default);

	StyleSetFont(wxSTC_C_DEFAULT, font);
	StyleSetFont(wxSTC_C_IDENTIFIER, font);

	SetMarginType(DEF_LINENUMBER_ID, wxSTC_MARGIN_NUMBER);
	SetMarginWidth(DEF_LINENUMBER_ID, 0);

	// set margin as unused
	SetMarginType(DEF_IMAGE_ID, wxSTC_MARGIN_SYMBOL);
	SetMarginMask(DEF_IMAGE_ID, ~(1024 | 256 | 512 | 128 | 64 | wxSTC_MASK_FOLDERS));
	StyleSetBackground(DEF_IMAGE_ID, *wxWHITE);

	SetMarginWidth(DEF_IMAGE_ID, FromDIP(16));
	SetMarginSensitive(DEF_IMAGE_ID, true);

	SetEditable(false);
}

void ibOutputWindow::OutputMessage(const wxString& message,
	const wxString& strFileName, const wxString& strDocPath,
	int currLine)
{
	SharedOutput(message, ibStatusMessage::ibStatusMessage_Information,
		strFileName, strDocPath, currLine);
}

void ibOutputWindow::OutputWarning(const wxString& message,
	const wxString& strFileName, const wxString& strDocPath,
	int currLine)
{
	SharedOutput(message, ibStatusMessage::ibStatusMessage_Warning,
		strFileName, strDocPath,
		currLine);
}

void ibOutputWindow::OutputError(const wxString& message,
	const wxString& strFileName, const wxString& strDocPath,
	int currLine)
{
	SharedOutput(message, ibStatusMessage::ibStatusMessage_Error,
		strFileName, strDocPath,
		currLine);
}

void ibOutputWindow::SharedOutput(const wxString& message, ibStatusMessage status,
	const wxString& strFileName, const wxString& strDocPath,
	int currLine)
{
	// ⭐⭐ AND INTO THE OUTPUT BUFFER OF WHOEVER ASKED FOR THIS RUN. The pane belongs to a WINDOW and
	// the window belongs to THIS PROCESS, so a run started from the designer says everything on a
	// screen its author is not sitting in front of.
	//
	// 🛑 NOT down the error road (`SendErrorToClient`). That one ends in the DESIGNER'S OUTPUT PANE,
	// and it is a person's workspace: it carries failures they chose to send over, with a module and
	// a line to jump to. Pouring a run's narration in there paints their window with output nobody
	// asked for, and it raises the designer to the foreground on every line (Max, 2026-09-04: *"I
	// read only the errors, when I press the button myself"*).
	//
	// The eval channel is the other reader's, and the designer shows none of it — the assistant
	// collects the lines and decides what, if anything, is worth repeating.
	//
	// ⚠ ONLY WHEN THE RUN IS BEING DEBUGGED: with nobody attached the sender opens no socket and
	// the line simply stays in this window, where the person can see it.
	if (debugServer != nullptr && debugServer->IsDebugging()) {

		// ⭐ TRANSLATED HERE, because here is where the application's vocabulary meets the wire's.
		// The protocol keeps its own word for a level (debugDefs.h) so that renumbering an
		// application enum cannot silently change what two processes mean by the same byte.
		const MessageType type =
			  status == ibStatusMessage::ibStatusMessage_Error   ? MessageType_Error
			: status == ibStatusMessage::ibStatusMessage_Warning ? MessageType_Warning
			                                                     : MessageType_Normal;

		debugServer->SendEvalMessage(message, type);
	}

	int beforeAppendPosition = GetInsertionPoint();
	int beforeAppendLastPosition = GetLastPosition();

	Freeze();

	unsigned int lastLine = GetLineCount();

	SetEditable(true);
	AppendText(message + '\n');
	SetEditable(false);

	MarkerAdd(
		lastLine - 1,
		status);

	Thaw();

	SetInsertionPoint(beforeAppendPosition);

	if (beforeAppendPosition == beforeAppendLastPosition) {
		SetInsertionPoint(GetLastPosition());
		ShowPosition(GetLastPosition());
		ScrollLines(-1);
	}

	if (mainFrame->IsShown()) {
		wxStyledTextCtrl::SetFocus();
	}

	// update output window 
	mainFrame->Update();
}

int ibOutputWindow::GetCurrentLine() const
{
	long pos = GetInsertionPoint();

	long x, y;
	PositionToXY(pos, &x, &y);

	return y;
}

void ibOutputWindow::OnDoubleClick(wxMouseEvent& event)
{
	wxTextCoord col, row;
	HitTest(event.GetPosition(), &col, &row);
	event.Skip();
}

void ibOutputWindow::OnContextMenu(wxContextMenuEvent& event)
{
	wxPoint pt = event.GetPosition();
	ScreenToClient(&pt.x, &pt.y);

	/*
	  Show context menu at event point if it's within the window,
	  or at caret location if not
	*/
	wxHitTest ht = wxStyledTextCtrl::HitTest(pt);
	if (ht != wxHT_WINDOW_INSIDE) {
		pt = this->PointFromPosition(this->GetCurrentPos());
	}

	// On the stack — PopupMenu does not take ownership and blocks until dismissed.
	wxMenu popupMenu;

	wxMenuItem* menuItemCopy = popupMenu.Append(idcmdCopy, _("Copy"));
	menuItemCopy->Enable(wxStyledTextCtrl::CanCopy());
	wxMenuItem* menuItemClear = popupMenu.Append(idcmdClear, _("Clear"));

	wxStyledTextCtrl::PopupMenu(&popupMenu, pt);
	//event.Skip();
}

void ibOutputWindow::OnClearOutput(wxCommandEvent& event)
{
	SetEditable(true);
	wxStyledTextCtrl::ClearAll();
	SetEditable(false);

	event.Skip();
}

void ibOutputWindow::OnKeyDown(wxKeyEvent& event)
{
	event.Skip();
}
