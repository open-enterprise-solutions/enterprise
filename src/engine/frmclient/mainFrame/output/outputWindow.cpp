////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : output window
////////////////////////////////////////////////////////////////////////////

#include "outputWindow.h"

#include "frmclient/artProvider/artProvider.h"
#include "frmclient/mainFrame/mainFrame.h"
#include "frmclient/mainFrame/settings/fontcolorsettings.h"

/** Enumeration of commands and child windows. */
enum
{
	idcmdCopy = 13,
	idcmdSelectAll = 16,

	idcmdClear = 17,
};

wxBEGIN_EVENT_TABLE(ibOutputWindow, wxStyledTextCtrl)
EVT_CONTEXT_MENU(ibOutputWindow::OnContextMenu)
EVT_MENU(idcmdClear, ibOutputWindow::OnClearOutput)
wxEND_EVENT_TABLE()

#define DEF_LINENUMBER_ID 0
#define DEF_IMAGE_ID 1

ibOutputWindow::ibOutputWindow(ibFrontendMainFrame* parent, wxWindowID winid)
	: wxStyledTextCtrl(parent, winid, wxDefaultPosition, wxDefaultSize)
{
	// initialize styles
	StyleClearAll();

	//set Lexer to LEX_CONTAINER: This will trigger the styleneeded event so you can do your own highlighting
	SetLexer(wxSTC_LEX_CONTAINER);

	//Set margin cursor
	for (int margin = 0; margin < GetMarginCount(); margin++)
		SetMarginCursor(margin, wxSTC_CURSORARROW);

	// The level of a message is the picture in the margin - the provider's (artProvider/service/output*.svg).
	const wxSize markerSize = FromDIP(wxSize(12, 12));
	MarkerDefineBitmap(static_cast<int>(ibProtocolMessageLevel::Information), wxArtProvider::GetBitmap(wxART_OUTPUT_INFORMATION, wxART_SERVICE, markerSize));
	MarkerDefineBitmap(static_cast<int>(ibProtocolMessageLevel::Warning), wxArtProvider::GetBitmap(wxART_OUTPUT_WARNING, wxART_SERVICE, markerSize));
	MarkerDefineBitmap(static_cast<int>(ibProtocolMessageLevel::Error), wxArtProvider::GetBitmap(wxART_OUTPUT_ERROR, wxART_SERVICE, markerSize));

	wxAcceleratorEntry entries[2];
	entries[0].Set(wxACCEL_CTRL, (int)'A', idcmdSelectAll);
	entries[1].Set(wxACCEL_CTRL, (int)'C', idcmdCopy);

	wxAcceleratorTable accel(2, entries);
	SetAcceleratorTable(accel);

	if (parent != nullptr)
		SetFontColorSettings(parent->GetFontColorSettings());
}

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

void ibOutputWindow::Output(const wxString& message, ibProtocolMessageLevel level)
{
	const int beforeAppendPosition = GetInsertionPoint();
	const int beforeAppendLastPosition = GetLastPosition();

	Freeze();

	const int lastLine = GetLineCount();

	SetEditable(true);
	AppendText(message + '\n');
	SetEditable(false);

	MarkerAdd(lastLine - 1, static_cast<int>(level));

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

void ibOutputWindow::Clear()
{
	SetEditable(true);
	wxStyledTextCtrl::ClearAll();
	SetEditable(false);
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
	popupMenu.Append(idcmdClear, _("Clear"));

	wxStyledTextCtrl::PopupMenu(&popupMenu, pt);
}

void ibOutputWindow::OnClearOutput(wxCommandEvent& event)
{
	Clear();
	event.Skip();
}
