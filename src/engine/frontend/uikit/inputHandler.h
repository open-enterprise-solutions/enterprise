// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to frontend.vcxproj
// (set ObjectFileName on a name clash with visualView), add to the demo form.

///////////////////////////////////////////////////////////////////////////////
// Name:        wx/univ/inphand.h
// Purpose:     ibInputHandler class maps the keyboard and mouse events to the
//              actions which then are performed by the control
// Author:      Vadim Zeitlin
// Created:     18.08.00
// Copyright:   (c) 2000 SciTech Software, Inc. (www.scitechsoft.com)
// Licence:     ibWindows licence
///////////////////////////////////////////////////////////////////////////////

#ifndef _WX_UNIV_INPHAND_H_
#define _WX_UNIV_INPHAND_H_

#include "frontend/frontend.h"

#include "frontend/uikit/inputConsumer.h"         // for ibControlAction(s)

// ----------------------------------------------------------------------------
// types of the standard input handlers which can be passed to
// ibThemeEngine::GetInputHandler()
// ----------------------------------------------------------------------------

#define ibINP_HANDLER_DEFAULT           wxT("")
#define ibINP_HANDLER_BUTTON            wxT("button")
#define ibINP_HANDLER_CHECKBOX          wxT("checkbox")
#define ibINP_HANDLER_CHECKLISTBOX      wxT("checklistbox")
#define ibINP_HANDLER_COMBOBOX          wxT("combobox")
#define ibINP_HANDLER_LISTBOX           wxT("listbox")
#define ibINP_HANDLER_NOTEBOOK          wxT("notebook")
#define ibINP_HANDLER_RADIOBTN          wxT("radiobtn")
#define ibINP_HANDLER_SCROLLBAR         wxT("scrollbar")
#define ibINP_HANDLER_SLIDER            wxT("slider")
#define ibINP_HANDLER_SPINBTN           wxT("spinbtn")
#define ibINP_HANDLER_STATUSBAR         wxT("statusbar")
#define ibINP_HANDLER_TEXTCTRL          wxT("textctrl")
#define ibINP_HANDLER_TOOLBAR           wxT("toolbar")
#define ibINP_HANDLER_TOPLEVEL          wxT("toplevel")

// ----------------------------------------------------------------------------
// ibInputHandler: maps the events to the actions
// ----------------------------------------------------------------------------

class FRONTEND_API ibInputHandler : public wxObject
{
public:
    // map a keyboard event to one or more actions (pressed == true if the key
    // was pressed, false if released), returns true if something was done
    virtual bool HandleKey(ibInputConsumer *consumer,
                           const wxKeyEvent& event,
                           bool pressed) = 0;

    // map a mouse (click) event to one or more actions
    virtual bool HandleMouse(ibInputConsumer *consumer,
                             const wxMouseEvent& event) = 0;

    // handle mouse movement (or enter/leave) event: it is separated from
    // HandleMouse() for convenience as many controls don't care about mouse
    // movements at all
    virtual bool HandleMouseMove(ibInputConsumer *consumer,
                                 const wxMouseEvent& event);

    // do something with focus set/kill event: this is different from
    // HandleMouseMove() as the mouse maybe over the control without it having
    // focus
    //
    // return true to refresh the control, false otherwise
    virtual bool HandleFocus(ibInputConsumer *consumer, const wxFocusEvent& event);

    // react to the app getting/losing activation
    //
    // return true to refresh the control, false otherwise
    virtual bool HandleActivation(ibInputConsumer *consumer, bool activated);

    // virtual dtor for any base class
    virtual ~ibInputHandler();
};

// ----------------------------------------------------------------------------
// ibStdInputHandler is just a base class for all other "standard" handlers
// and also provides the way to chain input handlers together
// ----------------------------------------------------------------------------

class FRONTEND_API ibStdInputHandler : public ibInputHandler
{
public:
    ibStdInputHandler(ibInputHandler *handler) : m_handler(handler) { }

    virtual bool HandleKey(ibInputConsumer *consumer,
                           const wxKeyEvent& event,
                           bool pressed) override
    {
        return m_handler ? m_handler->HandleKey(consumer, event, pressed)
                         : false;
    }

    virtual bool HandleMouse(ibInputConsumer *consumer,
                             const wxMouseEvent& event) override
    {
        return m_handler ? m_handler->HandleMouse(consumer, event) : false;
    }

    virtual bool HandleMouseMove(ibInputConsumer *consumer, const wxMouseEvent& event) override
    {
        return m_handler ? m_handler->HandleMouseMove(consumer, event) : false;
    }

    virtual bool HandleFocus(ibInputConsumer *consumer, const wxFocusEvent& event) override
    {
        return m_handler ? m_handler->HandleFocus(consumer, event) : false;
    }

private:
    ibInputHandler *m_handler;
};

#endif // _WX_UNIV_INPHAND_H_
