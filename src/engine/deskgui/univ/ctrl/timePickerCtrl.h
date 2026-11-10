// FORKED from the wx GENERIC time picker (wxTimePickerCtrlGeneric -> ibTimePickerCtrl, mechanical ib prefixes): the
// time of day in the library's text field, its hours, minutes and seconds stepped by the keys and the library's spin
// button.

///////////////////////////////////////////////////////////////////////////////
// Name:        wx/generic/timectrl.h
// Purpose:     Generic implementation of wxTimePickerCtrl.
// Author:      Paul Breen, Vadim Zeitlin
// Created:     2011-09-22
// Copyright:   (c) 2011 Vadim Zeitlin <vadim@wxwidgets.org>
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

#ifndef _DESKGUI_UNIV_TIMEPICKERCTRL_H_
#define _DESKGUI_UNIV_TIMEPICKERCTRL_H_

#include "deskgui/deskgui.h"

#if wxUSE_TIMEPICKCTRL

#include <wx/timectrl.h>      // wx's: wxTP_DEFAULT, wxTimePickerCtrlNameStr
#include <wx/dateevt.h>       // wxDateEvent, wxEVT_TIME_CHANGED
#include <wx/compositewin.h>
#include <wx/containr.h>

#include "deskgui/univ/ctrl/control.h"

// SEAM vs wx: wxTimePickerCtrlCommonBase<wxDateTimePickerCtrlBase> (wx/timectrl.h, wx/datetimectrl.h) rides the
// native wxControl chain - its value and time are this class's own here, no base declares them; the event it sends -
// wxDateEvent with wxEVT_TIME_CHANGED - stays wx's.
class DESKGUI_API ibTimePickerCtrl
    : public wxCompositeWindow< wxNavigationEnabled<ibControl> >
{
public:
    typedef wxCompositeWindow< wxNavigationEnabled<ibControl> > Base;

    // Creating the control.
    ibTimePickerCtrl() { Init(); }
    ibTimePickerCtrl(wxWindow *parent,
                     wxWindowID id,
                     const wxDateTime& date = wxDefaultDateTime,
                     const wxPoint& pos = wxDefaultPosition,
                     const wxSize& size = wxDefaultSize,
                     long style = wxTP_DEFAULT,
                     const wxValidator& validator = wxDefaultValidator,
                     const wxString& name = wxTimePickerCtrlNameStr)
    {
        Init();

        (void)Create(parent, id, date, pos, size, style, validator, name);
    }

    bool Create(wxWindow *parent,
                wxWindowID id,
                const wxDateTime& date = wxDefaultDateTime,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = wxTP_DEFAULT,
                const wxValidator& validator = wxDefaultValidator,
                const wxString& name = wxTimePickerCtrlNameStr);

    virtual ~ibTimePickerCtrl();

    // the value: only its time part is the control's, the date part is ignored
    void SetValue(const wxDateTime& date);
    wxDateTime GetValue() const;

    // the time alone (wx's wxTimePickerCtrlCommonBase)
    bool SetTime(int hour, int min, int sec);
    bool GetTime(int* hour, int* min, int* sec) const;

protected:
    virtual wxSize DoGetBestSize() const override;
    virtual void DoMoveWindow(int x, int y, int width, int height) override;

private:
    // Return the list of the windows composing this one.
    virtual wxWindowList GetCompositeWindowParts() const override;

    void Init();

    // Implementation data.
    class ibTimePickerGenericImpl* m_impl;

    wxDECLARE_DYNAMIC_CLASS(ibTimePickerCtrl);
    wxDECLARE_NO_COPY_CLASS(ibTimePickerCtrl);
};

#endif // wxUSE_TIMEPICKCTRL

#endif // _DESKGUI_UNIV_TIMEPICKERCTRL_H_
