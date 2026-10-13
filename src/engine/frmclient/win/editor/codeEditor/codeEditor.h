#ifndef _FRMCLIENT_CODE_EDITOR_H__
#define _FRMCLIENT_CODE_EDITOR_H__

// THE SCRIPT EDITOR — the desktop's ibCodeEditor (frontend/win/editor/codeEditor/codeEditor.h): a module's, which it
// loads, saves and checks against the configuration, prompts its names and stops on its breakpoints — the designer's
// and the debugger's. What a client's window takes of it is the text, shown in the editors' settings: the text editor.

#include "frmclient/win/editor/textEditor/textEditor.h"

class ibMetaDocument;

class ibCodeEditor : public ibTextEditor {
public:
	ibCodeEditor(ibMetaDocument* document, wxWindow* parent, wxWindowID id = wxID_ANY,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize, long style = 0,
		const wxString& strName = wxSTCNameStr);
};

#endif
