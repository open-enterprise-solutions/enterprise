// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to the demo form (docs/private/deskgui.md,
// the porting workflow).

//////////////////////////////////////////////////////////////////////////////
// Name:        wx/univ/statbox.h
// Purpose:     ibStaticBox declaration
// Author:      Vadim Zeitlin
// Created:     15.08.00
// Copyright:   (c) 2000 SciTech Software, Inc. (www.scitechsoft.com)
// Licence:     ibWindows licence
///////////////////////////////////////////////////////////////////////////////

#ifndef _DESKGUI_UNIV_STATICBOX_H_
#define _DESKGUI_UNIV_STATICBOX_H_

#include "deskgui/deskgui.h"

#include <wx/sizer.h>

#include "deskgui/univ/ctrl/control.h"

// SEAM vs univ: wxStaticBoxBase rides the native wxControl chain — derive
// from ibControl directly (NB: not the native wxStaticBox, so wxStaticBoxSizer
// can't host this box; ibStaticBoxSizer below does, and what it asks of the box
// is wxStaticBoxBase's, brought here)
class DESKGUI_API ibStaticBox : public ibControl
{
public:
    ibStaticBox() = default;

    ibStaticBox(wxWindow *parent,
                const wxString& label,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize)
    {
        Create(parent, wxID_ANY, label, pos, size);
    }

    ibStaticBox(wxWindow *parent, wxWindowID id,
                const wxString& label,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = 0,
                const wxString& name = wxASCII_STR(wxStaticBoxNameStr))
    {
        Create(parent, id, label, pos, size, style, name);
    }

    bool Create(wxWindow *parent,
                wxWindowID id,
                const wxString& label,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = 0,
                const wxString& name = wxASCII_STR(wxStaticBoxNameStr));

    // the origin of the static box is inside the border and under the label:
    // take account of this
    virtual wxPoint GetBoxAreaOrigin() const;

    // returning true from here ensures that we act as a container window for
    // our children
    virtual bool IsStaticBox() const override { return true; }

    // the box itself never takes focus (was wxStaticBoxBase behaviour)
    virtual bool AcceptsFocus() const override { return false; }

    // wxStaticBoxBase's: the borders a sizer leaves inside the box - above its
    // contents and on their other sides
    virtual void GetBordersForSizer(int *borderTop, int *borderOther) const;

    // wxStaticBoxBase's: destroy the box but not its children, which are
    // reparented to its parent - this is used by ibStaticBoxSizer, which owns
    // the box but not the windows it lays out
    void WXDestroyWithoutChildren();

protected:
    // SEAM vs univ: the label's size, as wxStaticBox (msw/statbox.cpp) answers
    // - wxWindowBase's best size is the extent of the box's children, which
    // grows with every layout that widens them
    virtual wxSize DoGetBestSize() const override;

    // get the size of the border
    wxRect GetBorderGeometry() const;

    // draw the control
    void OnPaint(wxPaintEvent& event);

private:
    wxDECLARE_DYNAMIC_CLASS(ibStaticBox);
    wxDECLARE_EVENT_TABLE();
};

// ----------------------------------------------------------------------------
// ibStaticBoxSizer: wxStaticBoxSizer (wx/sizer.h, src/common/sizer.cpp) of the
// library's static box
// ----------------------------------------------------------------------------

class DESKGUI_API ibStaticBoxSizer: public wxBoxSizer
{
public:
    ibStaticBoxSizer(ibStaticBox *box, int orient);
    ibStaticBoxSizer(int orient, wxWindow *win, const wxString& label = wxEmptyString);
    virtual ~ibStaticBoxSizer();

    virtual wxSize CalcMin() override;
    virtual void RepositionChildren(const wxSize& minSize) override;

    ibStaticBox *GetStaticBox() const
        { return m_staticBox; }

    // override to hide/show the static box as well
    virtual void ShowItems (bool show) override;
    virtual bool AreAnyItemsShown() const override;

    virtual bool Detach( wxWindowBase *window ) override;
    virtual bool Detach( wxSizer *sizer ) override { return wxBoxSizer::Detach(sizer); }
    virtual bool Detach( int index ) override { return wxBoxSizer::Detach(index); }

protected:
    ibStaticBox   *m_staticBox;

    virtual wxSizerItem* DoInsert(size_t index, wxSizerItem *item) override;

private:
    // Return true if we have any by recursively examining all children of this
    // sizer.
    bool CheckForNonBoxChildren(wxSizer* sizer) const;

    // Return true if this particular window is not a child of the static box.
    bool CheckIfNonBoxChild(wxWindow* win) const;

    // Set to true if there are any items _not_ using the associated static box
    // as parent in this sizer (either directly in it or in its child sizers).
    bool m_hasNonBoxChildren = false;

    wxDECLARE_NO_COPY_CLASS(ibStaticBoxSizer);
};

#endif // _DESKGUI_UNIV_STATICBOX_H_
