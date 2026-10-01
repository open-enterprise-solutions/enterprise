#ifndef __IB_DESIGNER_TREE_CTRL_H__
#define __IB_DESIGNER_TREE_CTRL_H__

#include <wx/treectrl.h>
#include <wx/event.h>

// ibDesignerTreeCtrl — our projection of wxTreeCtrl. The vendored widget in 3rdparty/wxWidgets stays
// untouched; when its behaviour doesn't fit we fork the ONE call here, in our subclass.
//
// Forked call: DeleteAllItems. While comctl32 clears the items it fires an artificial TVN_SELCHANGED
// (the caret item goes away), and wx's handler for it calls ::SetFocus() RE-ENTRANTLY to force focus
// events ahead of selection ones. During a programmatic rebuild (DeleteAllItems + repopulate) that
// grab STEALS the window focus — and, through the tree's own OnSetFocus, the active editor tab — from
// wherever the user was (the form-editor canvas, a property field). Being a direct ::SetFocus() it
// bypasses SetEvtHandlerEnabled(), so blocking events app-side alone doesn't stop it.
//
// We save the focus, block our own SET_FOCUS for the duration of the base delete (so the re-entrant
// grab drives no handler), run the base, then hand focus back if the base took it.
class ibDesignerTreeCtrl : public wxTreeCtrl
{
public:
	using wxTreeCtrl::wxTreeCtrl;   // same constructors as the base

	void DeleteAllItems() override
	{
		wxWindow* const keepFocus = wxWindow::FindFocus();
		{
			wxEventBlocker noFocusEvents(this, wxEVT_SET_FOCUS);
			wxTreeCtrl::DeleteAllItems();
		}
		if (keepFocus != nullptr && keepFocus != this && wxWindow::FindFocus() == this)
			keepFocus->SetFocus();
	}
};

#endif // !__IB_DESIGNER_TREE_CTRL_H__
