#ifndef _FRMCLIENT_VIEW_WIDGETS_H__
#define _FRMCLIENT_VIEW_WIDGETS_H__

#include <wx/html/htmlwin.h>

#include "frmclient/visualView/ctrl/window.h"
#include "deskgui/univ/panel.h"

class ibButton;
class ibTextEditor;

// THE WIDGETS OF A VIEW — the old desktop's, each on the control it drew with, now
// drawing what the node says.

// A TEXT BOX — the desktop's text editor, many lines: the State's Text shown, ReadOnly or typed into. What is typed is the
// person's until the box loses its focus, then goes back whole (Change {Text}), as a field's does.
class ibValueTextBox : public ibValueFrame {
public:
	ibValueTextBox(ibVisualHostClient& host, long long controlId) : ibValueFrame(host, controlId) {}

	virtual void Create(wxWindow* parent, const ibProtocolNode& node) override;
	virtual void Update(const ibProtocolNode& node) override;
	virtual wxWindow* GetWindow() const override;

private:
	void OnKillFocus(wxFocusEvent& event);

	ibTextEditor* m_textEditor = nullptr;
	wxString      m_text;   // the server's, as shown last — the editor's other than it: typed, not committed yet
};

// A button. A group command opens its commands to pick one from — the State's Members, kept from the draw to the press.
class ibValueButton : public ibValueWindow {
public:
	ibValueButton(ibVisualHostClient& host, long long controlId) : ibValueWindow(host, controlId) {}

	virtual void Create(wxWindow* parent, const ibProtocolNode& node) override;
	virtual void Update(const ibProtocolNode& node) override;
	virtual wxWindow* GetWindow() const override;

private:
	void OnButtonPressed(wxCommandEvent& event);

	ibButton*      m_button = nullptr;
	ibProtocolNode m_members;   // a copy — the frame changes under a handle into it
};

// An HTML box — the page its form's code set (the State's Page), the box's whole content.
class ibValueHTMLBox : public ibValueWindow {
public:
	ibValueHTMLBox(ibVisualHostClient& host, long long controlId) : ibValueWindow(host, controlId) {}

	virtual void Create(wxWindow* parent, const ibProtocolNode& node) override;
	virtual void Update(const ibProtocolNode& node) override;
	virtual wxWindow* GetWindow() const override;

private:
	wxHtmlWindow* m_htmlBox = nullptr;
	wxString      m_page;   // shown — set again only when the server's is another
};

// A chart box — its place in the layout, empty: the desktop's chart was a sample, drawn from no data of the form's.
class ibValueChartBox : public ibValueWindow {
public:
	ibValueChartBox(ibVisualHostClient& host, long long controlId) : ibValueWindow(host, controlId) {}

	virtual void Create(wxWindow* parent, const ibProtocolNode& node) override;
	virtual void Update(const ibProtocolNode& node) override;
	virtual wxWindow* GetWindow() const override { return m_chartBox; }

private:
	ibPanel* m_chartBox = nullptr;
};

// A control of a type this client does not draw yet: its place in the layout kept, and said so in the middle of it.
// What is in it is not drawn.
class ibViewPlaceholder : public ibValueFrame {
public:
	ibViewPlaceholder(ibVisualHostClient& host, long long controlId) : ibValueFrame(host, controlId) {}

	virtual void Create(wxWindow* parent, const ibProtocolNode& node) override;
	virtual void Update(const ibProtocolNode& node) override;
	virtual wxWindow* GetWindow() const override { return m_panel; }
	virtual bool IsPlaceholder() const override { return true; }

private:
	ibPanel* m_panel = nullptr;
};

#endif
