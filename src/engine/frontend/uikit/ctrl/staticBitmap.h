// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to frontend.vcxproj
// (set ObjectFileName on a name clash with visualView), add to the demo form.

/////////////////////////////////////////////////////////////////////////////
// Name:        wx/univ/statbmp.h
// Purpose:     ibStaticBitmap class for wxUniversal
// Author:      Vadim Zeitlin
// Created:     25.08.00
// Copyright:   (c) 2000 SciTech Software, Inc. (www.scitechsoft.com)
// Licence:     ibWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_UNIV_STATBMP_H_
#define _WX_UNIV_STATBMP_H_

#include "frontend/frontend.h"

#include <wx/bitmap.h>
#include <wx/bmpbndl.h>

#include "frontend/uikit/ctrl/control.h"

// ----------------------------------------------------------------------------
// ibStaticBitmap
// ----------------------------------------------------------------------------

// SEAM vs univ: wxStaticBitmapBase rides the native wxControl chain — derive
// from ibControl and keep the bitmap bundle ourselves
class FRONTEND_API ibStaticBitmap : public ibControl
{
public:
    ibStaticBitmap()
    {
    }

    ibStaticBitmap(wxWindow *parent,
                   const wxBitmapBundle& label,
                   const wxPoint& pos = wxDefaultPosition,
                   const wxSize& size = wxDefaultSize,
                   long style = 0)
    {
        Create(parent, wxID_ANY, label, pos, size, style);
    }

    ibStaticBitmap(wxWindow *parent,
                   wxWindowID id,
                   const wxBitmapBundle& label,
                   const wxPoint& pos = wxDefaultPosition,
                   const wxSize& size = wxDefaultSize,
                   long style = 0,
                   const wxString& name = wxASCII_STR(wxStaticBitmapNameStr))
    {
        Create(parent, id, label, pos, size, style, name);
    }

    bool Create(wxWindow *parent,
                wxWindowID id,
                const wxBitmapBundle& label,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = 0,
                const wxString& name = wxASCII_STR(wxStaticBitmapNameStr));

    virtual void SetBitmap(const wxBitmapBundle& bitmap);
    wxBitmap GetBitmap() const { return m_bitmapBundle.GetBitmapFor(this); }

    virtual bool HasTransparentBackground() override { return true; }

protected:
    virtual void DoDraw(ibControlRenderer *renderer) override;

    // size to the bitmap (the lost base did this in DoGetBestSize)
    virtual wxSize DoGetBestClientSize() const override
    {
        return m_bitmapBundle.IsOk()
            ? m_bitmapBundle.GetPreferredBitmapSizeFor(this)
            : wxSize(16, 16);
    }

    // the bitmap bundle (lived in the lost wxStaticBitmapBase)
    wxBitmapBundle m_bitmapBundle;

private:
    wxDECLARE_DYNAMIC_CLASS(ibStaticBitmap);
};

#endif // _WX_UNIV_STATBMP_H_
