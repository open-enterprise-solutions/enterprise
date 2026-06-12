// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to frontend.vcxproj
// (set ObjectFileName on a name clash with visualView), add to the demo form.

///////////////////////////////////////////////////////////////////////////////
// Name:        wx/univ/toolbar.h
// Purpose:     ibToolBar declaration
// Author:      Robert Roebling
// Created:     10.09.00
// Copyright:   (c) Robert Roebling
// Licence:     ibWindows licence
///////////////////////////////////////////////////////////////////////////////

#ifndef _WX_UNIV_TOOLBAR_H_
#define _WX_UNIV_TOOLBAR_H_

#include "frontend/frontend.h"

#include <wx/button.h>      // for ibStdButtonInputHandler

class ibToolBarTool;

// ----------------------------------------------------------------------------
// the actions supported by this control
// ----------------------------------------------------------------------------

#define ibACTION_TOOLBAR_TOGGLE  ibACTION_BUTTON_TOGGLE
#define ibACTION_TOOLBAR_PRESS   ibACTION_BUTTON_PRESS
#define ibACTION_TOOLBAR_RELEASE ibACTION_BUTTON_RELEASE
#define ibACTION_TOOLBAR_CLICK   ibACTION_BUTTON_CLICK
#define ibACTION_TOOLBAR_ENTER   wxT("enter")     // highlight the tool
#define ibACTION_TOOLBAR_LEAVE   wxT("leave")     // unhighlight the tool

// ----------------------------------------------------------------------------
// ibToolBar
// ----------------------------------------------------------------------------

#include <wx/toolbar.h>       // wxToolBarBase tool classes, wxTB_* styles
#include "frontend/uikit/ctrl/control.h"

// SEAM vs univ: wxToolBarBase rides the native wxControl chain; the tool
// bookkeeping is reimplemented via shims below
class FRONTEND_API ibToolBar : public ibControl
{
public:
    // ---- wxToolBarBase shims (see the SEAM note above) -------------------
    size_t GetToolsCount() const { return m_tools.GetCount(); }

    wxToolBarToolBase *InsertTool(size_t pos, int id, const wxString& label,
            const wxBitmapBundle& bitmap,
            const wxBitmapBundle& bmpDisabled = wxBitmapBundle(),
            wxItemKind kind = wxITEM_NORMAL,
            const wxString& shortHelp = wxEmptyString);
    wxToolBarToolBase *AddTool(int id, const wxString& label,
            const wxBitmapBundle& bitmap,
            const wxString& shortHelp = wxEmptyString,
            wxItemKind kind = wxITEM_NORMAL);
    wxToolBarToolBase *AddCheckTool(int id, const wxString& label,
            const wxBitmapBundle& bitmap,
            const wxBitmapBundle& bmpDisabled = wxBitmapBundle(),
            const wxString& shortHelp = wxEmptyString);
    wxToolBarToolBase *AddSeparator();
    void SetToolBitmapSize(const wxSize& size) {
        m_defaultWidth = size.x;
        m_defaultHeight = size.y;
    }
    wxSize GetToolBitmapSize() const {
        return wxSize(m_defaultWidth, m_defaultHeight);
    }
    void SetMargins(int x, int y);
    wxToolBarToolBase *FindById(int id) const {
        for ( wxToolBarToolsList::compatibility_iterator node = m_tools.GetFirst();
              node;
              node = node->GetNext() ) {
            if ( node->GetData()->GetId() == id )
                return node->GetData();
        }
        return nullptr;
    }
    void UnToggleRadioGroup(wxToolBarToolBase *tool) {
        if ( !tool || tool->GetKind() != wxITEM_RADIO )
            return;
        for ( wxToolBarToolsList::compatibility_iterator node = m_tools.GetFirst();
              node;
              node = node->GetNext() ) {
            wxToolBarToolBase *other = node->GetData();
            if ( other != tool && other->GetKind() == wxITEM_RADIO && other->IsToggled() )
                other->Toggle(false);
        }
    }
    virtual bool OnLeftClick(int id, bool toggleDown) {
        wxCommandEvent event(wxEVT_TOOL, id);
        event.SetEventObject(this);
        event.SetInt(toggleDown ? 1 : 0);
        return !ProcessWindowEvent(event) || event.GetSkipped() || true;
    }
    bool IsVertical() const {
        return (GetWindowStyle() & (wxTB_LEFT | wxTB_RIGHT)) != 0;
    }
    // ----------------------------------------------------------------------
    // construction/destruction
    ibToolBar() { Init(); }
    ibToolBar(wxWindow *parent,
              wxWindowID id,
              const wxPoint& pos = wxDefaultPosition,
              const wxSize& size = wxDefaultSize,
              long style = 0,
              const wxString& name = wxASCII_STR(wxToolBarNameStr))
    {
        Init();

        Create(parent, id, pos, size, style, name);
    }

    bool Create( wxWindow *parent,
                 wxWindowID id,
                 const wxPoint& pos = wxDefaultPosition,
                 const wxSize& size = wxDefaultSize,
                 long style = 0,
                 const wxString& name = wxASCII_STR(wxToolBarNameStr) );

    virtual ~ibToolBar();

    virtual bool Realize() ;

    virtual void SetWindowStyleFlag( long style ) override;

    virtual wxToolBarToolBase *FindToolForPosition(wxCoord x, wxCoord y) const ;

    virtual void SetToolShortHelp(int id, const wxString& helpString) ;

    void SetMargins(const wxSize& size)
        { SetMargins((int) size.x, (int) size.y); }

    virtual bool PerformAction(const ibControlAction& action,
                               long numArg = -1,
                               const wxString& strArg = wxEmptyString) override;
    static ibInputHandler *GetStdInputHandler(ibInputHandler *handlerDef);
    virtual ibInputHandler *DoGetStdInputHandler(ibInputHandler *handlerDef) override
    {
        return GetStdInputHandler(handlerDef);
    }

protected:
    // common part of all ctors
    void Init();

    // implement base class pure virtuals
    virtual bool DoInsertTool(size_t pos, wxToolBarToolBase *tool) ;
    virtual bool DoDeleteTool(size_t pos, wxToolBarToolBase *tool) ;

    virtual void DoEnableTool(wxToolBarToolBase *tool, bool enable) ;
    virtual void DoToggleTool(wxToolBarToolBase *tool, bool toggle) ;
    virtual void DoSetToggle(wxToolBarToolBase *tool, bool toggle) ;

    virtual wxToolBarToolBase *CreateTool(int id,
                                          const wxString& label,
                                          const wxBitmapBundle& bmpNormal,
                                          const wxBitmapBundle& bmpDisabled,
                                          wxItemKind kind,
                                          wxObject *clientData,
                                          const wxString& shortHelp,
                                          const wxString& longHelp) ;
    virtual wxToolBarToolBase *CreateTool(ibControl *control,
                                          const wxString& label) ;

    virtual wxSize DoGetBestClientSize() const override;
    virtual void DoDraw(ibControlRenderer *renderer) override;

    // get the bounding rect for the given tool
    wxRect GetToolRect(wxToolBarToolBase *tool) const;

    // redraw the given tool
    void RefreshTool(wxToolBarToolBase *tool);

    // (re)calculate the tool positions, should only be called if it is
    // necessary to do it, i.e. m_needsLayout == true
    void DoLayout();

    // get the rect limits depending on the orientation: top/bottom for a
    // vertical toolbar, left/right for a horizontal one
    void GetRectLimits(const wxRect& rect, wxCoord *start, wxCoord *end) const;

private:
    // have we calculated the positions of our tools?
    bool m_needsLayout;

    // the width of a separator
    wxCoord m_widthSeparator;

    // the total size of all toolbar elements
    wxCoord m_maxWidth,
            m_maxHeight;

private:
    // wxToolBarBase state (lived in the lost base)
    wxToolBarToolsList m_tools;
    wxCoord m_defaultWidth = 16;
    wxCoord m_defaultHeight = 15;
    wxCoord m_xMargin = 0;
    wxCoord m_yMargin = 0;

    wxDECLARE_DYNAMIC_CLASS(ibToolBar);
};

#endif // _WX_UNIV_TOOLBAR_H_
