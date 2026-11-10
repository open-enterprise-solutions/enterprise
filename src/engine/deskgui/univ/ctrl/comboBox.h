// OWN deskgui control (replaces the FORKED univ version): the combo box - ibComboCtrl (comboCtrl.h, the value area,
// the drop button, the text child) with a list of the items it keeps itself (wxItemContainer) and draws itself, as
// wx's wxOwnerDrawnComboBox does: OnDrawItem paints an item in the list and the chosen one in the value's strip
// (SetCustomPaintWidth, wxODCB_PAINTING_CONTROL). The default draws the text - a combo that draws nothing of its own
// looks as a list box does.

#ifndef _DESKGUI_UNIV_COMBOBOX_H_
#define _DESKGUI_UNIV_COMBOBOX_H_

#include "deskgui/univ/ctrl/comboCtrl.h"

#include <wx/ctrlsub.h>
#include <wx/odcombo.h>   // wxODCB_PAINTING_CONTROL / _SELECTED, wxODCB_DCLICK_CYCLES - wx's flags, as wxCB_READONLY

#include <vector>

class ibVListBoxComboPopup;

// ----------------------------------------------------------------------------
// actions
// ----------------------------------------------------------------------------

// choose the next/prev/specified (by numArg) item
#define ibACTION_COMBOBOX_SELECT_NEXT wxT("next")
#define ibACTION_COMBOBOX_SELECT_PREV wxT("prev")
#define ibACTION_COMBOBOX_SELECT      wxT("select")

// ----------------------------------------------------------------------------
// ibComboBox: a combination of a value area, a drop button and a popup list
// ----------------------------------------------------------------------------

class DESKGUI_API ibComboBox : public ibComboCtrl, public wxItemContainer
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

    // wxItemContainer implementation (the items are kept here, the popup list shows them)
    virtual unsigned int GetCount() const override;
    virtual wxString GetString(unsigned int n) const override;
    virtual void SetString(unsigned int n, const wxString& s) override;
    virtual void SetSelection(int n) override;
    virtual int GetSelection() const override;
    virtual wxString GetStringSelection() const override;

    // actions
    virtual bool PerformAction(const ibControlAction& action,
                               long numArg = -1l,
                               const wxString& strArg = wxEmptyString) override;

    // implementation only: called by the popup list - the item chosen there (or stepped to, OnComboDoubleClick)
    void OnPopupSelect(int selection);

protected:
    virtual void DoClear() override;
    virtual void DoDeleteOneItem(unsigned int n) override;
    virtual int DoInsertItems(const wxArrayStringsAdapter& items,
                              unsigned int pos,
                              void **clientData, wxClientDataType type) override;

    virtual void DoSetItemClientData(unsigned int n, void* clientData) override;
    virtual void* DoGetItemClientData(unsigned int n) const override;

    // THE ITEMS ARE DRAWN BY THE COMBO (wx's wxOwnerDrawnComboBox): the list asks it for each item, the value's strip
    // for the chosen one with wxODCB_PAINTING_CONTROL; wxODCB_PAINTING_SELECTED marks the item under the mouse or the
    // keys. A derived combo draws its own.
    virtual void OnDrawItem(wxDC& dc, const wxRect& rect, int item, int flags) const;
    virtual wxCoord OnMeasureItem(size_t item) const;
    virtual wxCoord OnMeasureItemWidth(size_t item) const;
    virtual void OnDrawBackground(wxDC& dc, const wxRect& rect, int item, int flags) const;

    // send wxEVT_COMBOBOX for the current selection
    void SendComboEvent();

    // common part of all ctors
    void Init();

private:
    // the popup list showing the items (the host's popup, created in Create(), destroyed with it)
    ibVListBoxComboPopup *m_list;

    // the items and their client data (wxItemContainer keeps the data's kind and the objects' ownership)
    wxArrayString m_strings;
    std::vector<void*> m_clientData;

    // the chosen item - the list's own selection is the one under the mouse while it is open
    int m_selection;

    friend class ibVListBoxComboPopup;

    wxDECLARE_DYNAMIC_CLASS(ibComboBox);
};

#endif // _DESKGUI_UNIV_COMBOBOX_H_
