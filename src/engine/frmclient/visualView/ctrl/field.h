#ifndef _FRMCLIENT_VIEW_FIELD_H__
#define _FRMCLIENT_VIEW_FIELD_H__

#include "frmclient/visualView/ctrl/window.h"

class ibControl;
class ibTextCtrl;
class ibCheckBox;
class ibStaticText;
class ibRadioButton;
class ibGauge;
class ibSlider;

class ibValueField;

// THE VIEW A FIELD IS DRAWN WITH — the server's ibFieldViewProvider, this client's half: the widget of the view, made in
// the field's place. What every view shares is the field's: the caption in the widget's title column, the look, the
// value read from the State. A view is this client's own widget for a meaning (ibProtocol::ibForm::ibProperty::
// ibFieldView), never one the server names.
class ibFieldViewProvider {
public:

	virtual ~ibFieldViewProvider() = default;

	// Its widget, made under `parent` — and drawn from the node: what the view shows of the State, and how.
	virtual void Create(wxWindow* parent, const ibProtocolNode& node) = 0;
	virtual void Update(const ibProtocolNode& node) = 0;

	// The widget, a control of the desktop's library: its title column holds the field's caption.
	virtual ibControl* GetControl() const = 0;
	// The field's caption, where the view shows it: in its widget's title column — a Label bound to nothing shows it as
	// its text.
	virtual void ShowCaption(const ibProtocolNode& state);

	// What is typed and not committed yet, kept over a frame drawn anew — an Input's alone.
	virtual bool SaveEdit(ibViewEdit& /*edit*/) const { return false; }
	virtual void RestoreEdit(const ibViewEdit& /*edit*/) {}

	// The child of a view, by its number; an Input for one this client does not know.
	static std::unique_ptr<ibFieldViewProvider> Make(long long view, ibValueField& field);

protected:

	explicit ibFieldViewProvider(ibValueField& field) : m_field(field) {}

	// What the person did, to the server — the field's event: its id is the field's.
	void Send(ibProtocol::ibEvent::ibKind event) const;
	void Send(ibProtocol::ibEvent::ibKind event, const ibProtocolNode& args) const;

private:

	ibValueField& m_field;
};

// ⭐ THE FIELD — one control, drawn with the widget its View is (docs/private/form-field.md): the caption, its side and
// the look are the field's, set on whatever widget the view made. A new View is a new widget: the host builds the field
// anew, as it builds a line turned (visualHostClient.cpp, BuiltWith).
class ibValueField : public ibValueWindow {
public:

	ibValueField(ibVisualHostClient& host, long long controlId) : ibValueWindow(host, controlId) {}

	virtual void Create(wxWindow* parent, const ibProtocolNode& node) override;
	virtual void Update(const ibProtocolNode& node) override;
	virtual wxWindow* GetWindow() const override;
	// a field's, whatever its view: the platform style's field background — none chosen, left to the theme
	virtual wxColour GetPlatformBackground() const override;

	virtual bool SaveEdit(ibViewEdit& edit) const override;
	virtual void RestoreEdit(const ibViewEdit& edit) override;

private:

	friend class ibFieldViewProvider;   // sends as the field

	std::unique_ptr<ibFieldViewProvider> m_provider;
};

// ⭐ INPUT — typed into: this client's text field (ibTextCtrl), its typing held until committed.
class ibFieldViewInput : public ibFieldViewProvider {
public:

	explicit ibFieldViewInput(ibValueField& field) : ibFieldViewProvider(field) {}

	virtual void Create(wxWindow* parent, const ibProtocolNode& node) override;
	virtual void Update(const ibProtocolNode& node) override;
	virtual ibControl* GetControl() const override;

	virtual bool SaveEdit(ibViewEdit& edit) const override;
	virtual void RestoreEdit(const ibViewEdit& edit) override;

private:

	void Commit();
	// The text the field shows while nobody types into it: the server's — and under a mask, where the person stands in
	// the field, the text the mask is typed over, laid into it; elsewhere an empty text is the mask's skeleton.
	void ShowText(bool focused);

	void OnTextInput(wxCommandEvent& event);
	void OnTextEnter(wxCommandEvent& event);
	void OnSetFocus(wxFocusEvent& event);
	void OnKillFocus(wxFocusEvent& event);

	ibTextCtrl* m_textCtrl = nullptr;
	bool        m_editing = false;   // typed into, not committed yet
	wxString    m_text;              // the server's Text — what the field shows at rest
	wxString    m_editText;          // …and its EditText: the text the mask is typed over, where Text is not
};

// ⭐ FLAG — a box, toggled: Toggle {Checked}.
class ibFieldViewFlag : public ibFieldViewProvider {
public:

	explicit ibFieldViewFlag(ibValueField& field) : ibFieldViewProvider(field) {}

	virtual void Create(wxWindow* parent, const ibProtocolNode& node) override;
	virtual void Update(const ibProtocolNode& node) override;
	virtual ibControl* GetControl() const override;

private:

	ibCheckBox* m_checkbox = nullptr;
};

// ⭐ LABEL — shown: unbound, the caption is the text itself; bound, the value beside it, a link that sends Open.
class ibFieldViewLabel : public ibFieldViewProvider {
public:

	explicit ibFieldViewLabel(ibValueField& field) : ibFieldViewProvider(field) {}

	virtual void Create(wxWindow* parent, const ibProtocolNode& node) override;
	virtual void Update(const ibProtocolNode& node) override;
	virtual ibControl* GetControl() const override;
	virtual void ShowCaption(const ibProtocolNode& state) override;

private:

	ibStaticText* m_staticText = nullptr;
};

// ⭐ SWITCH — its items as radio buttons inside its widget, in a row or a column (Orient); one picked sends Change
// {Member: its place}. Read-only, a button takes no press.
class ibFieldViewSwitch : public ibFieldViewProvider {
public:

	explicit ibFieldViewSwitch(ibValueField& field) : ibFieldViewProvider(field) {}

	virtual void Create(wxWindow* parent, const ibProtocolNode& node) override;
	virtual void Update(const ibProtocolNode& node) override;
	virtual ibControl* GetControl() const override;

private:

	void OnRadioButton(wxCommandEvent& event);

	ibControl*                  m_box = nullptr;     // the widget: the title column beside, the buttons in it
	wxBoxSizer*                 m_sizer = nullptr;   // …laid out by it, its orientation the field's Orient
	std::vector<ibRadioButton*> m_buttons;           // an item of the State's Choices each, in their order
};

// ⭐ GAUGE — a bar filled to the State's Position of the field's Range, running its Orient; it sends nothing.
class ibFieldViewGauge : public ibFieldViewProvider {
public:

	explicit ibFieldViewGauge(ibValueField& field) : ibFieldViewProvider(field) {}

	virtual void Create(wxWindow* parent, const ibProtocolNode& node) override;
	virtual void Update(const ibProtocolNode& node) override;
	virtual ibControl* GetControl() const override;

private:

	ibGauge* m_gauge = nullptr;
};

// ⭐ SLIDER — a thumb at the State's Position between the field's MinValue and MaxValue, running its Orient; let go,
// it sends Change {Position}. Read-only, it moves nothing.
class ibFieldViewSlider : public ibFieldViewProvider {
public:

	explicit ibFieldViewSlider(ibValueField& field) : ibFieldViewProvider(field) {}

	virtual void Create(wxWindow* parent, const ibProtocolNode& node) override;
	virtual void Update(const ibProtocolNode& node) override;
	virtual ibControl* GetControl() const override;

private:

	void OnScroll(wxScrollEvent& event);

	ibSlider* m_slider = nullptr;
};

#endif
