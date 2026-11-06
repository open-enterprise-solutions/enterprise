// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to the demo form (docs/private/gui.md,
// the porting workflow).

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

#include "gui/gui.h"

class ibControlRenderer;
class ibInputHandler;
class ibRenderer;

// we must include it as most/all control classes derive their handlers from
// it
#include "gui/univ/inputHandler.h"

#include "gui/univ/inputConsumer.h"

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

#include "gui/univ/window.h"

class GUI_API ibControl : public ibWindow, public ibInputConsumer
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

    // the title's column moves with the font it is measured in
    virtual bool SetFont(const wxFont& font) override;

    // THE TITLE - what the control is called, in a column of its own beside it and outside its frame: "Counterparty"
    // before a field, a check box's caption. Empty = none. The label stays what the control draws itself (a button's
    // text, a static text's); a check box with a title draws its box alone.
    void SetTitle(const wxString& title);
    const wxString& GetTitle() const { return m_title; }

    // which side: wxLEFT - the column a form lines up - or wxRIGHT
    void SetTitlePosition(wxDirection side);
    wxDirection GetTitlePosition() const { return m_titlePosition; }

    // THE COLUMN: as wide as the title, or as the form's label pass gives it - the widest title of a run, so the fields
    // after them start at one x (frmclient's visualHostClient, CalculateLabels / ApplyLabels)
    wxSize GetTitleExtent() const;
    void SetTitleWidth(int width);      // wxDefaultCoord = the title's own
    int GetTitleWidth() const;          // the column as drawn, its gap included; 0 without a title

protected:
    // the title is drawn beside the frame, outside the client area: the frame's rect leaves its column out
    virtual wxRect GetBorderRect() const override;

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

    // the title drawn in its column, by the theme, after the border (the non-client pass, as the border)
    void OnNcPaint(wxNcPaintEvent& event);

private:
    // the client area moved within the window: laid out and drawn anew, the best size asked again
    void OnTitleChanged();

    // label and accel info
    wxString   m_label;
    int        m_indexAccel;

    // the title and its column (SetTitle)
    wxString    m_title;
    wxDirection m_titlePosition = wxLEFT;
    int         m_titleWidth = wxDefaultCoord;

    wxDECLARE_DYNAMIC_CLASS(ibControl);
    wxDECLARE_EVENT_TABLE();
    WX_DECLARE_INPUT_CONSUMER()
};

#endif // _WX_UNIV_CONTROL_H_
