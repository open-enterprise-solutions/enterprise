#ifndef __TITLE__
#define __TITLE__

#include <wx/panel.h>

// A caption strip in the palette's colours, over one of the form editor's trees.
class ibPanelTitle : public wxPanel {
public:
	ibPanelTitle(wxWindow* parent, const wxString& title);

	// Puts the inner window under a caption: a new panel takes its place on its parent and holds both.
	static wxWindow* CreateTitle(wxWindow* inner, const wxString& title);
};

#endif //__TITLE__
