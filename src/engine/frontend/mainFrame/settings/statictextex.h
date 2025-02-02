#ifndef STATIC_TEXT_EX_H
#define STATIC_TEXT_EX_H

#include <wx/wx.h>

/**
 * Extended static text control. The text is centered vertically in this control.
 */
class StaticTextEx : public wxPanel
{

public:

    /**
     * Constructor.
     */
    StaticTextEx(wxWindow* parent, wxWindowID id, const wxString& label, const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize, long style = 0, const wxString& name = "staticText");

    /**
     * Called when the window needs to be painted.
     */
    void OnPaint(wxPaintEvent& event);

    wxDECLARE_EVENT_TABLE();

private:

    wxString    m_label;

};

#endif