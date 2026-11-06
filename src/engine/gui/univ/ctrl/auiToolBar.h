#ifndef _TOOLBAR_H_
#define _TOOLBAR_H_

#include <wx/aui/auibar.h>

// wxWidgets 3.3 changed wxAuiToolBar::DoGetBestSize() to return the full
// content extent (sum of all items). When this toolbar lives inside a
// pane with a scrollable area or inside an AUI pane, the AUI manager asks
// the toolbar for its best size to decide the minimum pane width — if we
// say "I need 800 px", the pane refuses to shrink below that and the user
// drag-resizes the virtual scroll area instead of triggering the gripper.
//
// Previous versions honored SetMinSize() out of the box. 3.3 doesn't.
// This subclass re-introduces that behaviour: a stored min-hint takes
// precedence over the computed content extent, and the gripper width is
// used as the absolute floor so AUI always has room to start a drag.
class ibAuiToolBar : public wxAuiToolBar
{
public:

	ibAuiToolBar() : wxAuiToolBar() {}

	ibAuiToolBar(wxWindow* parent,
		wxWindowID id = wxID_ANY,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize,
		long style = wxAUI_TB_DEFAULT_STYLE) : wxAuiToolBar(parent, id, pos, size, style)
	{
	}

	// Explicit min-size hint for the "I want the toolbar to collapse to this"
	// case. Stored separately from the wx internal SetMinSize so we can fall
	// back to the gripper-width floor if not set.
	void SetToolBarMinSize(const wxSize& minSize) {
		m_customMinSize = minSize;
		InvalidateBestSize();
		wxAuiToolBar::SetMinSize(minSize);
	}

	// In the form designer the toolbar must report its real content size
	// (no horizontal scrollbar there — the form size is driven by the mouse
	// and the user needs to see the actual toolbar extent). Runtime keeps the
	// collapse-on-overflow behaviour.
	void SetDesignerMode(bool designer) {
		m_designerMode = designer;
		InvalidateBestSize();
	}

protected:

	wxSize DoGetBestSize() const override {
		const wxSize content = wxAuiToolBar::DoGetBestSize();
		wxSize out = content;

		// Designer mode: report the real extent so the visual editor can show
		// the "true" toolbar width to the developer laying out the form.
		if (m_designerMode) {
			if (m_customMinSize.GetHeight() > 0) out.SetHeight(m_customMinSize.GetHeight());
			return out;
		}

		// Runtime: when wxAUI_TB_OVERFLOW is enabled, the toolbar itself can
		// collapse items into a dropdown (▼ on the right). That means the
		// parent should be free to give it *any* width — yet wxWidgets 3.3
		// started reporting the full content width as "best", which makes
		// sizers/scrolled windows allocate the whole strip and push a
		// horizontal scrollbar onto the form. Clamp to the overflow minimum.
		if (GetWindowStyleFlag() & wxAUI_TB_OVERFLOW) {
			const int kCollapsedMinWidth = FromDIP(32);
			out.SetWidth(kCollapsedMinWidth);
		}

		// Explicit owner override wins over anything computed above.
		if (m_customMinSize.GetWidth()  > 0) out.SetWidth (m_customMinSize.GetWidth());
		if (m_customMinSize.GetHeight() > 0) out.SetHeight(m_customMinSize.GetHeight());
		return out;
	}

	wxSize DoGetBestClientSize() const override {
		return DoGetBestSize();
	}

private:
	wxSize m_customMinSize = wxDefaultSize;
	bool   m_designerMode  = false;

public:

	wxAuiToolBarItem* InsertTool(int idx, int tool_id,
		const wxString& label,
		const wxBitmap& bitmap,
		const wxBitmap& disabledBitmap,
		wxItemKind kind,
		const wxString& shortHelpString,
		const wxString& longHelpString,
		wxObject* WXUNUSED(client_data))
	{
		wxAuiToolBarItem item;

		item.SetWindow(nullptr);
		item.SetLabel(label);
		item.SetBitmap(bitmap);
		item.SetDisabledBitmap(disabledBitmap);
		item.SetShortHelp(shortHelpString);
		item.SetLongHelp(longHelpString);
		item.SetActive(true);
		item.SetHasDropDown(false);
		item.SetId(tool_id);
		item.SetState(0);
		item.SetProportion(0);
		item.SetKind(kind);
		item.SetSizerItem(nullptr);
		item.SetMinSize(wxDefaultSize);
		item.SetUserData(0);
		item.SetSticky(false);

		if (tool_id == wxID_ANY) item.SetId(wxNewId());

		if (!disabledBitmap.IsOk())
		{
			// no disabled bitmap specified, we need to make one
			if (bitmap.IsOk())
			{
				item.SetDisabledBitmap(bitmap.ConvertToDisabled());
			}
		}

		m_items.Insert(item, idx);
		return &m_items[idx];
	}

	wxAuiToolBarItem* InsertSeparator(int idx, int tool_id)
	{
		wxAuiToolBarItem item;

		item.SetWindow(nullptr);
		item.SetLabel(wxEmptyString);
		item.SetBitmap(wxNullBitmap);
		item.SetDisabledBitmap(wxNullBitmap);
		item.SetActive(true);
		item.SetHasDropDown(false);
		item.SetId(tool_id);
		item.SetState(0);
		item.SetProportion(0);
		item.SetKind(wxITEM_SEPARATOR);
		item.SetSizerItem(nullptr);
		item.SetMinSize(wxDefaultSize);
		item.SetUserData(0);
		item.SetSticky(false);

		if (tool_id == wxID_ANY) item.SetId(wxNewId());

		m_items.Insert(item, idx);
		return &m_items[idx];
	}
};

#endif 
