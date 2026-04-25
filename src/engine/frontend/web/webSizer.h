#ifndef __WEB_SIZER_H__
#define __WEB_SIZER_H__

// Mirrors wxSizer on desktop: a pure layout object, not a window, not
// evented — so base is wxObject, not ibWebWindow. A sizer has exactly
// one owner (an ibWebWindow via SetSizer, OR a parent ibWebSizer via
// Add). Each child added to a sizer carries its own AddParams
// (proportion / flag / border / stretch) that mirror the analogous
// arguments of wxSizer::Add on desktop and come from ibValueSizerItem
// in the metadata tree. Actual layout is CSS-side — the sizer only
// reports type + orient/rows/cols + per-child AddParams.

#include <vector>

#include <wx/object.h>
#include <wx/string.h>
#include <wx/gdicmn.h>   // wxSize (parameter type in no-op SetMinSize)
#include <wx/colour.h>   // wxColour
#include <wx/font.h>     // wxFont

#include "jsonAdapter.h"

class ibWebWindow;

// Per-child layout hints. Mirrors wxSizer::Add(proportion, flag,
// border). "flag" is a bitmask of wxEXPAND / wxLEFT / wxRIGHT /
// wxUP / wxDOWN / wxStretch (wxSHRINK, wxGROW, wxALIGN_*).
// Populated from ibValueSizerItem for each added child.
//
// Defined outside ibWebSizer so that default-argument `= {}` in
// Add() declarations works under Clang's aggregate-init rules.
struct ibWebSizerAddParams {
	int proportion = 0;
	int flag       = 0;
	int border     = 0;
};

class ibWebSizer : public wxObject {
public:
	using AddParams = ibWebSizerAddParams;

	struct Item {
		wxObject* child = nullptr;  // ibWebWindow* or ibWebSizer*
		AddParams params;
	};

	ibWebSizer();
	virtual ~ibWebSizer() override;

	// "boxsizer", "wrapsizer", "staticboxsizer", "gridsizer"
	virtual wxString GetSizerType() const = 0;

	// Single owner: the ibWebWindow that called SetSizer, or the
	// parent ibWebSizer that Add'd us. Stored as wxObject* since both
	// are valid kinds.
	wxObject* GetOwner() const { return m_owner; }
	void      SetOwner(wxObject* owner);

	// Add a child with its layout params. Sizer takes ownership; the
	// dtor deletes the children it holds.
	void Add(ibWebWindow* child, const AddParams& params = {});
	void Add(ibWebSizer*  child, const AddParams& params = {});
	void Remove(wxObject* child);

	// Update the layout params of an existing child in-place. Returns
	// true if the child was found. Used by ibValueSizerItem::OnUpdated
	// to propagate script-side property changes (proportion / border /
	// flag / stretch) without a full ClearVisualHost + Create walker
	// pass. If the child isn't in the sizer, no-op.
	bool UpdateItemParams(wxObject* child, const AddParams& params);

	const std::vector<Item>& GetItems() const { return m_items; }

	virtual nlohmann::json ToJSON() const;

	// No-op API mirrors for wxSizer cross-platform parity — lets shared
	// Update() bodies in ibValueSizer / ibValueBoxSizer / … call these
	// uniformly without #ifdef. CSS handles real layout on web, so
	// server-side min-size / layout commands don't translate to anything
	// useful — they're silently dropped here.
	void SetMinSize(const wxSize& /*sz*/) {}
	void Layout() {}

private:
	void AttachChild(wxObject* child, const AddParams& params);

	wxObject*         m_owner = nullptr;
	std::vector<Item> m_items;
};

class ibWebBoxSizer : public ibWebSizer {
public:
	ibWebBoxSizer() = default;
	explicit ibWebBoxSizer(int orient) : m_orient(orient) {}

	virtual wxString GetSizerType() const override { return wxT("boxsizer"); }
	void SetOrientation(int o) { m_orient = o; }
	int  GetOrientation() const { return m_orient; }
	virtual nlohmann::json ToJSON() const override {
		auto n = ibWebSizer::ToJSON();
		n["orient"] = m_orient;  // wxHORIZONTAL / wxVERTICAL
		return n;
	}
private:
	int m_orient = 0;
};

class ibWebWrapSizer : public ibWebSizer {
public:
	ibWebWrapSizer() = default;
	explicit ibWebWrapSizer(int orient) : m_orient(orient) {}

	virtual wxString GetSizerType() const override { return wxT("wrapsizer"); }
	// Mirror wxWrapSizer API so Update bodies in ibValueWrapSizer can
	// share one path across desktop and web.
	void SetOrientation(int o) { m_orient = o; }
	int  GetOrientation() const { return m_orient; }
	virtual nlohmann::json ToJSON() const override {
		auto n = ibWebSizer::ToJSON();
		n["orient"] = m_orient;
		return n;
	}
private:
	int m_orient = 0;
};

// ibWebStaticBoxSizer composites sizer + static-box window: on desktop
// these are two objects (wxStaticBoxSizer + child wxStaticBox), here
// one class holds both the layout params (orient) and the window-level
// props (title / font / colours / enable / shown / tooltip). ToJSON
// emits everything; the browser renders caption + CSS styling directly.
class ibWebStaticBoxSizer : public ibWebSizer {
public:
	ibWebStaticBoxSizer() = default;
	ibWebStaticBoxSizer(int orient, const wxString& title)
		: m_orient(orient), m_title(title) {}

	virtual wxString GetSizerType() const override { return wxT("staticboxsizer"); }

	// Sizer-level property.
	void SetOrientation(int o) { m_orient = o; }
	int  GetOrientation() const { return m_orient; }

	// Window-level properties mirror wxStaticBox's settable surface
	// *by name* so ibValueStaticBoxSizer::Update can use one body that
	// targets either a wxStaticBox (desktop) or `this` composite (web)
	// through a typed local. Names match wxWindow / wxStaticBox.
	void SetLabel(const wxString& t)            { m_title = t; }
	const wxString& GetLabel() const            { return m_title; }

	void SetFont(const wxFont& f)               { m_font = f; }
	const wxFont& GetFont() const               { return m_font; }

	void SetForegroundColour(const wxColour& c) { m_fg = c; }
	const wxColour& GetForegroundColour() const { return m_fg; }

	void SetBackgroundColour(const wxColour& c) { m_bg = c; }
	const wxColour& GetBackgroundColour() const { return m_bg; }

	void Enable(bool e = true)                  { m_enabled = e; }
	bool IsEnabled() const                      { return m_enabled; }

	void Show(bool s = true)                    { m_shown = s; }
	bool IsShown() const                        { return m_shown; }

	void SetToolTip(const wxString& t)          { m_tooltip = t; }
	const wxString& GetToolTip() const          { return m_tooltip; }

	virtual nlohmann::json ToJSON() const override {
		auto n = ibWebSizer::ToJSON();
		n["orient"]  = m_orient;
		n["title"]   = m_title;
		n["enabled"] = m_enabled;
		n["shown"]   = m_shown;
		if (!m_tooltip.IsEmpty())
			n["tooltip"] = m_tooltip;
		if (m_fg.IsOk())
			n["fg"] = m_fg.GetAsString(wxC2S_HTML_SYNTAX);
		if (m_bg.IsOk())
			n["bg"] = m_bg.GetAsString(wxC2S_HTML_SYNTAX);
		if (m_font.IsOk()) {
			nlohmann::json fj;
			fj["family"] = m_font.GetFaceName();
			fj["size"]   = m_font.GetPointSize();
			fj["bold"]   = m_font.GetWeight() == wxFONTWEIGHT_BOLD;
			fj["italic"] = m_font.GetStyle()  == wxFONTSTYLE_ITALIC;
			fj["underline"] = m_font.GetUnderlined();
			n["font"] = fj;
		}
		return n;
	}

private:
	int      m_orient = 0;
	wxString m_title;

	wxFont   m_font;
	wxColour m_fg;
	wxColour m_bg;
	bool     m_enabled = true;
	bool     m_shown   = true;
	wxString m_tooltip;
};

class ibWebGridSizer : public ibWebSizer {
public:
	ibWebGridSizer() = default;
	ibWebGridSizer(unsigned rows, unsigned cols) : m_rows(rows), m_cols(cols) {}

	virtual wxString GetSizerType() const override { return wxT("gridsizer"); }
	// Mirror wxGridSizer API. wxGridSizer::SetRows / SetCols are plain
	// setters; the browser re-renders with the new grid dimensions.
	void SetRows(int r) { m_rows = r; }
	int  GetRows() const { return m_rows; }
	void SetCols(int c) { m_cols = c; }
	int  GetCols() const { return m_cols; }
	virtual nlohmann::json ToJSON() const override {
		auto n = ibWebSizer::ToJSON();
		n["rows"] = m_rows;
		n["cols"] = m_cols;
		return n;
	}
private:
	unsigned m_rows = 0;
	unsigned m_cols = 0;
};

#endif
