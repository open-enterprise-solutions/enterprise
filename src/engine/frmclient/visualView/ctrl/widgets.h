#ifndef _FRMCLIENT_VIEW_WIDGETS_H__
#define _FRMCLIENT_VIEW_WIDGETS_H__

#include <wx/gauge.h>
#include <wx/html/htmlwin.h>
#include <wx/radiobut.h>
#include <wx/slider.h>
#include <wx/statline.h>

#include "frmclient/visualView/ctrl/window.h"
#include "frmclient/visualView/controlEnum.h"   // the title's location, a button's representation, a line's orient
#include "frmclient/backend/propertyManager/property/propertyFormat.h"
#include "frmclient/backend/propertyManager/property/propertyNumber.h"
#include "frmclient/backend/propertyManager/property/propertyEnum.h"

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

	//load & save object in control
	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

private:

	ibPropertyCategory* m_categoryStaticText = ibPropertyObject::CreatePropertyCategory(wxT("StaticText"), _("Static text"));
	ibPropertyBoolean* m_propertyMarkup = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryStaticText, wxT("Markup"), _("Markup"),
		_("Kept for older forms and saved with the form; it has no effect - the caption is shown as plain text."), true);
	ibPropertyUInteger* m_propertyWrap = ibPropertyObject::CreateProperty<ibPropertyUInteger>(m_categoryStaticText, wxT("Wrap"), _("Wrap"),
		_("Kept for older forms and saved with the form; it has no effect. For several lines, put line breaks into the title."), 0);
	ibPropertyTString* m_propertyTitle = ibPropertyObject::CreateProperty<ibPropertyTString>(m_categoryStaticText, wxT("Title"), _("Title"),
		_("The caption text. Empty on a bound static text: the bound field's synonym. Can be written per language and changed from code while the form is open."),
		wxT("Static text"));
	ibPropertyEnum<ibValueEnumTitleLocation>* m_propertyTitleLocation = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumTitleLocation>>(m_categoryStaticText, wxT("TitleLocation"), _("Title location"),
		_("Which side of the value the caption sits on when the static text is bound: left (the default) or right."),
		ibTitleLocation::eLeft);

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

	//load & save object in control
	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

	virtual bool SaveEdit(ibViewEdit& edit) const override;
	virtual void RestoreEdit(const ibViewEdit& edit) override;

private:

	ibPropertyCategory* m_categoryText = ibPropertyObject::CreatePropertyCategory(wxT("Textbox"), _("Textbox"));
	ibPropertyTString* m_propertyTitle = ibPropertyObject::CreateProperty<ibPropertyTString>(m_categoryText, wxT("Title"), _("Title"),
		_("The field's caption, shown beside it. Empty: the bound field's synonym. Can be written per language."), wxT(""));
	ibPropertyBoolean* m_propertyPasswordMode = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryText, wxT("PasswordMode"), _("Password mode"), _("Whether the field hides what is typed behind placeholder characters, for secrets such as passwords. Off by default."), false);
	ibPropertyBoolean* m_propertyMultilineMode = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryText, wxT("MultilineMode"), _("Multiline mode"), _("Whether the field accepts several lines of text (Enter starts a new line) - for comments and addresses. Off by default: Enter finishes the input."), false);
	ibPropertyBoolean* m_propertyTexteditMode = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryText, wxT("TexteditMode"), _("Textedit mode"), _("Whether the value can be typed into the field. Off: it can only be picked with the Select button or cleared. On by default; a field bound through a reference path is read-only whatever this says."), true);
	ibPropertyFormat* m_propertyFormat = ibPropertyObject::CreateProperty<ibPropertyFormat>(m_categoryText, wxT("Format"), _("Format"),
		_("How the field shows its value, written per language: digits after the point, separators, a date pattern. Empty: the bound attribute's format, and without one a number shows as many digits after the point as its type keeps."), wxT(""));
	ibPropertyCategory* m_categoryButton = ibPropertyObject::CreatePropertyCategory(wxT("Button"), _("Button"));
	ibPropertyBoolean* m_propertySelectButton = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryButton, wxT("ButtonSelect"), _("Select button"),
		_("Whether the field shows the Select button (...), which opens a choice form or list to pick the value from. On by default."), true);
	ibPropertyBoolean* m_propertyClearButton = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryButton, wxT("ButtonClear"), _("Clear button"),
		_("Whether the field shows the Clear button (X), which empties the value. On by default."), true);
	ibPropertyBoolean* m_propertyOpenButton = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryButton, wxT("ButtonOpen"), _("Open button"),
		_("Whether the field shows the Open button, which opens the referenced object's form. Off by default."), false);

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

	//load & save object in control
	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

private:

	ibPropertyCategory* m_categoryButton = ibPropertyObject::CreatePropertyCategory(wxT("Button"), _("Button"));
	ibPropertyTString* m_propertyTitle = ibPropertyObject::CreateProperty<ibPropertyTString>(m_categoryButton, wxT("Title"), _("Title"),
		_("The text on the button. Can be written per language and changed from code while the form is open."), wxT("Button"));
	ibPropertyEnum<ibValueEnumRepresentation>* m_propertyRepresentation = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumRepresentation>>(m_categoryButton, wxT("Representation"), _("Representation"),
		_("What the button shows: text, picture, or both. Auto: what the bound command prefers (Close is a picture alone, Add a picture with text); picture and text when no command is bound."),
		ibRepresentation::ibRepresentation_Auto);

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

	//load & save object in control
	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

private:

	ibPropertyCategory* m_categoryCheckBox = ibPropertyObject::CreatePropertyCategory(wxT("Checkbox"), _("Checkbox"));
	ibPropertyTString* m_propertyTitle = ibPropertyObject::CreateProperty<ibPropertyTString>(m_categoryCheckBox, wxT("Title"), _("Title"),
		_("The checkbox's caption. Empty: the bound field's synonym. Can be written per language."), wxT(""));
	ibPropertyEnum<ibValueEnumTitleLocation>* m_propertyTitleLocation = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumTitleLocation>>(m_categoryCheckBox, wxT("TitleLocation"), _("Title location"),
		_("Which side of the box the caption sits on: left (the default) or right."),
		ibTitleLocation::eLeft);

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

	//load & save object in control
	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

private:

	ibPropertyCategory* m_categoryRadioButton = ibPropertyObject::CreatePropertyCategory(wxT("RadioButton"), _("Radio button"));
	ibPropertyTString* m_propertyTitle = ibPropertyObject::CreateProperty<ibPropertyTString>(m_categoryRadioButton, wxT("Title"), _("Title"),
		_("The radio button's caption. Can be written per language."), wxT("Radio button"));
	ibPropertyBoolean* m_propertySelected = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryRadioButton, wxT("Selected"), _("Selected"),
		_("Whether the radio button is shown selected. A radio button has no data source of its own."));

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

	//load & save object in control
	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

private:

	ibPropertyCategory* m_categorySlider = ibPropertyObject::CreatePropertyCategory(wxT("Slider"), _("Slider"));
	ibPropertyInteger* m_propertyMinValue = ibPropertyObject::CreateProperty<ibPropertyInteger>(m_categorySlider, wxT("MinValue"), _("Min value"),
		_("The slider's smallest value, at its start. Default 0."), 0);
	ibPropertyInteger* m_propertyMaxValue = ibPropertyObject::CreateProperty<ibPropertyInteger>(m_categorySlider, wxT("MaxValue"), _("Max value"),
		_("The slider's largest value, at its end. Default 100."), 100);
	ibPropertyInteger* m_propertyValue = ibPropertyObject::CreateProperty<ibPropertyInteger>(m_categorySlider, wxT("Value"), _("Value"),
		_("Where the slider's thumb starts, between the min and max values. Default 50."), 50);
	ibPropertyEnum<ibValueEnumOrient>* m_propertyOrient = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumOrient>>(m_categorySlider, wxT("Orient"), _("Orient"),
		_("The slider's direction: horizontal (the default) or vertical."), wxHORIZONTAL);

	wxSlider* m_slider = nullptr;
};

// A progress bar, filled to its value of its range. Its orientation is what it is built with, as a slider's is.
class ibValueGauge : public ibValueWindow {
public:
	ibValueGauge(ibVisualHostClient& host, long long controlId) : ibValueWindow(host, controlId) {}

	virtual void Create(wxWindow* parent, const ibProtocolNode& node) override;
	virtual void Update(const ibProtocolNode& node) override;
	virtual wxWindow* GetWindow() const override { return m_gauge; }

	//load & save object in control
	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

private:

	ibPropertyCategory* m_categoryGauge = ibPropertyObject::CreatePropertyCategory(wxT("Gauge"), _("Gauge"));
	ibPropertyInteger* m_propertyRange = ibPropertyObject::CreateProperty<ibPropertyInteger>(m_categoryGauge, wxT("Range"), _("Range"),
		_("The value at which the progress bar is full. Default 100, so the value reads as a percentage."), 100);
	ibPropertyInteger* m_propertyValue = ibPropertyObject::CreateProperty<ibPropertyInteger>(m_categoryGauge, wxT("Value"), _("Value"),
		_("How far the progress bar is filled, from 0 to the range. Default 30."), 30);
	ibPropertyEnum<ibValueEnumOrient>* m_propertyOrient = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumOrient>>(m_categoryGauge, wxT("Orient"), _("Orient"),
		_("The progress bar's direction: horizontal (the default, filling left to right) or vertical."), wxHORIZONTAL);

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
	ibViewPlaceholder(ibVisualHostClient& host, long long controlId, const wxString& type)
		: ibValueFrame(host, controlId), m_type(type) {}

	virtual void Create(wxWindow* parent, const ibProtocolNode& node) override;
	virtual void Update(const ibProtocolNode& node) override;
	virtual wxWindow* GetWindow() const override { return m_panel; }

private:
	const wxString m_type;
	wxPanel*       m_panel = nullptr;
};

// A line. Its orientation is what it is built with — a line turned is built anew (ibVisualHostClient's shape).
class ibValueStaticLine : public ibValueWindow {
public:
	ibValueStaticLine(ibVisualHostClient& host, long long controlId) : ibValueWindow(host, controlId) {}

	virtual void Create(wxWindow* parent, const ibProtocolNode& node) override;
	virtual void Update(const ibProtocolNode& node) override;
	virtual wxWindow* GetWindow() const override { return m_staticLine; }

	//load & save object in control
	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

private:

	ibPropertyCategory* m_categoryStaticLine = ibPropertyObject::CreatePropertyCategory(wxT("StaticLine"), _("Static line"));
	ibPropertyEnum<ibValueEnumOrient>* m_propertyOrient = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumOrient>>(m_categoryStaticLine, wxT("Orient"), _("Orient"),
		_("The line's direction: horizontal (the default), separating stacked parts of a form, or vertical, separating side-by-side ones. Set Stretch to Expand for the line to run the full width or height."),
		wxHORIZONTAL);

	wxStaticLine* m_staticLine = nullptr;
};

#endif
