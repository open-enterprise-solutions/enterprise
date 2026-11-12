// OWN deskgui control: the host of a drop-down (wx's wxComboCtrl, common/combocmn + generic/combog) - a value area
// with an ibTextCtrl child (a read-only one for wxCB_READONLY), a square drop button and a transient window holding
// what an ibComboPopup drops down. Taken out of ibComboBox (2026-11-10): the combo box is this host with a list
// (ibVListBoxComboPopup, comboBox.cpp); a date field is it with a calendar.

#ifndef _DESKGUI_UNIV_COMBOCTRL_H_
#define _DESKGUI_UNIV_COMBOCTRL_H_

#include "deskgui/deskgui.h"

#include "deskgui/univ/inputHandler.h"
#include "deskgui/univ/ctrl/control.h"

#include <wx/combo.h>   // wxCC_SPECIAL_DCLICK - wx's flag, as wxCB_READONLY

class ibTextCtrl;
class ibComboCtrl;
class ibComboPopupWindow;

// ----------------------------------------------------------------------------
// actions
// ----------------------------------------------------------------------------

// show/hide the popup
#define ibACTION_COMBOBOX_POPUP       wxT("popup")
#define ibACTION_COMBOBOX_DISMISS     wxT("dismiss")

// ----------------------------------------------------------------------------
// ibComboPopup: what an ibComboCtrl drops down (wx's wxComboPopup)
// ----------------------------------------------------------------------------

// A popup is its own control - a window made in the host's transient window and destroyed with it, as wx's list and
// calendar popups are; the host shows it, sizes it and asks it to paint the value's strip.
class DESKGUI_API ibComboPopup
{
public:
    virtual ~ibComboPopup() = default;

    // the popup's window, made in parent - the host's transient window
    virtual bool Create(wxWindow* parent) = 0;
    virtual wxWindow* GetControl() = 0;

    // the host's value, as it is set (SetValue, ChangeValue) and as the popup opens: a list takes its item for the
    // chosen one, a calendar its day (wx's wxComboCtrlBase::OnSetValue and ShowPopup)
    virtual void SetStringValue(const wxString& value);

    // the popup's size under the host: minWidth is the host's width, prefHeight its wish (wxDefaultCoord: none),
    // maxHeight the room on the screen
    virtual wxSize GetAdjustedSize(int minWidth, int prefHeight, int maxHeight) = 0;

    // the popup is shown, sized: a list is told its items and marks the chosen one (wx's wxComboPopup::OnPopup)
    virtual void OnPopup();

    // the host's strip of SetCustomPaintWidth, left of its text: the value's picture
    virtual void PaintComboControl(wxDC& dc, const wxRect& rect);

    // a double click on the value of a read-only host of wxCC_SPECIAL_DCLICK: a list steps to its next item
    virtual void OnComboDoubleClick();

    ibComboCtrl* GetComboCtrl() const { return m_combo; }

protected:
    // the host, set as it takes the popup (SetPopupControl)
    ibComboCtrl* m_combo = nullptr;

    friend class ibComboCtrl;
};

// ----------------------------------------------------------------------------
// ibComboCtrl: a value area, a drop button and a popup
// ----------------------------------------------------------------------------

class DESKGUI_API ibComboCtrl : public ibControl
{
public:
    // ctors and such
    ibComboCtrl() { Init(); }

    ibComboCtrl(wxWindow *parent,
                wxWindowID id,
                const wxString& value = wxEmptyString,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = 0,
                const wxValidator& validator = wxDefaultValidator,
                const wxString& name = wxASCII_STR(wxComboBoxNameStr))
    {
        Init();

        (void)Create(parent, id, value, pos, size, style, validator, name);
    }

    bool Create(wxWindow *parent,
                wxWindowID id,
                const wxString& value = wxEmptyString,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = 0,
                const wxValidator& validator = wxDefaultValidator,
                const wxString& name = wxASCII_STR(wxComboBoxNameStr));

    virtual ~ibComboCtrl();

    // actions
    virtual bool PerformAction(const ibControlAction& action,
                               long numArg = -1l,
                               const wxString& strArg = wxEmptyString) override;

    virtual ibInputHandler *DoGetStdInputHandler(ibInputHandler *handlerDef) override
    {
        return GetStdInputHandler(handlerDef);
    }

    // a field, as the text box it is made of: it lives on the content colour. Its read-only value is a box with no frame
    // of its own, which takes the surface under it (ibTextCtrl::HasContentBackground) - this; a choice stood grey inside
    // beside a white field once that box stopped being content by itself (2026-11-12, Max). On GTK3/Cocoa the same - the
    // combo is the engine's there too
    virtual bool HasContentBackground() const override { return true; }

    // the popup, set once: its control is made in the host's transient window and goes with it
    void SetPopupControl(ibComboPopup* popup);
    ibComboPopup* GetPopupControl() const { return m_popup; }

    // value access (the full wxTextEntry surface lives on the text child; the host only needs the value semantics)
    wxString GetValue() const;
    void SetValue(const wxString& value);
    void ChangeValue(const wxString& value);
    void SetInsertionPointEnd();

    bool IsEditable() const { return !HasFlag(wxCB_READONLY); }

    // the value's field
    ibTextCtrl* GetTextCtrl() const { return m_text; }

    // popup control
    void ShowPopup();
    void HidePopup();
    bool IsPopupShown() const;

    // the strip left of the text the popup paints (PaintComboControl) - the value's picture; 0, the default: none
    void SetCustomPaintWidth(int width);
    int GetCustomPaintWidth() const { return m_widthCustomPaint; }

    // the value area - all but the drop button: the strip and the text (client coords)
    wxRect GetTextRect() const;

    // implementation only: called by the popup window
    void OnPopupDismiss();

    static ibInputHandler *GetStdInputHandler(ibInputHandler *handlerDef);

protected:
    // geometry/drawing
    virtual wxSize DoGetBestClientSize() const override;

    // the rect of the drop button (client coords)
    wxRect GetButtonRect() const;

    // the text child in the value area, past the strip
    void PositionTextCtrl();

    // event handlers
    void OnPaint(wxPaintEvent& event);
    void OnLeftDown(wxMouseEvent& event);
    void OnLeftDClick(wxMouseEvent& event);
    void OnSetFocus(wxFocusEvent& event);
    void OnSize(wxSizeEvent& event);

    // common part of all ctors
    void Init();

private:
    // the transient window the popup's control is made in (a child of this control, created by SetPopupControl,
    // destroyed with it)
    ibComboPopupWindow *m_winPopup;

    // what drops down
    ibComboPopup *m_popup;

    // the value text box (read-only one for wxCB_READONLY) — the combo IS
    // a text box + a drop-down
    ibTextCtrl *m_text;

    // the width of the strip the popup paints, left of the text
    int m_widthCustomPaint;

    wxDECLARE_EVENT_TABLE();
    wxDECLARE_DYNAMIC_CLASS(ibComboCtrl);
};

#endif // _DESKGUI_UNIV_COMBOCTRL_H_
