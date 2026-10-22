#ifndef _FRMCLIENT_VIEW_WIDGETS_H__
#define _FRMCLIENT_VIEW_WIDGETS_H__

#include <wx/gauge.h>
#include <wx/html/htmlwin.h>
#include <wx/radiobut.h>
#include <wx/slider.h>
#include <wx/statline.h>

#include "frmclient/visualView/ctrl/window.h"

class ibControlStaticTextValue;
class ibControlTextEditor;
class ibControlButton;
class ibControlCheckbox;
class ibTextEditor;

// THE WIDGETS OF A VIEW — the desktop's (frontend/visualView/ctrl/widgets.h), each on the control it drew with, now
// drawing what the node says.

// A caption — and, bound, the value beside it, a link where there is something to open.
class ibValueStaticText : public ibValueWindow {
public:
	ibValueStaticText(ibVisualHostClient& host, long long controlId) : ibValueWindow(host, controlId) {}

	virtual void Create(wxWindow* parent, const ibProtocolNode& node) override;
	virtual void Update(const ibProtocolNode& node) override;
	virtual wxWindow* GetWindow() const override;

private:
	ibControlStaticTextValue* m_staticText = nullptr;
};

// A field. What is typed is the person's until it is committed — Enter, or the focus gone: the server is told the
// typing started (Input) and given the text then (Change); a frame drawn in between does not overwrite it.
class ibValueTextCtrl : public ibValueWindow {
public:
	ibValueTextCtrl(ibVisualHostClient& host, long long controlId) : ibValueWindow(host, controlId) {}

	virtual void Create(wxWindow* parent, const ibProtocolNode& node) override;
	virtual void Update(const ibProtocolNode& node) override;
	virtual wxWindow* GetWindow() const override;

	virtual bool SaveEdit(ibViewEdit& edit) const override;
	virtual void RestoreEdit(const ibViewEdit& edit) override;

private:
	void Commit();

	void OnTextInput(wxCommandEvent& event);
	void OnTextEnter(wxCommandEvent& event);
	void OnKillFocus(wxFocusEvent& event);

	ibControlTextEditor* m_textEditor = nullptr;
	bool                 m_editing = false;   // typed into, not committed yet
};

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

	ibControlButton* m_button = nullptr;
	ibProtocolNode   m_members;   // a copy — the frame changes under a handle into it
};

class ibValueCheckbox : public ibValueWindow {
public:
	ibValueCheckbox(ibVisualHostClient& host, long long controlId) : ibValueWindow(host, controlId) {}

	virtual void Create(wxWindow* parent, const ibProtocolNode& node) override;
	virtual void Update(const ibProtocolNode& node) override;
	virtual wxWindow* GetWindow() const override;

private:
	ibControlCheckbox* m_checkbox = nullptr;
};

// A radio button — shown selected as the State says; it has no data source, and its click stays on the client, as the
// desktop's did.
class ibValueRadioButton : public ibValueWindow {
public:
	ibValueRadioButton(ibVisualHostClient& host, long long controlId) : ibValueWindow(host, controlId) {}

	virtual void Create(wxWindow* parent, const ibProtocolNode& node) override;
	virtual void Update(const ibProtocolNode& node) override;
	virtual wxWindow* GetWindow() const override { return m_radioButton; }

private:
	wxRadioButton* m_radioButton = nullptr;
};

// A slider between its ends, its thumb where the form put it; moved, it stays on the client, as the desktop's did. Its
// orientation is what it is built with — turned, it is built anew (ibVisualHostClient's shape).
class ibValueSlider : public ibValueWindow {
public:
	ibValueSlider(ibVisualHostClient& host, long long controlId) : ibValueWindow(host, controlId) {}

	virtual void Create(wxWindow* parent, const ibProtocolNode& node) override;
	virtual void Update(const ibProtocolNode& node) override;
	virtual wxWindow* GetWindow() const override { return m_slider; }

private:
	wxSlider* m_slider = nullptr;
};

// A progress bar, filled to its value of its range. Its orientation is what it is built with, as a slider's is.
class ibValueGauge : public ibValueWindow {
public:
	ibValueGauge(ibVisualHostClient& host, long long controlId) : ibValueWindow(host, controlId) {}

	virtual void Create(wxWindow* parent, const ibProtocolNode& node) override;
	virtual void Update(const ibProtocolNode& node) override;
	virtual wxWindow* GetWindow() const override { return m_gauge; }

private:
	wxGauge* m_gauge = nullptr;
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
	wxPanel* m_chartBox = nullptr;
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
	wxPanel* m_panel = nullptr;
};

// A line. Its orientation is what it is built with — a line turned is built anew (ibVisualHostClient's shape).
class ibValueStaticLine : public ibValueWindow {
public:
	ibValueStaticLine(ibVisualHostClient& host, long long controlId) : ibValueWindow(host, controlId) {}

	virtual void Create(wxWindow* parent, const ibProtocolNode& node) override;
	virtual void Update(const ibProtocolNode& node) override;
	virtual wxWindow* GetWindow() const override { return m_staticLine; }

private:
	wxStaticLine* m_staticLine = nullptr;
};

#endif
