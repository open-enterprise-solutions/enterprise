#ifndef __WEB_WINDOW_H__
#define __WEB_WINDOW_H__

// Base class for every web "control". Each ibValue<Control>::Create in
// visualView/ctrl/*.cpp returns an ibWebWindow-derived instance when
// built with OES_USE_WEB, instead of the wxWidgets control it returns
// in the desktop build.
//
// Why inherit wxEvtHandler:
//   * the rest of the frontend is already wx-event-driven; keeping the
//     same base lets scripts call Bind/Unbind/ProcessEvent identically
//     to the desktop path
//   * control meta-objects already hold raw wxObject* pointers and
//     dynamic_cast to wxEvtHandler*, so ibWebWindow slots in without
//     changing the meta-object API
//
// The class does not render anything by itself. It keeps the logical
// state (id, visible, enabled, parent/child tree, arbitrary property
// bag) and knows how to serialise that state as JSON so the browser
// client can draw the matching HTML.
//
// Sizers are NOT ibWebWindow (see webSizer.h). A window has an optional
// ibWebSizer* m_sizer slot analogous to wxWindow::SetSizer on desktop;
// a window may also be contained BY a sizer (m_container), set when a
// parent sizer Add's us.

#include <vector>

#include <wx/colour.h>
#include <wx/event.h>
#include <wx/font.h>
#include <wx/gdicmn.h>
#include <wx/string.h>

#include "jsonAdapter.h"

#ifdef OES_USE_WEB
// Textctrl side-button events — mirror the three custom wx events
// ibControlTextEditor declares (wxEVT_CONTROL_BUTTON_SELECT / OPEN /
// CLEAR). The desktop declarations live in win/ctrls/controlTextEditor.h
// which pulls in wx controls; web-side we declare again so ibWebTextCtrl
// can FireCommand without including that heavier header. Definitions
// for wfrontend.dll live in webWindow.cpp, so the same Bind/Unbind
// code path in ibValueTextCtrl compiles on both builds.
wxDECLARE_EVENT(wxEVT_CONTROL_BUTTON_SELECT, wxCommandEvent);
wxDECLARE_EVENT(wxEVT_CONTROL_BUTTON_OPEN,   wxCommandEvent);
wxDECLARE_EVENT(wxEVT_CONTROL_BUTTON_CLEAR,  wxCommandEvent);
// Text edit lifecycle events — mirror desktop ibControlTextEditor.
// ibValueTextCtrl binds all three in the unified Update body;
// web fires only ENTER/INPUT/CLEAR that make sense remotely (per-
// keystroke INPUT still goes through /change commit — server side
// doesn't see it directly, but the binds are emitted so the shared
// code path links identically on both builds).
wxDECLARE_EVENT(wxEVT_CONTROL_TEXT_ENTER,    wxCommandEvent);
wxDECLARE_EVENT(wxEVT_CONTROL_TEXT_INPUT,    wxCommandEvent);
wxDECLARE_EVENT(wxEVT_CONTROL_TEXT_CLEAR,    wxCommandEvent);
#endif

class ibWebSizer;

class ibWebWindow : public wxEvtHandler {
public:
	// Single ctor: id at construction. Two sources in priority order:
	//   1. Runtime id passed in — typically from EnsureControlID() on
	//      the paired ibValueFrame, which delegates to
	//      ibValueFrame::GenerateNewID (the form-scoped runtime id
	//      generator). Used for every form-bound control.
	//   2. Web-local auto-id — when `id == 0`, the ctor assigns a
	//      unique value from a process-wide atomic counter. Mirrors
	//      the wxEvent pattern (events carry an id, auto-assigned if
	//      the caller didn't specify one). Keeps detached /
	//      synthetic nodes uniquely addressable, though they aren't
	//      routed through form->FindControlByID.
	// Parent isn't a ctor arg — the walker owns parent setup
	// (SetParent or sizer->Add) since it needs the typed
	// window-vs-sizer decision.
	explicit ibWebWindow(int id = 0);

	virtual ~ibWebWindow() override;

	// Type tag written into the JSON node. Concrete subclasses return
	// their widget kind ("statictext", "button", "textctrl", ...).
	virtual wxString GetControlType() const = 0;

	// Label / caption — most controls have one, so it lives here. A
	// subclass that does not (sizer, spacer) can simply ignore it.
	virtual void            SetLabel(const wxString& label) { m_label = label; }
	virtual const wxString& GetLabel() const { return m_label; }

	virtual bool IsEnabled() const { return m_enabled; }
	virtual void Enable(bool enable = true) { m_enabled = enable; }

	virtual bool IsShown() const { return m_shown; }
	// Return bool to match wxWindow::Show contract — `if (win->Show(x))`
	// is a valid idiom used in shared code (ibView::ShowFrame).
	virtual bool Show(bool show = true) { m_shown = show; return true; }

	// Doc/view shims — fork of wxWidgets' doc/view subsystem calls these
	// on the "associated window" (ibDocChildFrameAnyBase::m_win) which on
	// web is an ibWebWindow*. No-op stubs so ibDocChildFrameAny<T,P>
	// template instantiates cleanly with <ibWebChildFrame, ibWebWindow>.
	// Behavioral wiring (e.g. defer-mark for tab close on Destroy) is
	// step 3 of the fork plan (see ibDocView.h header notes).
	virtual void Raise() {}
	virtual bool Destroy() { delete this; return true; }
	virtual bool Close(bool /*force*/ = false) { return true; }

	// Visual style properties pushed from ibValueWindow::UpdateWindow.
	// Emitted as optional JSON fields (only when set to a non-default
	// value) so the browser can apply inline CSS without every node
	// carrying the overhead. Desktop has the same set on wxWindow —
	// SetFont / SetForegroundColour / SetBackgroundColour / SetToolTip —
	// these are the web equivalents, persisted on the render shim.
	const wxColour&      GetForegroundColour() const { return m_fg; }
	void                 SetForegroundColour(const wxColour& c) { m_fg = c; }
	const wxColour&      GetBackgroundColour() const { return m_bg; }
	void                 SetBackgroundColour(const wxColour& c) { m_bg = c; }
	const wxFont&        GetFont() const { return m_font; }
	void                 SetFont(const wxFont& f) { m_font = f; }
	const wxString&      GetToolTip() const { return m_tooltip; }
	void                 SetToolTip(const wxString& t) { m_tooltip = t; }

	// Size hints — desktop's wxWindow::SetMinSize / SetMaxSize. Default
	// wxDefaultSize (-1,-1) means "no constraint"; ToJSON skips the
	// field so nothing hits the client for unconstrained controls.
	const wxSize& GetMinSize() const { return m_minSize; }
	void          SetMinSize(const wxSize& s) { m_minSize = s; }
	const wxSize& GetMaxSize() const { return m_maxSize; }
	void          SetMaxSize(const wxSize& s) { m_maxSize = s; }

	ibWebWindow* GetParent() const { return m_parent; }
	void         SetParent(ibWebWindow* parent);

	const std::vector<ibWebWindow*>& GetChildren() const { return m_children; }

	// Control-model id — copy of ibValueFrame::GetControlID for the
	// node's backing ibValueFrame. Lets the browser round-trip actions
	// ("button with id=N clicked") back to the right ibValueFrame on
	// the server. Zero when the node is synthetic (host itself, etc.).
	int  GetControlId() const { return m_controlId; }
	void SetControlId(int id) { m_controlId = id; }

	// Optional owned sizer — mirrors wxWindow::SetSizer. When set, the
	// sizer's JSON is appended to this window's children[] array so
	// existing browser renderers pick it up with no contract change.
	// Window owns the sizer; dtor deletes.
	ibWebSizer* GetSizer() const { return m_sizer; }
	void        SetSizer(ibWebSizer* sizer);

	// Back-link set by ibWebSizer::Add when this window is placed into
	// a sizer. Used only for dtor unlink — layout params live on the
	// sizer's Item, not here.
	ibWebSizer* GetContainer() const { return m_container; }
	void        SetContainer(ibWebSizer* container) { m_container = container; }
	void        ClearContainer() { m_container = nullptr; }

	// Default serialisation: type + label + enabled/shown flags + the
	// children array (with the sizer appended as the last entry when
	// present). Subclasses extend by overriding and merging in their
	// extra fields.
	virtual nlohmann::json ToJSON() const;

	// Polymorphic HTTP-origin event handler. The session dispatcher
	// resolves the ibValueFrame by id, fetches the matching ibWebWindow
	// from the host's (frame→wxObject) map, and calls this. Default
	// returns false; each descendant implements the kinds it cares
	// about (Button: "click", TextCtrl: "text", CheckBox: "toggle", …)
	// and builds the matching wxEvent via FireCommand / FireEvent.
	// Adding a new control = one override here + one JS renderer; no
	// dispatcher change, no new HTTP route.
	virtual bool HandleRequest(const wxString& /*kind*/,
		const wxString& /*value*/) { return false; }

protected:
	// Core event-generation primitive for descendants. Takes any
	// wxEvent subclass (wxCommandEvent for button/text/check,
	// wxMouseEvent for pointer, wxKeyEvent for keyboard, ...) —
	// stamps the node's id + event-object on it, posts through our
	// own QueueEvent (thread-safe; wx copies via Clone()) and drains
	// on the caller's thread. Every ibWebXxx::FireXxx() routes
	// through here: keeps the Queue + Drain pair in one place so the
	// later move to a session-worker thread touches only this method.
	// Returns true when posted.
	bool FireEvent(wxEvent& ev) {
		ev.SetId(m_controlId);
		ev.SetEventObject(this);
		QueueEvent(ev.Clone());
		ProcessPendingEvents();
		return true;
	}

	// Sugar for the common wxCommandEvent case — button clicks, text
	// changes, checkbox toggles. Descendants with pointer/keyboard
	// events build a wxMouseEvent / wxKeyEvent directly and call
	// FireEvent above.
	bool FireCommand(wxEventType type) {
		wxCommandEvent ev(type, m_controlId);
		return FireEvent(ev);
	}

private:
	void AddChild(ibWebWindow* child);
	void RemoveChild(ibWebWindow* child);

	wxString                  m_label;
	bool                      m_enabled = true;
	bool                      m_shown   = true;

	// Optional style state — matches desktop wxWindow's per-widget
	// foreground/background/font/tooltip. Default-constructed values
	// are wxNullColour / wxNullFont / empty string, which ToJSON
	// omits so unset windows don't carry redundant JSON fields.
	wxColour                  m_fg;
	wxColour                  m_bg;
	wxFont                    m_font;
	wxString                  m_tooltip;
	wxSize                    m_minSize = wxDefaultSize;
	wxSize                    m_maxSize = wxDefaultSize;

	ibWebWindow*              m_parent  = nullptr;
	std::vector<ibWebWindow*> m_children;
	int                       m_controlId = 0;

	ibWebSizer*               m_sizer     = nullptr;  // owned
	ibWebSizer*               m_container = nullptr;  // borrowed (sizer owns us)
};

// -----------------------------------------------------------------------------
// Concrete web controls. Each mirrors one wxWidgets control used by the
// visualView runtime; the rendering is done browser-side from the JSON
// produced by ibWebWindow::ToJSON(), so these classes just hold the
// state that the JSON serialiser needs.
// -----------------------------------------------------------------------------

// Placeholder node for controls whose real rendering isn't implemented
// yet on the web side. Carries a type string (e.g. "toolbar") so the
// browser can either render a stub block with the right CSS class or
// skip it, and keeps the layout/metadata tree intact. Replace with a
// dedicated ibWeb<Control> when the real renderer is added.
class ibWebStubControl : public ibWebWindow {
public:
	explicit ibWebStubControl(const wxString& type) : m_type(type) {}
	virtual wxString GetControlType() const override { return m_type; }
private:
	wxString m_type;
};

class ibWebStaticText : public ibWebWindow {
public:
	explicit ibWebStaticText(int id = 0) : ibWebWindow(id) {}
	ibWebStaticText(const wxString& caption, int id = 0)
		: ibWebWindow(id) { SetLabel(caption); }

	virtual wxString GetControlType() const override { return wxT("statictext"); }
};

class ibWebButton : public ibWebWindow {
public:
	explicit ibWebButton(int id = 0) : ibWebWindow(id) {}
	ibWebButton(const wxString& caption, int id = 0)
		: ibWebWindow(id) { SetLabel(caption); }

	virtual wxString GetControlType() const override { return wxT("button"); }

	// Fire the native-click event. The Bind set up in
	// ibValueButton::Create routes it to OnButtonPressed, which runs
	// the script and refreshes the form — same shape as a desktop
	// wxButton click. See docs/web/event-dispatcher.md.
	bool FireClick() { return FireCommand(wxEVT_BUTTON); }

	virtual bool HandleRequest(const wxString& kind,
		const wxString& /*value*/) override
	{
		if (kind == wxT("click")) return FireClick();
		return false;
	}

	// Representation / picture — mirrors ibWebToolBarItem. ibRepresentation
	// enum: 0 Auto / 1 Text / 2 Picture / 3 PictureAndText. Server resolves
	// Auto before pushing. Picture arrives as a data:image/png;base64 URI
	// produced by ibBackendPicture::CreateBase64Image, so the browser
	// renders <img src="..."> directly.
	void SetRepresentation(int r)               { m_representation = r; }
	void SetHasPicture(bool v)                  { m_hasPicture     = v; }
	void SetPictureDataUri(const wxString& uri) { m_pictureDataUri = uri; }

	virtual nlohmann::json ToJSON() const override {
		auto node = ibWebWindow::ToJSON();
		node["representation"] = m_representation;
		node["hasPicture"]     = m_hasPicture;
		if (!m_pictureDataUri.IsEmpty())
			node["picture"] = m_pictureDataUri;
		return node;
	}

private:
	int      m_representation = 3;   // default PictureAndText
	bool     m_hasPicture     = false;
	wxString m_pictureDataUri;
};

class ibWebCheckBox : public ibWebWindow {
public:
	explicit ibWebCheckBox(int id = 0) : ibWebWindow(id) {}

	virtual wxString GetControlType() const override { return wxT("checkbox"); }

	void SetValue(bool v) { m_value = v; }
	bool GetValue() const { return m_value; }

	// Client toggle. wxEVT_CHECKBOX carries the new state as int on
	// desktop (event.GetInt() == IsChecked()); we mirror that so the
	// shared OnClickedCheckbox handler reads the same field on both
	// sides.
	bool FireToggle(bool checked) {
		m_value = checked;
		wxCommandEvent ev(wxEVT_CHECKBOX);
		ev.SetInt(checked ? 1 : 0);
		return FireEvent(ev);
	}

	virtual bool HandleRequest(const wxString& kind,
		const wxString& value) override
	{
		if (kind == wxT("toggle"))
			return FireToggle(value == wxT("1") || value == wxT("true"));
		return false;
	}

	virtual nlohmann::json ToJSON() const override {
		auto node = ibWebWindow::ToJSON();
		node["value"] = m_value;
		return node;
	}

private:
	bool m_value = false;
};

class ibWebToolbar : public ibWebWindow {
public:
	explicit ibWebToolbar(int id = 0) : ibWebWindow(id) {}

	virtual wxString GetControlType() const override { return wxT("toolbar"); }

	// Toolbars don't receive direct client events (tool-button clicks
	// route through their child ibWebToolBarItem). Base HandleRequest
	// default (false) is the right behaviour here.
};

class ibWebToolBarItem : public ibWebWindow {
public:
	explicit ibWebToolBarItem(int id = 0) : ibWebWindow(id) {}
	ibWebToolBarItem(const wxString& caption, int id = 0)
		: ibWebWindow(id) { SetLabel(caption); }

	virtual wxString GetControlType() const override { return wxT("tool"); }

	// Representation mode (matches ibRepresentation enum: 0 Auto /
	// 1 Text / 2 Picture / 3 PictureAndText). Server resolves Auto
	// to one of the concrete modes before pushing, so the browser
	// always sees a concrete choice and can show/hide the icon and
	// label accordingly. hasPicture is true when there's an
	// associated picture (from m_propertyPicture or from the
	// action's bitmap) — browser renders a placeholder glyph when
	// the actual bitmap encoding isn't wired yet.
	void SetRepresentation(int r) { m_representation = r; }
	void SetHasPicture(bool v)    { m_hasPicture     = v; }
	// base64-encoded PNG data URI — pushed by ibValueToolBarItem::Update
	// from GetItemPicture's wxBitmap so the browser can render an
	// <img src="data:..."> directly without a round-trip to the server.
	void SetPictureDataUri(const wxString& uri) { m_pictureDataUri = uri; }

	// On desktop, wxEVT_TOOL is emitted by the wx toolbar (not by the
	// tool item itself) — the toolbar's bound OnTool handler does the
	// action dispatch. Mirror that here: queue the event on the parent
	// toolbar so ibValueToolbar::OnTool runs via its Bind. Falls back
	// to self when there's no parent (shouldn't happen in practice).
	bool FireClick() {
		if (ibWebWindow* parent = GetParent()) {
			wxCommandEvent ev(wxEVT_TOOL, GetControlId());
			ev.SetEventObject(parent);
			parent->QueueEvent(ev.Clone());
			parent->ProcessPendingEvents();
			return true;
		}
		return FireCommand(wxEVT_TOOL);
	}

	virtual bool HandleRequest(const wxString& kind,
		const wxString& /*value*/) override
	{
		if (kind == wxT("click")) return FireClick();
		return false;
	}

	virtual nlohmann::json ToJSON() const override {
		auto node = ibWebWindow::ToJSON();
		node["representation"] = m_representation;
		node["hasPicture"]     = m_hasPicture;
		if (!m_pictureDataUri.IsEmpty())
			node["picture"] = m_pictureDataUri;
		return node;
	}

private:
	int      m_representation = 3;   // default PictureAndText
	bool     m_hasPicture     = false;
	wxString m_pictureDataUri;
};

class ibWebToolBarSeparator : public ibWebWindow {
public:
	explicit ibWebToolBarSeparator(int id = 0) : ibWebWindow(id) {}

	virtual wxString GetControlType() const override { return wxT("toolseparator"); }
};

class ibWebTextCtrl : public ibWebWindow {
public:
	explicit ibWebTextCtrl(int id = 0) : ibWebWindow(id) {}

	virtual wxString GetControlType() const override { return wxT("textctrl"); }

	void           SetValue(const wxString& v) { m_value = v; }
	const wxString& GetValue() const { return m_value; }

	void SetPasswordMode(bool v)  { m_passwordMode = v; }
	void SetMultilineMode(bool v) { m_multilineMode = v; }
	// Mirror desktop ibControlTextEditor::SetTextEditMode — enables the
	// inline-editable surface. On web the browser's <input> is always
	// editable; flag is still stored so JSON carries it and scripts that
	// toggle it server-side get persisted state for diagnostics.
	void SetTextEditMode(bool v)  { m_textEditMode = v; }

	// Side-button visibility — mirrors desktop's ibControlTextEditor
	// ShowSelectButton / ShowOpenButton / ShowClearButton. Each renders
	// as an additional button glued to the right edge of the input;
	// clicking fires the corresponding wxEVT_CONTROL_BUTTON_* event so
	// the existing ibValueTextCtrl Bind handlers (OnSelectButtonPressed
	// etc.) run unmodified.
	void ShowSelectButton(bool v) { m_showSelectButton = v; }
	void ShowOpenButton(bool v)   { m_showOpenButton   = v; }
	void ShowClearButton(bool v)  { m_showClearButton  = v; }

	// Client-side text commit (browser "change" event on blur/Enter).
	// Updates local state so the subsequent JSON render matches what
	// the user typed, then posts wxEVT_TEXT with the new value as
	// wxCommandEvent payload. The Bind set up in
	// ibValueTextCtrl::Create routes the event into OnWebTextChanged,
	// where the value is committed through SetControlValue and the
	// OnChange script fires.
	bool FireTextChange(const wxString& v) {
		m_value = v;
		wxCommandEvent ev(wxEVT_TEXT);
		ev.SetString(v);
		return FireEvent(ev);
	}

	// Side-button fire methods — emit the matching desktop wx event
	// so ibValueTextCtrl's Bind handlers run without a separate web
	// dispatch path. All three mirror the desktop CONTROL_BUTTON_*
	// events one-to-one.
	bool FireSelectButton() { return FireCommand(wxEVT_CONTROL_BUTTON_SELECT); }
	bool FireOpenButton()   { return FireCommand(wxEVT_CONTROL_BUTTON_OPEN); }
	bool FireClearButton()  { return FireCommand(wxEVT_CONTROL_BUTTON_CLEAR); }

	virtual bool HandleRequest(const wxString& kind,
		const wxString& value) override
	{
		if (kind == wxT("text"))         return FireTextChange(value);
		if (kind == wxT("buttonSelect")) return FireSelectButton();
		if (kind == wxT("buttonOpen"))   return FireOpenButton();
		if (kind == wxT("buttonClear"))  return FireClearButton();
		return false;
	}

	virtual nlohmann::json ToJSON() const override {
		auto node = ibWebWindow::ToJSON();
		node["value"]            = m_value;
		node["passwordMode"]     = m_passwordMode;
		node["multilineMode"]    = m_multilineMode;
		node["showSelectButton"] = m_showSelectButton;
		node["showOpenButton"]   = m_showOpenButton;
		node["showClearButton"]  = m_showClearButton;
		return node;
	}

private:
	wxString m_value;
	bool     m_passwordMode      = false;
	bool     m_multilineMode     = false;
	bool     m_textEditMode      = true;
	bool     m_showSelectButton  = false;
	bool     m_showOpenButton    = false;
	bool     m_showClearButton   = false;
};

#endif
