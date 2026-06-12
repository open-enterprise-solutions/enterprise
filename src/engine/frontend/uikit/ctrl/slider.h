// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to frontend.vcxproj
// (set ObjectFileName on a name clash with visualView), add to the demo form.

///////////////////////////////////////////////////////////////////////////////
// Name:        wx/univ/slider.h
// Purpose:     ibSlider control for wxUniversal
// Author:      Vadim Zeitlin
// Created:     09.02.01
// Copyright:   (c) 2001 SciTech Software, Inc. (www.scitechsoft.com)
// Licence:     ibWindows licence
///////////////////////////////////////////////////////////////////////////////

#ifndef _WX_UNIV_SLIDER_H_
#define _WX_UNIV_SLIDER_H_

#include "frontend/frontend.h"

class ibWindow;

#include "frontend/uikit/ctrl/scrollThumb.h"

// ----------------------------------------------------------------------------
// the actions supported by this control
// ----------------------------------------------------------------------------

// our actions are the same as scrollbars

#define ibACTION_SLIDER_START       wxT("start")     // to the beginning
#define ibACTION_SLIDER_END         wxT("end")       // to the end
#define ibACTION_SLIDER_LINE_UP     wxT("lineup")    // one line up/left
#define ibACTION_SLIDER_PAGE_UP     wxT("pageup")    // one page up/left
#define ibACTION_SLIDER_LINE_DOWN   wxT("linedown")  // one line down/right
#define ibACTION_SLIDER_PAGE_DOWN   wxT("pagedown")  // one page down/right
#define ibACTION_SLIDER_PAGE_CHANGE wxT("pagechange")// change page by numArg

#define ibACTION_SLIDER_THUMB_DRAG      wxT("thumbdrag")
#define ibACTION_SLIDER_THUMB_MOVE      wxT("thumbmove")
#define ibACTION_SLIDER_THUMB_RELEASE   wxT("thumbrelease")

// ----------------------------------------------------------------------------
// ibSlider
// ----------------------------------------------------------------------------

#include <wx/slider.h>        // wxSL_* styles
#include "frontend/uikit/ctrl/control.h"

// SEAM vs univ: wxSliderBase sits on the native wxControl in our build
class FRONTEND_API ibSlider : public ibControl,
                             public ibControlWithThumb
{
public:
    // ctors and such
    ibSlider();

    ibSlider(wxWindow *parent,
             wxWindowID id,
             int value, int minValue, int maxValue,
             const wxPoint& pos = wxDefaultPosition,
             const wxSize& size = wxDefaultSize,
             long style = wxSL_HORIZONTAL,
             const wxValidator& validator = wxDefaultValidator,
             const wxString& name = wxASCII_STR(wxSliderNameStr));

    bool Create(wxWindow *parent,
                wxWindowID id,
                int value, int minValue, int maxValue,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = wxSL_HORIZONTAL,
                const wxValidator& validator = wxDefaultValidator,
                const wxString& name = wxASCII_STR(wxSliderNameStr));

    // implement base class pure virtuals
    virtual int GetValue() const ;
    virtual void SetValue(int value) ;

    virtual void SetRange(int minValue, int maxValue) ;
    virtual int GetMin() const ;
    virtual int GetMax() const ;

    virtual void SetLineSize(int lineSize) ;
    virtual void SetPageSize(int pageSize) ;
    virtual int GetLineSize() const ;
    virtual int GetPageSize() const ;

    virtual void SetThumbLength(int lenPixels) ;
    virtual int GetThumbLength() const ;

    virtual int GetTickFreq() const { return m_tickFreq; }

    // wxUniv-specific methods
    // -----------------------

    // is this a vertical slider?
    bool IsVert() const { return (GetWindowStyle() & wxSL_VERTICAL) != 0; }

    // get the slider orientation
    wxOrientation GetOrientation() const
        { return IsVert() ? wxVERTICAL : wxHORIZONTAL; }

    // do we have labels?
    bool HasLabels() const
        { return ((GetWindowStyle() & wxSL_LABELS) != 0) &&
                 ((GetWindowStyle() & (wxSL_TOP|wxSL_BOTTOM|wxSL_LEFT|wxSL_RIGHT)) != 0); }

    // do we have ticks?
    bool HasTicks() const
        { return ((GetWindowStyle() & wxSL_TICKS) != 0) &&
                 ((GetWindowStyle() & (wxSL_TOP|wxSL_BOTTOM|wxSL_LEFT|wxSL_RIGHT|wxSL_BOTH)) != 0); }

    // implement ibControlWithThumb interface
    virtual ibWindow *GetWindow() override { return this; }
    virtual bool IsVertical() const override { return IsVert(); }

    virtual ibScrollThumb::Shaft HitTest(const wxPoint& pt) const override;
    virtual wxCoord ThumbPosToPixel() const override;
    virtual int PixelToThumbPos(wxCoord x) const override;

    virtual void SetShaftPartState(ibScrollThumb::Shaft shaftPart,
                                   int flag,
                                   bool set = true) override;

    virtual void OnThumbDragStart(int pos) override;
    virtual void OnThumbDrag(int pos) override;
    virtual void OnThumbDragEnd(int pos) override;
    virtual void OnPageScrollStart() override;
    virtual bool OnPageScroll(int pageInc) override;

    // for ibStdSliderInputHandler
    ibScrollThumb& GetThumb() { return m_thumb; }

    virtual bool PerformAction(const ibControlAction& action,
                               long numArg = 0,
                               const wxString& strArg = wxEmptyString) override;

    static ibInputHandler *GetStdInputHandler(ibInputHandler *handlerDef);
    virtual ibInputHandler *DoGetStdInputHandler(ibInputHandler *handlerDef) override
    {
        return GetStdInputHandler(handlerDef);
    }

protected:
    enum
    {
        INVALID_THUMB_VALUE = -0xffff
    };

    // Platform-specific implementation of SetTickFreq
    virtual void DoSetTickFreq(int freq) ;

    // overridden base class virtuals
    virtual wxSize DoGetBestClientSize() const override;
    virtual void DoDraw(ibControlRenderer *renderer) override;
    virtual wxBorder GetDefaultBorder() const override { return wxBORDER_NONE; }

    // event handlers
    void OnSize(wxSizeEvent& event);

    // common part of all ctors
    void Init();

    // normalize the value to fit in the range
    int NormalizeValue(int value) const;

    // change the value by the given increment, return true if really changed
    bool ChangeValueBy(int inc);

    // change the value to the given one
    bool ChangeValueTo(int value);

    // is the value inside the range?
    bool IsInRange(int value) { return (value >= m_min) && (value <= m_max); }

    // format the value for printing as label
    virtual wxString FormatValue(int value) const;

    // calculate max label size
    wxSize CalcLabelSize() const;

    // calculate m_rectLabel/Slider
    void CalcGeometry();

    // get the thumb size
    wxSize GetThumbSize() const;

    // get the shaft rect (uses m_rectSlider which is supposed to be calculated)
    wxRect GetShaftRect() const;

    // calc the current thumb position using the shaft rect (if the pointer is
    // null, we calculate it here too)
    void CalcThumbRect(const wxRect *rectShaft,
                       wxRect *rectThumbOut,
                       wxRect *rectLabelOut,
                       int value = INVALID_THUMB_VALUE) const;

    // return the slider rect calculating it if needed
    const wxRect& GetSliderRect() const;

    // refresh the current thumb position
    void RefreshThumb();

private:
    // get the default thumb size (without using m_thumbSize)
    wxSize GetDefaultThumbSize() const;

    // the object which manages our thumb
    ibScrollThumb m_thumb;

    // the slider range and value
    int m_min,
        m_max,
        m_value;

    // the tick frequence (default is 1)
    int m_tickFreq;

    // the line and page increments (logical units)
    int m_lineSize,
        m_pageSize;

    // the size of the thumb (in pixels)
    int m_thumbSize;

    // the part of the client area reserved for the label, the ticks and the
    // part for the slider itself
    wxRect m_rectLabel,
           m_rectTicks,
           m_rectSlider;

    // the state of the thumb (wxCONTROL_XXX constants sum)
    int m_thumbFlags;

    wxDECLARE_EVENT_TABLE();
    wxDECLARE_DYNAMIC_CLASS(ibSlider);
};

#endif // _WX_UNIV_SLIDER_H_
