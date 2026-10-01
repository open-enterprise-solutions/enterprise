#include "canvasWindow.h"

// Chrome window — a pure composite wrapper. See canvasWindow.h.

ibCanvasWindow::ibCanvasWindow(wxWindow* parent)
{
	// wxCompositeWindow<wxPanel> default-constructs the wxPanel base; finish the
	// two-phase creation here and own a vertical stack sizer (chrome on top, the
	// inner control filling the rest).
	wxPanel::Create(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxTAB_TRAVERSAL | wxNO_BORDER);

	m_stack = new wxBoxSizer(wxVERTICAL);
	SetSizer(m_stack);
}

void ibCanvasWindow::AddLayer(wxWindow* part)
{
	if (part == nullptr)
		return;
	m_layers.push_back(part);
	m_stack->Add(part, 0, wxEXPAND);   // chrome row: natural height, full width
}

void ibCanvasWindow::SetInner(wxWindow* inner)
{
	m_inner = inner;
	if (inner != nullptr)
		m_stack->Add(inner, 1, wxEXPAND);   // the control fills the remaining space
}

wxWindowList ibCanvasWindow::GetCompositeWindowParts() const
{
	// Colour / font / cursor / focus forward to every live part.
	wxWindowList parts;
	for (wxWindow* part : m_layers)
		parts.Append(part);
	if (m_inner != nullptr)
		parts.Append(m_inner);
	return parts;
}

void ibCanvasWindow::SetDropTarget(wxDropTarget* dt)
{
	// Forward to the inner content window — the real control (a grid) is what a body drop must hit. The
	// chrome's toolbar layer sits over the form canvas, which catches drops there.
	if (m_inner != nullptr)
		m_inner->SetDropTarget(dt);
	else
		wxCompositeWindow<wxPanel>::SetDropTarget(dt);
}
