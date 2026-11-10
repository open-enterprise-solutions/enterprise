// FORKED from wxWidgets: wxEditableListBox (include/wx/editlbox.h, src/generic/editlbox.cpp), on the library's
// panel, list and buttons (wx -> ib prefixes applied).

/////////////////////////////////////////////////////////////////////////////
// Name:        wx/editlbox.h
// Purpose:     ListBox with editable items
// Author:      Vaclav Slavik
// Copyright:   (c) Vaclav Slavik
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////


#ifndef _DESKGUI_UNIV_EDITABLELISTBOX_H_
#define _DESKGUI_UNIV_EDITABLELISTBOX_H_

#include "deskgui/univ/panel.h"           // base class declaration

class ibBitmapButton;
class ibListCtrl;
class WXDLLIMPEXP_FWD_CORE wxListEvent;

#define ibEL_ALLOW_NEW          0x0100
#define ibEL_ALLOW_EDIT         0x0200
#define ibEL_ALLOW_DELETE       0x0400
#define ibEL_NO_REORDER         0x0800
#define ibEL_DEFAULT_STYLE      (ibEL_ALLOW_NEW | ibEL_ALLOW_EDIT | ibEL_ALLOW_DELETE)

extern DESKGUI_API const char ibEditableListBoxNameStr[];

// This class provides a composite control that lets the
// user easily enter list of strings

// SEAM vs wx: wxPanel, wxListCtrl, wxBitmapButton and wxStaticText there - the library's panel, list, buttons and
// label here (ibPanel, ibListCtrl, ibBitmapButton, ibStaticText), so the strings' editor is drawn by the theme
class DESKGUI_API ibEditableListBox : public ibPanel
{
public:
    ibEditableListBox() { Init(); }

    ibEditableListBox(wxWindow *parent, wxWindowID id,
                      const wxString& label,
                      const wxPoint& pos = wxDefaultPosition,
                      const wxSize& size = wxDefaultSize,
                      long style = ibEL_DEFAULT_STYLE,
                      const wxString& name = wxASCII_STR(ibEditableListBoxNameStr))
    {
        Init();
        Create(parent, id, label, pos, size, style, name);
    }

    bool Create(wxWindow *parent, wxWindowID id,
                const wxString& label,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = ibEL_DEFAULT_STYLE,
                const wxString& name = wxASCII_STR(ibEditableListBoxNameStr));

    void SetStrings(const wxArrayString& strings);
    void GetStrings(wxArrayString& strings) const;

    ibListCtrl* GetListCtrl()       { return m_listCtrl; }
    ibBitmapButton* GetDelButton()  { return m_bDel; }
    ibBitmapButton* GetNewButton()  { return m_bNew; }
    ibBitmapButton* GetUpButton()   { return m_bUp; }
    ibBitmapButton* GetDownButton() { return m_bDown; }
    ibBitmapButton* GetEditButton() { return m_bEdit; }

protected:
    ibBitmapButton *m_bDel, *m_bNew, *m_bUp, *m_bDown, *m_bEdit;
    ibListCtrl *m_listCtrl;
    int m_selection;
    long m_style;

    void Init()
    {
        m_style = 0;
        m_selection = 0;
        m_bEdit = m_bNew = m_bDel = m_bUp = m_bDown = nullptr;
        m_listCtrl = nullptr;
    }

    void OnItemSelected(wxListEvent& event);
    void OnEndLabelEdit(wxListEvent& event);
    void OnNewItem(wxCommandEvent& event);
    void OnDelItem(wxCommandEvent& event);
    void OnEditItem(wxCommandEvent& event);
    void OnUpItem(wxCommandEvent& event);
    void OnDownItem(wxCommandEvent& event);

    wxDECLARE_CLASS(ibEditableListBox);
    wxDECLARE_EVENT_TABLE();

private:
    void SwapItems(long i1, long i2);

};

#endif // _DESKGUI_UNIV_EDITABLELISTBOX_H_
