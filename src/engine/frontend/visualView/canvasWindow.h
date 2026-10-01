#ifndef _CANVAS_WINDOW_H__
#define _CANVAS_WINDOW_H__

#include <vector>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/compositewin.h>

//********************************************************************************************
//*                          Canvas window (pure widget wrapper)                             *
//********************************************************************************************

// A composite window that stacks optional LAYER parts — a command-bar toolbar today, a
// search row / status bar later — ABOVE the real inner control. It is a PURE WIDGET: not an
// ibValue, not runtime-registered, not in the CLSID system. Being a wxCompositeWindow it
// forwards colour / font / cursor / focus to its parts, so painting the "control"
// transparently paints the inner widget (and the layers). GetGenericControl() hands back the
// real inner widget. Consumer: ibValueWindowComposite::CreateWithLayers (tableBox).
class ibCanvasWindow : public wxCompositeWindow<wxPanel>
{
public:

	explicit ibCanvasWindow(wxWindow* parent);
	virtual ~ibCanvasWindow() {}

	// Add a chrome part (toolbar / search row) — stacked top-to-bottom, natural height.
	// The part MUST be created with this window as its parent (composite focus/key
	// forwarding only binds direct children).
	void AddLayer(wxWindow* part);

	// The real control this chrome wraps — added last, filling the remaining space.
	// Also created as a direct child of this window.
	void SetInner(wxWindow* inner);

	// The inner control the wrapped control casts to its concrete widget type.
	wxWindow* GetGenericControl() const { return m_inner; }

	// A drop target set on this chrome is FORWARDED to the inner content window — the real control a body
	// drop must hit (the toolbar layer sits over the form canvas, which catches drops there). A wxDropTarget
	// is single-owner, so it can't be shared across the sibling parts; the composite hands it to its content
	// child, the same way wxCompositeWindow forwards colour / font / focus to its parts.
	virtual void SetDropTarget(wxDropTarget* dt) override;

	// The chrome parts (stable refs) — iterated on UPDATE to refresh them in place. Teardown
	// is automatic: destroying this window destroys the parts (and the inner) with it.
	const std::vector<wxWindow*>& GetLayerParts() const { return m_layers; }

private:

	// Parts the composite forwards colour / font / focus to: chrome rows, then inner.
	virtual wxWindowList GetCompositeWindowParts() const override;

	wxBoxSizer*            m_stack  = nullptr;   // vertical: chrome on top, inner below
	std::vector<wxWindow*> m_layers;             // toolbar, search, … (top, in order)
	wxWindow*              m_inner  = nullptr;
};

#endif // _CANVAS_WINDOW_H__
