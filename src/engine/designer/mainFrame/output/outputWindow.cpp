////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : output window
////////////////////////////////////////////////////////////////////////////

#include "outputWindow.h"
#include "frontend/mainFrame/settings/fontcolorsettings.h"

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

#include "mainFrame/mainFrameDesigner.h"

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
	if (ibFrontendMainFrameDesigner::GetFrame())
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

void ibOutputWindow::OutputMessage(const wxString& strMessage,
	const wxString& strFileName, const wxString& strDocPath,
	int currLine)
{
	SharedOutput(strMessage, ibStatusMessage::ibStatusMessage_Information,
		strFileName, strDocPath, currLine);
}

void ibOutputWindow::OutputWarning(const wxString& strMessage,
	const wxString& strFileName, const wxString& strDocPath,
	int currLine)
{
	SharedOutput(strMessage, ibStatusMessage::ibStatusMessage_Warning,
		strFileName, strDocPath,
		currLine);
}

void ibOutputWindow::OutputError(const wxString& strMessage,
	const wxString& strFileName, const wxString& strDocPath,
	int currLine)
{
	SharedOutput(strMessage, ibStatusMessage::ibStatusMessage_Error,
		strFileName, strDocPath,
		currLine);
}

void ibOutputWindow::SharedOutput(const wxString& strMessage, ibStatusMessage status,
	const wxString& strFileName, const wxString& strDocPath,
	int currLine)
{
	int beforeAppendPosition = GetInsertionPoint();
	int beforeAppendLastPosition = GetLastPosition();

	Freeze();

	unsigned int lastLine = GetLineCount();

	if (currLine != wxNOT_FOUND) {
		m_listCodeInfo.insert_or_assign(lastLine, 
			lineOutputData_t{ 
				strFileName, 
				strDocPath,
				currLine 
			}
		);
	}

	SetEditable(true);
	AppendText(strMessage + '\n');
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

#include "frontend/docView/docView.h"
#include "backend/metadataConfiguration.h"

void ibOutputWindow::OnDoubleClick(wxMouseEvent& event)
{
	wxTextCoord col, row;
	HitTest(event.GetPosition(), &col, &row);

	for (auto &pair : m_listCodeInfo) {

		if (pair.first >= row) {

			auto code = pair.second;

			if (code.m_fileName.IsEmpty()) {
				ibBackendMetadataTree* metaTree = activeMetaData->GetMetaTree();
				wxASSERT(metaTree);
				metaTree->EditModule(code.m_docPath, code.m_currLine, false);
			}

			if (!code.m_fileName.IsEmpty()) {
				ibMetaDataDocument* foundedDoc = dynamic_cast<ibMetaDataDocument*>(
					docManager->FindDocumentByPath(code.m_fileName)
					);

				if (foundedDoc == nullptr) {
					foundedDoc = dynamic_cast<ibMetaDataDocument*>(
						docManager->CreateDocument(code.m_fileName, ibDOC_SILENT)
						);
				}

				if (foundedDoc != nullptr) {
					ibMetaData* metadata = foundedDoc->GetMetaData();
					wxASSERT(metadata);
					ibBackendMetadataTree* metaTree = metadata->GetMetaTree();
					wxASSERT(metaTree);
					metaTree->EditModule(code.m_docPath, code.m_currLine, false);
				}
			}
			break;
		}
	}

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

	wxMenu* popupMenu = new wxMenu;

	wxMenuItem* menuItemCopy = popupMenu->Append(idcmdCopy, _("Copy"));
	menuItemCopy->Enable(wxStyledTextCtrl::CanCopy());
	wxMenuItem* menuItemClear = popupMenu->Append(idcmdClear, _("Clear"));

	wxStyledTextCtrl::PopupMenu(popupMenu, pt);
	//event.Skip();
}

void ibOutputWindow::OnClearOutput(wxCommandEvent& event)
{
	m_listCodeInfo.clear();

	SetEditable(true);
	wxStyledTextCtrl::ClearAll();
	SetEditable(false);

	event.Skip();
}

void ibOutputWindow::OnKeyDown(wxKeyEvent& event)
{
	event.Skip();
}
