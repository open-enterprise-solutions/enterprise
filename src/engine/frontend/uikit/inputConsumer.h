// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to frontend.vcxproj
// (set ObjectFileName on a name clash with visualView), add to the demo form.

/////////////////////////////////////////////////////////////////////////////
// Name:        wx/univ/inpcons.h
// Purpose:     ibInputConsumer: mix-in class for input handling
// Author:      Vadim Zeitlin
// Created:     14.08.00
// Copyright:   (c) 2000 SciTech Software, Inc. (www.scitechsoft.com)
// Licence:     ibWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_UNIV_INPCONS_H_
#define _WX_UNIV_INPCONS_H_

#include "frontend/frontend.h"

class ibInputHandler;
class ibWindow;

#include <wx/object.h>
#include <wx/event.h>

// ----------------------------------------------------------------------------
// ibControlAction: the action is currently just a string which identifies it,
// later it might become an atom (i.e. an opaque handler to string).
// ----------------------------------------------------------------------------

typedef wxString ibControlAction;

// the list of actions which apply to all controls (other actions are defined
// in the controls headers)

#define ibACTION_NONE    wxT("")           // no action to perform

// ----------------------------------------------------------------------------
// ibInputConsumer: mix-in class for handling ibControlActions (used by
// ibControl and ibTopLevelWindow).
// ----------------------------------------------------------------------------

class FRONTEND_API ibInputConsumer
{
public:
    ibInputConsumer() { m_inputHandler = nullptr; }
    virtual ~ibInputConsumer() = default;

    // get the input handler
    ibInputHandler *GetInputHandler() const { return m_inputHandler; }

    // perform a control-dependent action: an action may have an optional
    // numeric and another (also optional) string argument whose interpretation
    // depends on the action
    //
    // NB: we might use ellipsis in PerformAction() declaration but this
    //     wouldn't be more efficient than always passing 2 unused parameters
    //     but would be more difficult. Another solution would be to have
    //     several overloaded versions but this will expose the problem of
    //     virtual function hiding we don't have here.
    virtual bool PerformAction(const ibControlAction& action,
                               long numArg = -1l,
                               const wxString& strArg = wxEmptyString);

    // get the window to work with (usually the class ibInputConsumer was mixed into)
    virtual ibWindow *GetInputWindow() const = 0;

    // this function must be implemented in any classes process input (i.e. not
    // static controls) to create the standard input handler for the concrete
    // class deriving from this mix-in
    //
    // the parameter is the default input handler which should receive all
    // unprocessed input (i.e. typically handlerDef is passed to
    // ibStdInputHandler ctor) or it may be null
    //
    // the returned pointer will not be deleted by caller so it must either
    // point to a static object or be deleted on program termination
    virtual ibInputHandler *DoGetStdInputHandler(ibInputHandler *handlerDef);


protected:
    // event handlers
    void OnMouse(wxMouseEvent& event);
    void OnKeyDown(wxKeyEvent& event);
    void OnKeyUp(wxKeyEvent& event);
    void OnFocus(wxFocusEvent& event);
    void OnActivate(wxActivateEvent& event);

    // create input handler by name, fall back to GetStdInputHandler() if
    // the current theme doesn't define any specific handler of this type
    void CreateInputHandler(const wxString& inphandler);

private:
    // the input processor (we never delete it)
    ibInputHandler *m_inputHandler;
};


// ----------------------------------------------------------------------------
// macros which must be used by the classes derived from ibInputConsumer mix-in
// ----------------------------------------------------------------------------

// declare the methods to be forwarded
#define WX_DECLARE_INPUT_CONSUMER() \
private: \
    void OnMouse(wxMouseEvent& event); \
    void OnKeyDown(wxKeyEvent& event); \
    void OnKeyUp(wxKeyEvent& event); \
    void OnFocus(wxFocusEvent& event); \
public: /* because of docview :-( */ \
    void OnActivate(wxActivateEvent& event); \
private:

// implement the event table entries for wxControlContainer
#define WX_EVENT_TABLE_INPUT_CONSUMER(classname) \
    EVT_KEY_DOWN(classname::OnKeyDown) \
    EVT_KEY_UP(classname::OnKeyUp) \
    EVT_MOUSE_EVENTS(classname::OnMouse) \
    EVT_SET_FOCUS(classname::OnFocus) \
    EVT_KILL_FOCUS(classname::OnFocus) \
    EVT_ACTIVATE(classname::OnActivate)

// Forward event handlers to ibInputConsumer
//
// (We can't use them directly, because wxIC has virtual methods, which forces
// the compiler to include (at least) two vtables into ibControl, one for the
// ibWindow-wxControlBase-ibControl branch and one for the wxIC mix-in.
// Consequently, the "this" pointer has different value when in ibControl's
// and wxIC's method, even though the instance stays same. This doesn't matter
// so far as member pointers aren't used, but that's not ibControl's case.
// When we add an event table entry (= use a member pointer) pointing to
// wxIC's OnXXX method, GCC compiles code that executes wxIC::OnXXX with the
// version of "this" that belongs to ibControl, not wxIC! In our particular
// case, the effect is that m_handler is null (probably same memory
// area as the_other_vtable's_this->m_refObj) and input handling doesn't work.)
#define WX_FORWARD_TO_INPUT_CONSUMER(classname) \
    void classname::OnMouse(wxMouseEvent& event) \
    { \
        ibInputConsumer::OnMouse(event); \
    } \
    void classname::OnKeyDown(wxKeyEvent& event) \
    { \
        ibInputConsumer::OnKeyDown(event); \
    } \
    void classname::OnKeyUp(wxKeyEvent& event) \
    { \
        ibInputConsumer::OnKeyUp(event); \
    } \
    void classname::OnFocus(wxFocusEvent& event) \
    { \
        ibInputConsumer::OnFocus(event); \
    } \
    void classname::OnActivate(wxActivateEvent& event) \
    { \
        ibInputConsumer::OnActivate(event); \
    }

#endif // _WX_UNIV_INPCONS_H_
