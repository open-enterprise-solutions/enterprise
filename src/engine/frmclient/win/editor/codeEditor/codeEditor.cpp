#include "codeEditor.h"

#include "frmclient/docView/docView.h"   // ibMetaDocument — a document the text editor marks modified

ibCodeEditor::ibCodeEditor(ibMetaDocument* document, wxWindow* parent, wxWindowID id,
	const wxPoint& pos, const wxSize& size, long style, const wxString& strName)
	: ibTextEditor(document, parent, id, pos, size, style, strName)
{
}
