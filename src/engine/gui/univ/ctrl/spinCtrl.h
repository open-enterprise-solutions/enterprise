// FORKED from the wx GENERIC spin control (wxSpinCtrl -> ibSpinCtrl, wxSpinCtrlDouble -> ibSpinCtrlDouble,
// mechanical ib prefixes): the library's text control and spin button in one.

/////////////////////////////////////////////////////////////////////////////
// Name:        wx/spinctrl.h
// Purpose:     ibSpinCtrlBase class
// Author:      Vadim Zeitlin
// Created:     22.07.99
// Copyright:   (c) Vadim Zeitlin
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////
// Name:        wx/generic/spinctlg.h
// Purpose:     generic ibSpinCtrl class
// Author:      Vadim Zeitlin
// Created:     28.10.99
// Copyright:   (c) Vadim Zeitlin
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _GUI_UNIV_SPINCTRL_H_
#define _GUI_UNIV_SPINCTRL_H_

#include "gui/gui.h"

#if wxUSE_SPINCTRL

#include <wx/spinctrl.h>      // wxSpinEvent, wxSpinDoubleEvent, wxEVT_SPINCTRL(DOUBLE), the wxSP_* styles
#include <wx/compositewin.h>

#include "gui/univ/ctrl/control.h"

class ibSpinButton;
class ibTextCtrl;

class ibSpinCtrlTextGeneric; // ibTextCtrl used for the ibSpinCtrlGenericBase

// ----------------------------------------------------------------------------
// A spin ctrl is a text control with a spin button which is usually used to
// prompt the user for a numeric input.
// There are two kinds for number types T=integer or T=double.
// ----------------------------------------------------------------------------

// SEAM vs wx: wxSpinCtrlBase (wx/spinctrl.h) rides the native wxControl chain;
// this is its re-declaration on the ib chain, so the generic implementation
// keeps all its overrides valid. The events it sends - wxSpinEvent with
// wxEVT_SPINCTRL, wxSpinDoubleEvent with wxEVT_SPINCTRLDOUBLE - stay wx's.
class GUI_API ibSpinCtrlBase : public ibControl
{
public:
    ibSpinCtrlBase() = default;

    // accessor functions that derived classes are expected to have
    virtual wxString GetTextValue() const = 0;
    // T GetValue() const
    // T GetMin() const
    // T GetMax() const
    // T GetIncrement() const
    virtual bool GetSnapToTicks() const = 0;
    // unsigned GetDigits() const                   - ibSpinCtrlDouble only

    // operation functions that derived classes are expected to have
    virtual void SetValue(const wxString& value) = 0;
    // void SetValue(T val)
    // void SetRange(T minVal, T maxVal)
    // void SetIncrement(T inc)
    virtual void SetSnapToTicks(bool snap_to_ticks) = 0;
    // void SetDigits(unsigned digits)              - ibSpinCtrlDouble only

    // The base for numbers display, e.g. 10 or 16.
    virtual int GetBase() const = 0;
    virtual bool SetBase(int base) = 0;
    // Select text in the textctrl
    virtual void SetSelection(long from, long to) = 0;

protected:
#if wxUSE_ACCEL
    // Reserve the keys used for editing the text in this control, as the
    // controls implemented in terms of ibTextCtrl already do.
    virtual bool ClaimsKeyBeforeAccelerator(const wxKeyEvent& event,
                                            int command) const override;
#endif // wxUSE_ACCEL

private:
    wxDECLARE_NO_COPY_CLASS(ibSpinCtrlBase);
};

// ----------------------------------------------------------------------------
// wxSpinCtrlGeneric is a combination of ibTextCtrl and ibSpinButton
//
// This class manages a double valued generic spinctrl through the DoGet/SetXXX
// functions that are made public as Get/SetXXX functions for int or double
// for the ibSpinCtrl and ibSpinCtrlDouble classes respectively to avoid
// function ambiguity.
// ----------------------------------------------------------------------------

class GUI_API ibSpinCtrlGenericBase
                : public wxNavigationEnabled<wxCompositeWindow<ibSpinCtrlBase> >
{
public:
    ibSpinCtrlGenericBase() { Init(); }

    bool Create(wxWindow *parent,
                wxWindowID id = wxID_ANY,
                const wxString& value = wxEmptyString,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = wxSP_ARROW_KEYS,
                double min = 0, double max = 100, double initial = 0,
                double inc = 1,
                const wxString& name = wxT("wxSpinCtrl"));

    virtual ~ibSpinCtrlGenericBase();

    // accessors
    virtual wxString GetTextValue() const override;
    // T GetValue() const
    // T GetMin() const
    // T GetMax() const
    // T GetIncrement() const
    virtual bool GetSnapToTicks() const override { return m_snap_to_ticks; }
    // unsigned GetDigits() const                   - ibSpinCtrlDouble only

    // operations
    virtual void SetValue(const wxString& text) override;
    // void SetValue(T val)
    // void SetRange(T minVal, T maxVal)
    // void SetIncrement(T inc)
    virtual void SetSnapToTicks(bool snap_to_ticks) override;
    // void SetDigits(unsigned digits)              - ibSpinCtrlDouble only

    // Select text in the textctrl
    void SetSelection(long from, long to) override;

    // implementation from now on

    // forward these functions to all subcontrols
    virtual bool Enable(bool enable = true) override;
    virtual bool Show(bool show = true) override;

    virtual bool SetBackgroundColour(const wxColour& colour) override;

    // get the subcontrols
    ibTextCtrl   *GetText() const       { return m_textCtrl; }
    ibSpinButton *GetSpinButton() const { return m_spinButton; }

    // forwarded events from children windows
    void OnSpinButton(wxSpinEvent& event);
    void OnTextLostFocus(wxFocusEvent& event);
    void OnTextChar(wxKeyEvent& event);
    void OnMouseWheel(wxMouseEvent& event);

    // this window itself is used only as a container for its sub windows so it
    // shouldn't accept the focus at all and any attempts to explicitly set
    // focus to it should give focus to its text constol part
    virtual bool AcceptsFocus() const override { return false; }
    virtual void SetFocus() override;

    friend class ibSpinCtrlTextGeneric;

protected:
    // override the base class virtuals involved into geometry calculations
    virtual wxSize DoGetBestSize() const override;
    virtual wxSize DoGetSizeFromTextSize(int xlen, int ylen = -1) const override;
    virtual void DoMoveWindow(int x, int y, int width, int height) override;

    // SEAM vs wx: no DoEnable of its own - wxMSW's passed it to the base, which here keeps the window enabled
    // (ibWindow::DoEnable); the parts are enabled by Enable()

    enum SendEvent
    {
        SendEvent_None,
        SendEvent_Text
    };

    // generic double valued functions
    double DoGetValue() const { return m_value; }
    bool DoSetValue(double val, SendEvent sendEvent);
    void DoSetRange(double min_val, double max_val);
    void DoSetIncrement(double inc);

    // update our value to reflect the text control contents (if it has been
    // modified by user, do nothing otherwise)
    //
    // can also change the text control if its value is invalid
    //
    // return true if our value has changed
    bool SyncSpinToText(SendEvent sendEvent);

    // Send the correct event type
    virtual void DoSendEvent() = 0;

    // Convert the text to/from the corresponding value.
    virtual bool DoTextToValue(const wxString& text, double *val) = 0;
    virtual wxString DoValueToText(double val) = 0;

    // check if the value is in range
    bool InRange(double n) const { return (n >= m_min) && (n <= m_max); }

    // adjust the value to fit the range and snap it to ticks if necessary
    double AdjustAndSnap(double value) const;

    // ensure that the value is in range wrapping it round if necessary
    double AdjustToFitInRange(double value) const;

    // Assign validator with current parameters
    virtual void ResetTextValidator() = 0;

    double m_value;
    double m_min;
    double m_max;
    double m_increment;
    bool   m_snap_to_ticks;

    int m_spin_value;

    // the subcontrols
    ibTextCtrl   *m_textCtrl;
    ibSpinButton *m_spinButton;

private:
    // common part of all ctors
    void Init();

    // Implement pure virtual function inherited from wxCompositeWindow.
    virtual wxWindowList GetCompositeWindowParts() const override;

    wxDECLARE_EVENT_TABLE();
};

//-----------------------------------------------------------------------------
// ibSpinCtrl
//-----------------------------------------------------------------------------

class GUI_API ibSpinCtrl : public ibSpinCtrlGenericBase
{
public:
    ibSpinCtrl() { Init(); }
    ibSpinCtrl(wxWindow *parent,
               wxWindowID id = wxID_ANY,
               const wxString& value = wxEmptyString,
               const wxPoint& pos = wxDefaultPosition,
               const wxSize& size = wxDefaultSize,
               long style = wxSP_ARROW_KEYS,
               int min = 0, int max = 100, int initial = 0,
               const wxString& name = wxT("wxSpinCtrl"))
    {
        Init();

        Create(parent, id, value, pos, size, style, min, max, initial, name);
    }

    bool Create(wxWindow *parent,
                wxWindowID id = wxID_ANY,
                const wxString& value = wxEmptyString,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = wxSP_ARROW_KEYS,
                int min = 0, int max = 100, int initial = 0,
                const wxString& name = wxT("wxSpinCtrl"))
    {
        return ibSpinCtrlGenericBase::Create(parent, id, value, pos, size,
                                             style, min, max, initial, 1, name);
    }

    // accessors
    int GetValue() const { return int(DoGetValue()); }
    int GetMin() const { return int(m_min); }
    int GetMax() const { return int(m_max); }
    int GetIncrement() const { return int(m_increment); }

    // operations
    virtual void SetValue(const wxString& value) override
        { ibSpinCtrlGenericBase::SetValue(value); }
    void SetValue( int value )              { DoSetValue(value, SendEvent_None); }
    void SetRange( int minVal, int maxVal ) { DoSetRange(minVal, maxVal); }
    void SetIncrement(int inc) { DoSetIncrement(inc); }

    virtual int GetBase() const override { return m_base; }
    virtual bool SetBase(int base) override;

protected:
    virtual void DoSendEvent() override;

    virtual bool DoTextToValue(const wxString& text, double *val) override;
    virtual wxString DoValueToText(double val) override;
    virtual void ResetTextValidator() override;

private:
    // Common part of all ctors.
    void Init()
    {
        m_base = 10;
    }

    int m_base;

    wxDECLARE_DYNAMIC_CLASS(ibSpinCtrl);
};

//-----------------------------------------------------------------------------
// ibSpinCtrlDouble
//-----------------------------------------------------------------------------

class GUI_API ibSpinCtrlDouble : public ibSpinCtrlGenericBase
{
public:
    ibSpinCtrlDouble() { Init(); }
    ibSpinCtrlDouble(wxWindow *parent,
                     wxWindowID id = wxID_ANY,
                     const wxString& value = wxEmptyString,
                     const wxPoint& pos = wxDefaultPosition,
                     const wxSize& size = wxDefaultSize,
                     long style = wxSP_ARROW_KEYS,
                     double min = 0, double max = 100, double initial = 0,
                     double inc = 1,
                     const wxString& name = wxT("wxSpinCtrlDouble"))
    {
        Init();

        Create(parent, id, value, pos, size, style,
               min, max, initial, inc, name);
    }

    bool Create(wxWindow *parent,
                wxWindowID id = wxID_ANY,
                const wxString& value = wxEmptyString,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = wxSP_ARROW_KEYS,
                double min = 0, double max = 100, double initial = 0,
                double inc = 1,
                const wxString& name = wxT("wxSpinCtrlDouble"));

    // accessors
    double GetValue() const { return DoGetValue(); }
    double GetMin() const { return m_min; }
    double GetMax() const { return m_max; }
    double GetIncrement() const { return m_increment; }
    unsigned GetDigits() const { return m_digits; }

    // operations
    void SetValue(const wxString& value) override
        { ibSpinCtrlGenericBase::SetValue(value); }
    void SetValue(double value)                 { DoSetValue(value, SendEvent_None); }
    void SetRange(double minVal, double maxVal) { DoSetRange(minVal, maxVal); }
    void SetIncrement(double inc);
    void SetDigits(unsigned digits);

    // We don't implement bases support for floating point numbers, this is not
    // very useful in practice.
    virtual int GetBase() const override { return 10; }
    virtual bool SetBase(int WXUNUSED(base)) override { return false; }

protected:
    virtual void DoSendEvent() override;

    virtual bool DoTextToValue(const wxString& text, double *val) override;
    virtual wxString DoValueToText(double val) override;
    virtual void ResetTextValidator() override;

    unsigned m_digits;

private:
    // Common part of all ctors.
    void Init()
    {
        DoSetDigits(0);
    }

    // Just set the number of digits and the format unconditionally.
    void DoSetDigits(unsigned digits);

    // Call DoSetDigits() and update the appearance.
    void DoSetDigitsAndUpdate(unsigned digits);


    wxDECLARE_DYNAMIC_CLASS(ibSpinCtrlDouble);
};

#endif // wxUSE_SPINCTRL

#endif // _GUI_UNIV_SPINCTRL_H_
