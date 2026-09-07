#include "webWindow.h"
#include "webSizer.h"

#include <algorithm>

#include "frontend/visualView/ctrl/frame.h"   // wxDefaultStype{BG,FG}Colour

bool ibWebIsPlatformPaper(const wxColour& colour)
{
	return colour == wxDefaultStypeBGColour || colour == *wxWHITE;
}

bool ibWebIsPlatformInk(const wxColour& colour)
{
	return colour == wxDefaultStypeFGColour;
}

// Definitions for the textctrl side-button events declared in webWindow.h
// (web build). Kept here so wfrontend.dll has its own symbols, parallel
// to the desktop definitions in win/ctrls/controlTextEditor.cpp. The
// wx event-type integers are process-local and self-contained per DLL.
wxDEFINE_EVENT(wxEVT_CONTROL_BUTTON_SELECT, wxCommandEvent);
wxDEFINE_EVENT(wxEVT_CONTROL_BUTTON_OPEN,   wxCommandEvent);
wxDEFINE_EVENT(wxEVT_CONTROL_BUTTON_CLEAR,  wxCommandEvent);
wxDEFINE_EVENT(wxEVT_CONTROL_TEXT_ENTER,    wxCommandEvent);
wxDEFINE_EVENT(wxEVT_CONTROL_TEXT_INPUT,    wxCommandEvent);
wxDEFINE_EVENT(wxEVT_CONTROL_TEXT_CLEAR,    wxCommandEvent);

// Web-local id generator: the same shape wx events themselves use —
// an event carries an id, either passed explicitly or auto-assigned
// if the caller doesn't care. A web node is a wxEvtHandler that fires
// wxCommandEvents carrying its id; giving every node an id (not just
// form-bound ones) keeps that path honest and lets detached nodes
// still be uniquely addressable in logs / diagnostics.
//
// Range starts at 1'000'000 so it can't collide with the runtime
// id space managed by ibValueFrame::GenerateNewID (those grow from
// 1 within a form). When Create passes a runtime id via
// EnsureControlID(), that wins; only id == 0 falls through to here.
#include <atomic>
namespace {
int NextWebId() {
	static std::atomic<int> s_counter{ 1'000'000 };
	return s_counter.fetch_add(1, std::memory_order_relaxed);
}
} // namespace

ibWebWindow::ibWebWindow(int id)
{
	SetControlId(id > 0 ? id : NextWebId());
}

ibWebWindow::~ibWebWindow()
{
	if (m_parent != nullptr)
		m_parent->RemoveChild(this);

	// Detach from a sizer we were Add'd to — owner drops us; it may
	// still outlive us if the sizer itself persists across rebuilds.
	if (m_container != nullptr) {
		m_container->Remove(this);
		m_container = nullptr;
	}

	// Make a local copy because AddChild/RemoveChild mutate m_children.
	const auto children = m_children;
	for (ibWebWindow* child : children)
		child->m_parent = nullptr;

	// Owned sizer — delete last so its children (which may have back-
	// pointers into us through Container) outlive this window only
	// briefly while Remove() runs on them.
	delete m_sizer;
	m_sizer = nullptr;
}

void ibWebWindow::SetParent(ibWebWindow* parent)
{
	if (m_parent == parent)
		return;
	if (m_parent != nullptr)
		m_parent->RemoveChild(this);
	m_parent = parent;
	if (m_parent != nullptr)
		m_parent->AddChild(this);
}

void ibWebWindow::SetSizer(ibWebSizer* sizer)
{
	if (m_sizer == sizer)
		return;
	// Null-out m_sizer BEFORE deleting the old one. ~ibWebSizer notifies
	// its owner via parentWindow->GetSizer() == this -> SetSizer(nullptr),
	// which re-enters here and would delete the same pointer twice if
	// m_sizer still held it. Swap first, delete second.
	ibWebSizer* const old = m_sizer;
	m_sizer = sizer;
	delete old;
	if (m_sizer != nullptr)
		m_sizer->SetOwner(this);
}

void ibWebWindow::AddChild(ibWebWindow* child)
{
	if (child != nullptr &&
		std::find(m_children.begin(), m_children.end(), child) == m_children.end())
	{
		m_children.push_back(child);
	}
}

void ibWebWindow::RemoveChild(ibWebWindow* child)
{
	m_children.erase(
		std::remove(m_children.begin(), m_children.end(), child),
		m_children.end());
}

nlohmann::json ibWebWindow::ToJSON() const
{
	nlohmann::json node = {
		{ "type",    GetControlType() },
		{ "label",   m_label },
		{ "enabled", m_enabled },
		{ "shown",   m_shown },
	};
	if (m_controlId != 0)
		node["id"] = m_controlId;

	// Style properties — emitted only when set, so un-styled nodes
	// stay clean in the JSON. Colours serialise as CSS hex (#RRGGBB),
	// font as a small object the browser can stitch into inline
	// style. Tooltip is just the plain string, surfaces as the
	// element's `title` attribute client-side.
	if (m_fg.IsOk() && !ibWebIsPlatformInk(m_fg)) {
		node["fg"] = m_fg.GetAsString(wxC2S_HTML_SYNTAX);
	}
	if (m_bg.IsOk() && !ibWebIsPlatformPaper(m_bg)) {
		node["bg"] = m_bg.GetAsString(wxC2S_HTML_SYNTAX);
	}
	if (m_font.IsOk()) {
		nlohmann::json f = {
			{ "size",   m_font.GetPointSize() },
			{ "family", m_font.GetFaceName() },
			{ "bold",   m_font.GetWeight() >= wxFONTWEIGHT_BOLD },
			{ "italic", m_font.GetStyle() == wxFONTSTYLE_ITALIC },
			{ "underline", m_font.GetUnderlined() },
		};
		node["font"] = std::move(f);
	}
	if (!m_tooltip.IsEmpty()) {
		node["tooltip"] = m_tooltip;
	}
	// Size hints — emit only the axes that are constrained. wxDefaultSize
	// uses -1 for both, so a -1 stays out of the JSON and the browser
	// applies no min/max for that axis.
	if (m_minSize != wxDefaultSize) {
		nlohmann::json s = nlohmann::json::object();
		if (m_minSize.GetWidth()  > 0) s["w"] = m_minSize.GetWidth();
		if (m_minSize.GetHeight() > 0) s["h"] = m_minSize.GetHeight();
		if (!s.empty()) node["minSize"] = std::move(s);
	}
	if (m_maxSize != wxDefaultSize) {
		nlohmann::json s = nlohmann::json::object();
		if (m_maxSize.GetWidth()  > 0) s["w"] = m_maxSize.GetWidth();
		if (m_maxSize.GetHeight() > 0) s["h"] = m_maxSize.GetHeight();
		if (!s.empty()) node["maxSize"] = std::move(s);
	}

	if (!m_children.empty() || m_sizer != nullptr) {
		nlohmann::json arr = nlohmann::json::array();
		for (const ibWebWindow* child : m_children)
			arr.push_back(child->ToJSON());
		if (m_sizer != nullptr)
			arr.push_back(m_sizer->ToJSON());
		node["children"] = std::move(arr);
	}
	return node;
}
