// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to frontend.vcxproj
// (set ObjectFileName on a name clash with visualView), add to the demo form.

/////////////////////////////////////////////////////////////////////////////
// Name:        wx/univ/control.h
// Purpose:     universal ibControl: adds handling of mnemonics
// Author:      Vadim Zeitlin
// Created:     14.08.00
// Copyright:   (c) 2000 SciTech Software, Inc. (www.scitechsoft.com)
// Licence:     ibWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_UNIV_CONTROL_H_
#define _WX_UNIV_CONTROL_H_

#include "frontend/frontend.h"

class ibControlRenderer;
class ibInputHandler;
class ibRenderer;

// we must include it as most/all control classes derive their handlers from
// it
#include "frontend/uikit/inputHandler.h"

#include "frontend/uikit/inputConsumer.h"

// ----------------------------------------------------------------------------
// ibControlAction: the action is currently just a string which identifies it,
// later it might become an atom (i.e. an opaque handler to string).
// ----------------------------------------------------------------------------

typedef wxString ibControlAction;

// the list of actions which apply to all controls (other actions are defined
// in the controls headers)

#define ibACTION_NONE    wxT("")           // no action to perform
#define wxNO_ACCEL_CHAR    wxT('\0')

// ----------------------------------------------------------------------------
// ibControl: the base class for all GUI controls.
//
// SEAM vs univ: the univ original derived from wxControlBase whose parent
// was the univ wxWindow; in our build wxControlBase sits on the *native*
// wxWindow, which would bypass the ibWindow paint/input pipeline. So
// ibControl derives from ibWindow directly and carries tiny shims for the
// few wxControlBase services the univ code uses (command events).
// ----------------------------------------------------------------------------

#include "frontend/uikit/window.h"

class FRONTEND_API ibControl : public ibWindow, public ibInputConsumer
{
public:
    ibControl() { Init(); }

    ibControl(wxWindow *parent,
              wxWindowID id,
              const wxPoint& pos = wxDefaultPosition,
              const wxSize& size = wxDefaultSize, long style = 0,
              const wxValidator& validator = wxDefaultValidator,
              const wxString& name = wxASCII_STR(wxControlNameStr))
    {
        Init();

        Create(parent, id, pos, size, style, validator, name);
    }

    virtual ~ibControl();

    bool Create(wxWindow *parent,
                wxWindowID id,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize, long style = 0,
                const wxValidator& validator = wxDefaultValidator,
                const wxString& name = wxASCII_STR(wxControlNameStr));

    // this function will filter out '&' characters and will put the
    // accelerator char (the one immediately after '&') into m_chAccel
    virtual void SetLabel(const wxString& label) override;

    // return the current label with mnemonics
    virtual wxString GetLabel() const override { return ibWindow::GetLabel(); }

    // return the current label without mnemonics
    virtual wxString GetLabelText() const { return m_label; }

    // wxControlBase shims (see the SEAM note above)
    void Command(wxCommandEvent& event) { ProcessWindowEvent(event); }
    int GetAlignment() const { return GetWindowStyle() & wxALIGN_MASK; }

    // wxUniversal-specific methods

    // return the index of the accel char in the label or -1 if none
    int GetAccelIndex() const { return m_indexAccel; }

    // return the accel char itself or 0 if none
    wxChar GetAccelChar() const
    {
        return m_indexAccel == -1 ? wxNO_ACCEL_CHAR : (wxChar)m_label[m_indexAccel];
    }

    virtual ibWindow *GetInputWindow() const override
    {
        return const_cast<ibControl*>(this);
    }

protected:
    // common part of all ctors
    void Init();

    // wxControlBase shim: stamp the standard command-event fields
    void InitCommandEvent(wxCommandEvent& event) const {
        event.SetEventObject(const_cast<ibControl*>(this));
        event.SetId(GetId());
    }

    // set m_label and m_indexAccel and refresh the control to show the new
    // label (but, unlike SetLabel(), don't call the base class SetLabel() thus
    // avoiding to change wxControlBase::m_labelOrig)
    void UnivDoSetLabel(const wxString& label);

private:
    // label and accel info
    wxString   m_label;
    int        m_indexAccel;

    wxDECLARE_DYNAMIC_CLASS(ibControl);
    wxDECLARE_EVENT_TABLE();
    WX_DECLARE_INPUT_CONSUMER()
};

#endif // _WX_UNIV_CONTROL_H_
