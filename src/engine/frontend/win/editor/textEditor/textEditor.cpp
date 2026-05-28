#include "textEditor.h"

inline wxColour GetInverse(const wxColour& color)
{
	unsigned char r = color.Red();
	unsigned char g = color.Green();
	unsigned char b = color.Blue();

	return wxColour(r ^ 0xFF, g ^ 0xFF, b ^ 0xFF);
}

ibTextEditor::ibTextEditor(ibDocument* document, wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style, const wxString& name)
	: wxStyledTextCtrl(parent, id, pos, size, style, name), m_document(document)
{
	//Set margin cursor
	for (int margin = 0; margin < wxStyledTextCtrl::GetMarginCount(); margin++)
		wxStyledTextCtrl::SetMarginCursor(margin, wxSTC_CURSORARROW);

	// subscribe to changes in the text control to update the document state
	// when it's modified
	wxStyledTextCtrl::Connect
	(
		wxEVT_STC_CHANGE,
		wxCommandEventHandler(ibTextEditor::OnTextChange),
		nullptr,
		this
	);
}

void ibTextEditor::SetEditorSettings(const ibEditorSettings& settings)
{
	unsigned int m_bIndentationSize = settings.GetIndentSize();

	wxStyledTextCtrl::SetIndent(m_bIndentationSize);
	wxStyledTextCtrl::SetTabWidth(m_bIndentationSize);

	bool useTabs = settings.GetUseTabs();
	bool showWhiteSpace = settings.GetShowWhiteSpace();

	wxStyledTextCtrl::SetUseTabs(useTabs);
	wxStyledTextCtrl::SetTabIndents(useTabs);
	wxStyledTextCtrl::SetBackSpaceUnIndents(useTabs);
	wxStyledTextCtrl::SetViewWhiteSpace(showWhiteSpace);
}

void ibTextEditor::SetFontColorSettings(const ibFontColorSettings& settings)
{
	// For some reason StyleSetFont takes a (non-const) reference, so we need to make
	// a copy before passing it in.
	wxFont font = settings.GetFont();

	wxStyledTextCtrl::StyleClearAll();
	wxStyledTextCtrl::StyleSetFont(wxSTC_STYLE_DEFAULT, font);

	wxStyledTextCtrl::SetSelForeground(true, settings.GetColors(ibFontColorSettings::DisplayItem_Selection).foreColor);
	wxStyledTextCtrl::SetSelBackground(true, settings.GetColors(ibFontColorSettings::DisplayItem_Selection).backColor);

	font = settings.GetFont(ibFontColorSettings::DisplayItem_Default);

	wxStyledTextCtrl::StyleSetFont(wxSTC_C_DEFAULT, font);
	wxStyledTextCtrl::StyleSetFont(wxSTC_C_IDENTIFIER, font);

	wxStyledTextCtrl::StyleSetForeground(wxSTC_C_DEFAULT, settings.GetColors(ibFontColorSettings::DisplayItem_Default).foreColor);
	wxStyledTextCtrl::StyleSetBackground(wxSTC_C_DEFAULT, settings.GetColors(ibFontColorSettings::DisplayItem_Default).backColor);

	wxStyledTextCtrl::StyleSetForeground(wxSTC_STYLE_DEFAULT, settings.GetColors(ibFontColorSettings::DisplayItem_Default).foreColor);
	wxStyledTextCtrl::StyleSetBackground(wxSTC_STYLE_DEFAULT, settings.GetColors(ibFontColorSettings::DisplayItem_Default).backColor);

	wxStyledTextCtrl::StyleSetForeground(wxSTC_C_IDENTIFIER, settings.GetColors(ibFontColorSettings::DisplayItem_Default).foreColor);
	wxStyledTextCtrl::StyleSetBackground(wxSTC_C_IDENTIFIER, settings.GetColors(ibFontColorSettings::DisplayItem_Default).backColor);

	font = settings.GetFont(ibFontColorSettings::DisplayItem_Comment);

	wxStyledTextCtrl::StyleSetFont(wxSTC_C_COMMENT, font);
	wxStyledTextCtrl::StyleSetFont(wxSTC_C_COMMENTLINE, font);
	wxStyledTextCtrl::StyleSetFont(wxSTC_C_COMMENTDOC, font);

	wxStyledTextCtrl::StyleSetForeground(wxSTC_C_COMMENT, settings.GetColors(ibFontColorSettings::DisplayItem_Comment).foreColor);
	wxStyledTextCtrl::StyleSetBackground(wxSTC_C_COMMENT, settings.GetColors(ibFontColorSettings::DisplayItem_Comment).backColor);

	wxStyledTextCtrl::StyleSetForeground(wxSTC_C_COMMENTLINE, settings.GetColors(ibFontColorSettings::DisplayItem_Comment).foreColor);
	wxStyledTextCtrl::StyleSetBackground(wxSTC_C_COMMENTLINE, settings.GetColors(ibFontColorSettings::DisplayItem_Comment).backColor);

	wxStyledTextCtrl::StyleSetForeground(wxSTC_C_COMMENTDOC, settings.GetColors(ibFontColorSettings::DisplayItem_Comment).foreColor);
	wxStyledTextCtrl::StyleSetBackground(wxSTC_C_COMMENTDOC, settings.GetColors(ibFontColorSettings::DisplayItem_Comment).backColor);

	font = settings.GetFont(ibFontColorSettings::DisplayItem_Keyword);

	wxStyledTextCtrl::StyleSetFont(wxSTC_C_WORD, font);
	wxStyledTextCtrl::StyleSetForeground(wxSTC_C_WORD, settings.GetColors(ibFontColorSettings::DisplayItem_Keyword).foreColor);
	wxStyledTextCtrl::StyleSetBackground(wxSTC_C_WORD, settings.GetColors(ibFontColorSettings::DisplayItem_Keyword).backColor);

	font = settings.GetFont(ibFontColorSettings::DisplayItem_Operator);
	wxStyledTextCtrl::StyleSetFont(wxSTC_C_OPERATOR, font);
	wxStyledTextCtrl::StyleSetForeground(wxSTC_C_OPERATOR, settings.GetColors(ibFontColorSettings::DisplayItem_Operator).foreColor);
	wxStyledTextCtrl::StyleSetBackground(wxSTC_C_OPERATOR, settings.GetColors(ibFontColorSettings::DisplayItem_Operator).backColor);

	font = settings.GetFont(ibFontColorSettings::DisplayItem_String);

	wxStyledTextCtrl::StyleSetFont(wxSTC_C_STRING, font);
	wxStyledTextCtrl::StyleSetForeground(wxSTC_C_STRING, settings.GetColors(ibFontColorSettings::DisplayItem_String).foreColor);
	wxStyledTextCtrl::StyleSetBackground(wxSTC_C_STRING, settings.GetColors(ibFontColorSettings::DisplayItem_String).backColor);

	wxStyledTextCtrl::StyleSetFont(wxSTC_C_STRINGEOL, font);
	wxStyledTextCtrl::StyleSetForeground(wxSTC_C_STRINGEOL, settings.GetColors(ibFontColorSettings::DisplayItem_String).foreColor);
	wxStyledTextCtrl::StyleSetBackground(wxSTC_C_STRINGEOL, settings.GetColors(ibFontColorSettings::DisplayItem_String).backColor);

	wxStyledTextCtrl::StyleSetFont(wxSTC_C_CHARACTER, font);
	wxStyledTextCtrl::StyleSetForeground(wxSTC_C_CHARACTER, settings.GetColors(ibFontColorSettings::DisplayItem_String).foreColor);
	wxStyledTextCtrl::StyleSetBackground(wxSTC_C_CHARACTER, settings.GetColors(ibFontColorSettings::DisplayItem_String).backColor);

	font = settings.GetFont(ibFontColorSettings::DisplayItem_Number);

	wxStyledTextCtrl::StyleSetFont(wxSTC_C_NUMBER, font);
	wxStyledTextCtrl::StyleSetForeground(wxSTC_C_NUMBER, settings.GetColors(ibFontColorSettings::DisplayItem_Number).foreColor);
	wxStyledTextCtrl::StyleSetBackground(wxSTC_C_NUMBER, settings.GetColors(ibFontColorSettings::DisplayItem_Number).backColor);

	// Apply the full font to the line-number margin, not just its size — see
	// codeEditor.cpp for the same fix. StyleSetSize alone leaves the margin
	// with the default monospace face until a style cascade refresh occurs.
	wxStyledTextCtrl::StyleSetFont(wxSTC_STYLE_LINENUMBER, font);
	wxStyledTextCtrl::StyleSetSize(wxSTC_STYLE_LINENUMBER, font.GetPointSize());

	// Set the caret color as the inverse of the background color so it's always visible.
	wxStyledTextCtrl::SetCaretForeground(GetInverse(settings.GetColors(ibFontColorSettings::DisplayItem_Default).backColor));
}

#include "frontend/docView/docView.h"

void ibTextEditor::OnTextChange(wxCommandEvent& event)
{
	if (m_document != nullptr)
		m_document->Modify(true);
	
	event.Skip();
}
