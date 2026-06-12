// OWN uikit control (replaces the FORKED univ version): the univ combobox
// rode the generic wxComboCtrl host which lives outside the uikit theme
// chain; this one is a pure uikit composition — an ibControl host drawing
// the value area + drop arrow, an ibTextCtrl child for editable mode and a
// transient popup window with an ibListBox carrying the items.

#ifndef _WX_UNIV_COMBOBOX_H_
#define _WX_UNIV_COMBOBOX_H_

#include "frontend/frontend.h"

#include "frontend/uikit/inputHandler.h"
#include "frontend/uikit/ctrl/control.h"

#include <wx/ctrlsub.h>

class ibListBox;
class ibTextCtrl;
class ibComboPopupWindow;

// ----------------------------------------------------------------------------
// actions
// ----------------------------------------------------------------------------

// choose the next/prev/specified (by numArg) item
#define ibACTION_COMBOBOX_SELECT_NEXT wxT("next")
#define ibACTION_COMBOBOX_SELECT_PREV wxT("prev")
#define ibACTION_COMBOBOX_SELECT      wxT("select")

// show/hide the popup listbox
#define ibACTION_COMBOBOX_POPUP       wxT("popup")
#define ibACTION_COMBOBOX_DISMISS     wxT("dismiss")

// ----------------------------------------------------------------------------
// ibComboBox: a combination of a value area, a drop button and a popup list
// ----------------------------------------------------------------------------

class FRONTEND_API ibComboBox : public ibControl, public wxItemContainer
{
public:
    // ctors and such
    ibComboBox() { Init(); }

    ibComboBox(wxWindow *parent,
               wxWindowID id,
               const wxString& value = wxEmptyString,
               const wxPoint& pos = wxDefaultPosition,
               const wxSize& size = wxDefaultSize,
               int n = 0,
               const wxString choices[] = nullptr,
               long style = 0,
               const wxValidator& validator = wxDefaultValidator,
               const wxString& name = wxASCII_STR(wxComboBoxNameStr))
    {
        Init();

        (void)Create(parent, id, value, pos, size, n, choices,
                     style, validator, name);
    }
    ibComboBox(wxWindow *parent,
               wxWindowID id,
               const wxString& value,
               const wxPoint& pos,
               const wxSize& size,
               const wxArrayString& choices,
               long style = 0,
               const wxValidator& validator = wxDefaultValidator,
               const wxString& name = wxASCII_STR(wxComboBoxNameStr));

    bool Create(wxWindow *parent,
                wxWindowID id,
                const wxString& value = wxEmptyString,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                int n = 0,
                const wxString choices[] = nullptr,
                long style = 0,
                const wxValidator& validator = wxDefaultValidator,
                const wxString& name = wxASCII_STR(wxComboBoxNameStr));
    bool Create(wxWindow *parent,
                wxWindowID id,
                const wxString& value,
                const wxPoint& pos,
                const wxSize& size,
                const wxArrayString& choices,
                long style = 0,
                const wxValidator& validator = wxDefaultValidator,
                const wxString& name = wxASCII_STR(wxComboBoxNameStr));

    virtual ~ibComboBox();

    // value access (the full wxTextEntry surface lives on the editable
    // child; the combo itself only needs the value semantics)
    wxString GetValue() const;
    void SetValue(const wxString& value);
    void ChangeValue(const wxString& value);

    bool IsEditable() const { return !HasFlag(wxCB_READONLY); }

    // popup control
    void ShowPopup();
    void HidePopup();
    bool IsPopupShown() const;

    // wxItemContainer implementation (items live in the popup ibListBox)
    virtual void DoClear() override;
    virtual void DoDeleteOneItem(unsigned int n) override;
    virtual unsigned int GetCount() const override;
    virtual wxString GetString(unsigned int n) const override;
    virtual void SetString(unsigned int n, const wxString& s) override;
    virtual int FindString(const wxString& s, bool bCase = false) const override;
    virtual void SetSelection(int n) override;
    virtual int GetSelection() const override;
    virtual wxString GetStringSelection() const override;

    virtual wxClientDataType GetClientDataType() const override;
    virtual void SetClientDataType(wxClientDataType clientDataItemsType) override;

    // actions
    virtual bool PerformAction(const ibControlAction& action,
                               long numArg = -1l,
                               const wxString& strArg = wxEmptyString) override;

    static ibInputHandler *GetStdInputHandler(ibInputHandler *handlerDef);
    virtual ibInputHandler *DoGetStdInputHandler(ibInputHandler *handlerDef) override
    {
        return GetStdInputHandler(handlerDef);
    }

    // implementation only: called by the popup window / popup listbox
    void OnPopupSelect(int selection);
    void OnPopupDismiss();

protected:
    virtual int DoInsertItems(const wxArrayStringsAdapter& items,
                              unsigned int pos,
                              void **clientData, wxClientDataType type) override;

    virtual void DoSetItemClientData(unsigned int n, void* clientData) override;
    virtual void* DoGetItemClientData(unsigned int n) const override;

    // geometry/drawing
    virtual wxSize DoGetBestClientSize() const override;
    virtual void DoDraw(ibControlRenderer *renderer) override;

    // the rect of the drop button (client coords)
    wxRect GetButtonRect() const;
    // the rect of the value area (client coords)
    wxRect GetValueRect() const;

    // event handlers
    void OnLeftDown(wxMouseEvent& event);
    void OnSize(wxSizeEvent& event);

    // send wxEVT_COMBOBOX for the current selection
    void SendComboEvent();

    // common part of all ctors
    void Init();

    // get the popup listbox (always exists after Create)
    ibListBox *GetLBox() const { return m_lbox; }

private:
    // the popup window owning the listbox (child of this control,
    // created in Create(), destroyed with it)
    ibComboPopupWindow *m_popup;

    // the items listbox inside m_popup
    ibListBox *m_lbox;

    // the value text box (read-only one for wxCB_READONLY) — the combo IS
    // a text box + a drop-down list
    ibTextCtrl *m_text;

    wxDECLARE_EVENT_TABLE();
    wxDECLARE_DYNAMIC_CLASS(ibComboBox);
};

#endif // _WX_UNIV_COMBOBOX_H_
