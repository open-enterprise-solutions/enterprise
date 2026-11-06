// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to the demo form (docs/private/gui.md,
// the porting workflow).

///////////////////////////////////////////////////////////////////////////////
// Name:        wx/univ/button.h
// Purpose:     ibButton for wxUniversal
// Author:      Vadim Zeitlin
// Created:     15.08.00
// Copyright:   (c) 2000 SciTech Software, Inc. (www.scitechsoft.com)
// Licence:     ibWindows licence
///////////////////////////////////////////////////////////////////////////////

#ifndef _WX_UNIV_BUTTON_H_
#define _WX_UNIV_BUTTON_H_

#include "gui/gui.h"

#include "gui/univ/ctrl/anyButton.h"
#include <wx/anybutton.h>      // wxAnyButtonBase::State
#include <wx/bmpbndl.h>

// ----------------------------------------------------------------------------
// the actions supported by this control
// ----------------------------------------------------------------------------
//checkbox.cpp needed it, so not move it to anybutton.h
#define ibACTION_BUTTON_TOGGLE  wxT("toggle")    // press/release the button
#define ibACTION_BUTTON_PRESS   wxT("press")     // press the button
#define ibACTION_BUTTON_RELEASE wxT("release")   // release the button
#define ibACTION_BUTTON_CLICK   wxT("click")     // generate button click event

// ----------------------------------------------------------------------------
// ibButton: a push button
// ----------------------------------------------------------------------------

// SEAM vs univ: wxButtonBase sits on the native wxAnyButton in our build. What a form asks of its button came with the
// form's own (ibControlButton, owner-drawn on the native host, dissolved here 2026-11-06): a bitmap for each state and
// its place beside the label, a TILE (the bitmap on top, the label wrapped into the width, a note under it) and a
// HYPERLINK. The pieces and the face are drawn by the theme; the colours and their changes with the state are its own.
class GUI_API ibButton : public ibAnyButton
{
public:
    using State = wxAnyButtonBase::State;

    ibButton() { Init(); }
    ibButton(wxWindow *parent,
             wxWindowID id,
             const wxBitmapBundle& bitmap,
             const wxString& label = wxEmptyString,
             const wxPoint& pos = wxDefaultPosition,
             const wxSize& size = wxDefaultSize,
             long style = 0,
             const wxValidator& validator = wxDefaultValidator,
             const wxString& name = wxASCII_STR(wxButtonNameStr))
    {
        Init();

        Create(parent, id, bitmap, label, pos, size, style, validator, name);
    }

    ibButton(wxWindow *parent,
             wxWindowID id,
             const wxString& label = wxEmptyString,
             const wxPoint& pos = wxDefaultPosition,
             const wxSize& size = wxDefaultSize,
             long style = 0,
             const wxValidator& validator = wxDefaultValidator,
             const wxString& name = wxASCII_STR(wxButtonNameStr))
    {
        Init();

        Create(parent, id, label, pos, size, style, validator, name);
    }

    bool Create(wxWindow *parent,
                wxWindowID id,
                const wxString& label = wxEmptyString,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = 0,
                const wxValidator& validator = wxDefaultValidator,
                const wxString& name = wxASCII_STR(wxButtonNameStr))
    {
        return Create(parent, id, wxNullBitmap, label,
                      pos, size, style, validator, name);
    }

    bool Create(wxWindow *parent,
                wxWindowID id,
                const wxBitmapBundle& bitmap,
                const wxString& label = wxEmptyString,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = 0,
                const wxValidator& validator = wxDefaultValidator,
                const wxString& name = wxASCII_STR(wxButtonNameStr));

    virtual ~ibButton();

    virtual ibWindow *SetDefault();

    virtual bool IsPressed() const override { return m_isPressed; }
    virtual bool IsDefault() const override { return m_isDefault; }

    // ibButton actions
    virtual void Click() override;

    // a hyperlink has no face to highlight: it is drawn on what is under it
    virtual bool CanBeHighlighted() const override { return !m_isHyperlink; }

    // A sizer that gives a tile a width first asks its height there - its label wrapped into it.
    virtual wxSize GetMinSizeFromKnownDirection(int direction, int size, int availableOtherDir) override;

    // wxButtonBase shims: the bitmap and its place beside the label - on the left unless told otherwise; on top, the
    // button is a tile
    void SetBitmap(const wxBitmapBundle& bitmap, wxDirection dir = wxLEFT);
    void SetBitmapPosition(wxDirection dir);

    // The line under the label of a tile (SetBitmap(bitmap, wxTOP)): a price, a count. Drawn in that layout alone, and
    // not with wxBU_NOTEXT, which hides the label it stands under.
    void SetNote(const wxString& note);
    const wxString& GetNote() const { return m_note; }

    // Drawn as a HYPERLINK: no face and no frame, the label underlined in the theme's link colour and a hand over it -
    // the same button, its bitmap where it was and pressed the same way.
    void SetHyperlink(bool isHyperlink = true);
    bool IsHyperlink() const { return m_isHyperlink; }

    // a primary-action button is drawn as the default one (palette accent)
    void SetPrimary(bool primary = true) { m_isDefault = primary; Refresh(); }

    static wxSize GetDefaultSize(wxWindow* win = nullptr);

protected:
    // SEAM vs univ: a bitmap for each state (the canon kept the normal one alone); the state's own is drawn, or the
    // normal one - greyed by the button itself where the disabled one is not given
    virtual void DoSetBitmap(const wxBitmapBundle& bitmap, State which);
    virtual wxBitmap DoGetBitmap(State which) const;
    virtual void DoSetBitmapMargins(wxCoord x, wxCoord y);

    virtual wxSize DoGetBestClientSize() const override;
    virtual int DoGetBestClientHeight(int width) const override;

    virtual void DoDraw(ibControlRenderer *renderer) override;

    // common part of all ctors
    void Init();

private:
    wxBitmap BitmapForCurrentState() const;

    wxFont NoteFont() const { return GetFont().Smaller(); }
    // The note as this button draws it, measured in its face - a zero size where it draws none: only a tile with its
    // text shown has a line under the label (SetNote).
    wxSize GetNoteExtent() const;

    wxBitmapBundle m_bitmaps[wxAnyButtonBase::State_Max];
    wxDirection    m_bitmapPosition = wxLEFT;

    wxString       m_note;
    bool           m_isHyperlink = false;

    wxDECLARE_DYNAMIC_CLASS(ibButton);
};

#endif // _WX_UNIV_BUTTON_H_
