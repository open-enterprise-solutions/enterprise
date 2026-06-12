// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to frontend.vcxproj
// (set ObjectFileName on a name clash with visualView), add to the demo form.

///////////////////////////////////////////////////////////////////////////////
// Name:        wx/univ/choice.h
// Purpose:     the universal choice
// Author:      Vadim Zeitlin
// Created:     30.08.00
// Copyright:   (c) 2000 SciTech Software, Inc. (www.scitechsoft.com)
// Licence:     ibWindows licence
///////////////////////////////////////////////////////////////////////////////

#ifndef _WX_UNIV_CHOICE_H_
#define _WX_UNIV_CHOICE_H_

#include "frontend/frontend.h"

#include "frontend/uikit/ctrl/comboBox.h"

#include <wx/combobox.h>

// VS: This is only a *temporary* implementation, real ibChoice should not
//     derive from ibComboBox and may have different l&f
class FRONTEND_API ibChoice : public ibComboBox
{
public:
    ibChoice() = default;
    ibChoice(wxWindow *parent, wxWindowID id,
            const wxPoint& pos = wxDefaultPosition,
            const wxSize& size = wxDefaultSize,
            int n = 0, const wxString choices[] = nullptr,
            long style = 0,
            const wxValidator& validator = wxDefaultValidator,
            const wxString& name = wxASCII_STR(wxChoiceNameStr))
    {
        Create(parent, id, pos, size, n, choices, style, validator, name);
    }
    ibChoice(wxWindow *parent, wxWindowID id,
            const wxPoint& pos,
            const wxSize& size,
            const wxArrayString& choices,
            long style = 0,
            const wxValidator& validator = wxDefaultValidator,
            const wxString& name = wxASCII_STR(wxChoiceNameStr));

    bool Create(wxWindow *parent, wxWindowID id,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                int n = 0, const wxString choices[] = nullptr,
                long style = 0,
                const wxValidator& validator = wxDefaultValidator,
                const wxString& name = wxASCII_STR(wxChoiceNameStr));
    bool Create(wxWindow *parent, wxWindowID id,
                const wxPoint& pos,
                const wxSize& size,
                const wxArrayString& choices,
                long style = 0,
                const wxValidator& validator = wxDefaultValidator,
                const wxString& name = wxASCII_STR(wxChoiceNameStr));

private:
    void OnComboBox(wxCommandEvent &event);

    wxDECLARE_EVENT_TABLE();
    wxDECLARE_DYNAMIC_CLASS(ibChoice);
};


#endif // _WX_UNIV_CHOICE_H_
