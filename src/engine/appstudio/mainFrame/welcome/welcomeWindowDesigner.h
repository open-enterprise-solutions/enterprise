#ifndef _DESIGNER_WELCOME_WINDOW_H__
#define _DESIGNER_WELCOME_WINDOW_H__

#include "frmclient/mainFrame/welcome/welcomeWindow.h"

class ibKeyBinder;

// THE DESIGNER'S WELCOME — the greeting, and under it the keys of the commands a configuration is worked on with: run it,
// run it without the debugger, find, the syntax helper. Each key is read from the window's binder as it is drawn, so a key
// the person changed in Options shows at once, and a command they took the key off is not shown.
class ibDesignerWelcomeWindow : public ibWelcomeWindow {
public:

	ibDesignerWelcomeWindow(wxWindow* parent, const wxString& fullName, const ibKeyBinder& keys);

protected:

	virtual std::vector<std::pair<wxString, wxString>> GetKeys() const override;

private:

	const ibKeyBinder& m_keyBinder;   // the window's, which outlives its welcome
};

#endif
