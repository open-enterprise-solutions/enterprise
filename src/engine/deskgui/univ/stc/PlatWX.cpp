// FORKED from wxWidgets' wxStyledTextCtrl (src/stc of wx 3.3; wx -> ib prefixes applied mechanically to the
// names it defines) - see stc.h.in.

// Scintilla source code edit control
// PlatWX.cxx - implementation of platform facilities on wxWidgets
// Copyright 1998-1999 by Neil Hodgson <neilh@scintilla.org>
//                        Robin Dunn <robin@aldunn.com>
// The License.txt file describes the conditions under which this software may be distributed.

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"


#if wxUSE_STC

#ifndef WX_PRECOMP
    #include "wx/math.h"
    #include "wx/dcmemory.h"
    #include "wx/settings.h"
#endif // WX_PRECOMP

#include "wx/display.h"
#include "wx/encconv.h"
#include "wx/listctrl.h"
#include "wx/mstream.h"
#include "wx/xpmdecod.h"
#include "wx/image.h"
#include "wx/tokenzr.h"
#include "wx/dynlib.h"
#include "wx/scopedarray.h"
#include "wx/toplevel.h"
#include "wx/sizer.h"
#include "wx/dcclient.h"
#include "wx/wupdlock.h"

#ifdef wxHAS_RAW_BITMAP
#include "wx/rawbmp.h"
#endif
#if wxUSE_GRAPHICS_CONTEXT
#include "wx/dcgraph.h"
#include "wx/graphics.h"
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <unordered_map>

#include "PlatWX.h"
#include "deskgui/univ/stc/stc.h"
#include "deskgui/univ/menu.h"
#include "deskgui/univ/ctrl/vListBox.h"
#include "deskgui/univ/theme.h"
#include "deskgui/univ/colourScheme.h"
#include "deskgui/univ/renderer.h"

// SEAM vs wx: no DirectWrite surface - every surface draws through a wxDC, whatever technology Scintilla is told

#if defined(__WXGTK__) && ibSTC_POPUP_IS_FRAME
    #include "wx/gtk/private/wrapgtk.h"
#elif defined(__WXMSW__)
    #include "wx/msw/wrapwin.h"
#elif defined(__WXOSX_COCOA__)
    #include "PlatWXcocoa.h"
#endif

wxRect wxRectFromPRectangle(PRectangle prc) {
    wxRect r(wxRound(prc.left), wxRound(prc.top),
             wxRound(prc.Width()), wxRound(prc.Height()));
    return r;
}

PRectangle PRectangleFromwxRect(wxRect rc) {
    return PRectangle(rc.GetLeft(), rc.GetTop(),
                      rc.GetRight()+1, rc.GetBottom()+1);
}

wxColour wxColourFromColourRGBA(ColourRGBA colour) {
    return wxColour(colour.GetRed(), colour.GetGreen(), colour.GetBlue(), colour.GetAlpha());
}

//----------------------------------------------------------------------

namespace Scintilla::Internal
{

inline wxWindow* GETWIN(WindowID id) { return (wxWindow*)id; }

// SEAM vs wx: Scintilla 5 derives the platform's font from Font and holds it by a shared_ptr - it was a wxFont
// subclass with the ascent cached behind Font::fid; the ascent is now the DC's font metrics (SurfaceImpl::Ascent)
class FontWX : public Font
{
public:
    explicit FontWX(const FontParameters &fp);

    static const wxFont& FromFont(const Font *font)
    {
        return static_cast<const FontWX*>(font)->m_font;
    }

private:
    wxFont m_font;
};

FontWX::FontWX(const FontParameters &fp) {
    // The minus one is done because since Scintilla uses SC_CHARSET_DEFAULT
    // internally and we need to have wxFONENCODING_DEFAULT == SC_SHARSET_DEFAULT
    // so we adjust the encoding before passing it to Scintilla.  See also
    // ibStyledTextCtrl::StyleSetCharacterSet
    wxFontEncoding encoding = (wxFontEncoding)(static_cast<int>(fp.characterSet)-1);

    wxFontEncodingArray ea = wxEncodingConverter::GetPlatformEquivalents(encoding);
    if (ea.GetCount())
        encoding = ea[0];

    const int fontWeight = static_cast<int>(fp.weight);
    wxFontWeight weight;
    if (fontWeight <= 300)
        weight = wxFONTWEIGHT_LIGHT;
    else if (fontWeight >= 700)
        weight = wxFONTWEIGHT_BOLD;
    else
        weight = wxFONTWEIGHT_NORMAL;

    m_font = wxFont(wxRound(fp.size),
        wxFONTFAMILY_DEFAULT,
        fp.italic ? wxFONTSTYLE_ITALIC :  wxFONTSTYLE_NORMAL,
        weight,
        false,
        wxString::FromUTF8(fp.faceName),
        encoding);
}

std::shared_ptr<Font> Font::Allocate(const FontParameters &fp) {
    return std::make_shared<FontWX>(fp);
}

//----------------------------------------------------------------------

class SurfaceImpl : public Surface {
private:
    wxDC*       hdc;
    bool        hdcOwned;
    wxBitmap    bitmap;
    WindowID    wid;

    // The clip rectangles SetClip stacked and PopClip takes off, each already cut by the one under it
    std::vector<wxRect> clips;

    // MRU brush cache.
    struct BrushCacheEntry {
        int usage = 0;
        ColourRGBA colour;
        wxBrush brush;
    };
    static std::array<BrushCacheEntry, 8> brushCache;

    // String reused for conversions from UTF-8 to wxString: this allows to
    // avoid reallocating memory all the time, e.g. for each line drawn.
    static wxString stcString;

    // The text is UTF-8 whatever the code page: ibStyledTextCtrl hands Scintilla every string as UTF-8
    const wxString& STCString(std::string_view text) {
        stcString.AssignFromUTF8Unchecked(text.data(), text.length());

        return stcString;
    }

public:
    SurfaceImpl();
    ~SurfaceImpl() noexcept override;

    virtual void Init(WindowID wid) override;
    virtual void Init(SurfaceID sid, WindowID wid) override;
    virtual std::unique_ptr<Surface> AllocatePixMap(int width, int height) override;

    virtual void SetMode(SurfaceMode mode_) override;

    virtual void Release() noexcept override;
    virtual int SupportsFeature(Supports feature) noexcept override;
    virtual bool Initialised() override;
    virtual int LogPixelsY() override;
    virtual int PixelDivisions() override;
    virtual int DeviceHeightFont(int points) override;
    virtual void LineDraw(Point start, Point end, Stroke stroke) override;
    virtual void PolyLine(const Point *pts, size_t npts, Stroke stroke) override;
    virtual void Polygon(const Point *pts, size_t npts, FillStroke fillStroke) override;
    virtual void RectangleDraw(PRectangle rc, FillStroke fillStroke) override;
    virtual void RectangleFrame(PRectangle rc, Stroke stroke) override;
    virtual void FillRectangle(PRectangle rc, Fill fill) override;
    virtual void FillRectangleAligned(PRectangle rc, Fill fill) override;
    virtual void FillRectangle(PRectangle rc, Surface &surfacePattern) override;
    virtual void RoundedRectangle(PRectangle rc, FillStroke fillStroke) override;
    virtual void AlphaRectangle(PRectangle rc, XYPOSITION cornerSize, FillStroke fillStroke) override;
    virtual void GradientRectangle(PRectangle rc, const std::vector<ColourStop> &stops,
                                   GradientOptions options) override;
    virtual void DrawRGBAImage(PRectangle rc, int width, int height,
                               const unsigned char *pixelsImage) override;
    virtual void Ellipse(PRectangle rc, FillStroke fillStroke) override;
    virtual void Stadium(PRectangle rc, FillStroke fillStroke, Ends ends) override;
    virtual void Copy(PRectangle rc, Point from, Surface &surfaceSource) override;

    virtual std::unique_ptr<IScreenLineLayout> Layout(const IScreenLine* screenLine) override;

    virtual void DrawTextNoClip(PRectangle rc, const Font *font_, XYPOSITION ybase, std::string_view text, ColourRGBA fore, ColourRGBA back) override;
    virtual void DrawTextClipped(PRectangle rc, const Font *font_, XYPOSITION ybase, std::string_view text, ColourRGBA fore, ColourRGBA back) override;
    virtual void DrawTextTransparent(PRectangle rc, const Font *font_, XYPOSITION ybase, std::string_view text, ColourRGBA fore) override;
    virtual void MeasureWidths(const Font *font_, std::string_view text, XYPOSITION *positions) override;
    virtual XYPOSITION WidthText(const Font *font_, std::string_view text) override;

    virtual void DrawTextNoClipUTF8(PRectangle rc, const Font *font_, XYPOSITION ybase, std::string_view text, ColourRGBA fore, ColourRGBA back) override;
    virtual void DrawTextClippedUTF8(PRectangle rc, const Font *font_, XYPOSITION ybase, std::string_view text, ColourRGBA fore, ColourRGBA back) override;
    virtual void DrawTextTransparentUTF8(PRectangle rc, const Font *font_, XYPOSITION ybase, std::string_view text, ColourRGBA fore) override;
    virtual void MeasureWidthsUTF8(const Font *font_, std::string_view text, XYPOSITION *positions) override;
    virtual XYPOSITION WidthTextUTF8(const Font *font_, std::string_view text) override;

    virtual XYPOSITION Ascent(const Font *font_) override;
    virtual XYPOSITION Descent(const Font *font_) override;
    virtual XYPOSITION InternalLeading(const Font *font_) override;
    virtual XYPOSITION Height(const Font *font_) override;
    virtual XYPOSITION AverageCharWidth(const Font *font_) override;

    virtual void SetClip(PRectangle rc) override;
    virtual void PopClip() override;
    virtual void FlushCachedState() override;
    virtual void FlushDrawing() override;

    void InitPixMap(int width, int height, wxDC* compatible, WindowID winid);
    void PenColour(ColourRGBA fore, XYPOSITION width = 1.0);
    void BrushColour(ColourRGBA back);
    void SetFont(const Font *font_);
};

std::array<SurfaceImpl::BrushCacheEntry, 8> SurfaceImpl::brushCache;
wxString SurfaceImpl::stcString;


SurfaceImpl::SurfaceImpl() :
    hdc(nullptr), hdcOwned(false), wid(nullptr)
{}

SurfaceImpl::~SurfaceImpl() noexcept {
    Release();
}

void SurfaceImpl::Init(WindowID wid_) {
    // On Mac and GTK the DC is not really valid until it has a bitmap
    // selected into it.  So instead of just creating the DC with no bitmap,
    // go ahead and give it one.
    InitPixMap(1, 1, nullptr, wid_);
}

void SurfaceImpl::Init(SurfaceID hdc_, WindowID wid_) {
    Release();
    hdc = (wxDC*)hdc_;
    wid = wid_;
}

// SEAM vs wx: Scintilla 5 asks the surface for a pixmap like itself (AllocatePixMap) - it handed InitPixMap the
// surface and the window; the window is the one this surface was initialised with
std::unique_ptr<Surface> SurfaceImpl::AllocatePixMap(int width, int height) {
    auto pixMap = std::make_unique<SurfaceImpl>();
    pixMap->InitPixMap(width, height, hdc, wid);
    return pixMap;
}

void SurfaceImpl::InitPixMap(int width, int height, wxDC* compatible, WindowID winid) {
    Release();

    wxMemoryDC* mdc = compatible
        ? new wxMemoryDC(compatible)
        : new wxMemoryDC();
    mdc->DisableAutomaticBoundingBoxUpdates();
    hdc = mdc;
    hdcOwned = true;
    wid = winid;
    if (width < 1) width = 1;
    if (height < 1) height = 1;
    bitmap.CreateWithLogicalSize(width, height, GETWIN(winid)->GetDPIScaleFactor());
    mdc->SelectObject(bitmap);
}

// The code page tells nothing here: the text is UTF-8 whatever it is (STCString)
void SurfaceImpl::SetMode(SurfaceMode WXUNUSED(mode_)) {
}

void SurfaceImpl::Release() noexcept {
    clips.clear();
    if (bitmap.IsOk()) {
        ((wxMemoryDC*)hdc)->SelectObject(wxNullBitmap);
        bitmap.UnRef();
    }
    if (hdcOwned) {
        delete hdc;
        hdc = nullptr;
        hdcOwned = false;
    }
}

// As Scintilla's own GDI surface answers: a wxDC sets single pixels, and no more
int SurfaceImpl::SupportsFeature(Supports feature) noexcept {
    return feature == Supports::PixelModification ? 1 : 0;
}

bool SurfaceImpl::Initialised() {
    return hdc != nullptr;
}


void SurfaceImpl::PenColour(ColourRGBA fore, XYPOSITION width) {
    hdc->SetPen(wxPen(wxColourFromColourRGBA(fore.Opaque()), std::max(1, wxRound(width))));
}

void SurfaceImpl::BrushColour(ColourRGBA back) {
    wxBrush brush;
    int minUsage = INT_MAX;
    size_t minUsageIndex = 0;
    for (size_t i = 0; i < brushCache.size(); i++) {
        auto& entry = brushCache[i];
        if (entry.usage && entry.colour == back) {
            entry.usage++;
            brush = entry.brush;
        } else if (entry.usage < minUsage) {
            minUsage = entry.usage;
            minUsageIndex = i;
        }
    }

    if (!brush.IsOk()) {
        auto& entry = brushCache[minUsageIndex];
        entry.colour = back;
        entry.brush = wxBrush(wxColourFromColourRGBA(back.Opaque()));
        entry.usage = 1;
        brush = entry.brush;
    }

    hdc->SetBrush(brush);
}

void SurfaceImpl::SetFont(const Font *font_) {
    if (font_) {
        hdc->SetFont(FontWX::FromFont(font_));
    }
}

int SurfaceImpl::LogPixelsY() {
    return hdc->GetPPI().y;
}

// A wxDC draws in logical pixels, a whole one each
int SurfaceImpl::PixelDivisions() {
    return 1;
}

int SurfaceImpl::DeviceHeightFont(int points) {
    return points;
}

// SEAM vs wx: Scintilla 5 strokes lines, polylines and frames whole (LineDraw, PolyLine, RectangleFrame) - it moved
// the pen with MoveTo and LineTo; the points are floored as Scintilla's own GDI surface does
void SurfaceImpl::LineDraw(Point start, Point end, Stroke stroke) {
    PenColour(stroke.colour, stroke.width);
    hdc->DrawLine(wxRound(std::floor(start.x)), wxRound(std::floor(start.y)),
                  wxRound(std::floor(end.x)), wxRound(std::floor(end.y)));
}

void SurfaceImpl::PolyLine(const Point *pts, size_t npts, Stroke stroke) {
    if (npts < 2)
        return;
    PenColour(stroke.colour, stroke.width);
    std::vector<wxPoint> p(npts);
    for (size_t i=0; i<npts; i++) {
        p[i].x = wxRound(pts[i].x);
        p[i].y = wxRound(pts[i].y);
    }
    hdc->DrawLines(static_cast<int>(npts), p.data());
}

void SurfaceImpl::Polygon(const Point *pts, size_t npts, FillStroke fillStroke) {
    PenColour(fillStroke.stroke.colour, fillStroke.stroke.width);
    BrushColour(fillStroke.fill.colour);
    std::vector<wxPoint> p(npts);
    for (size_t i=0; i<npts; i++) {
        p[i].x = wxRound(pts[i].x);
        p[i].y = wxRound(pts[i].y);
    }
    hdc->DrawPolygon(static_cast<int>(npts), p.data());
}

void SurfaceImpl::RectangleDraw(PRectangle rc, FillStroke fillStroke) {
    PenColour(fillStroke.stroke.colour, fillStroke.stroke.width);
    BrushColour(fillStroke.fill.colour);
    hdc->DrawRectangle(wxRectFromPRectangle(rc));
}

void SurfaceImpl::RectangleFrame(PRectangle rc, Stroke stroke) {
    PenColour(stroke.colour, stroke.width);
    hdc->SetBrush(*wxTRANSPARENT_BRUSH);
    hdc->DrawRectangle(wxRectFromPRectangle(rc));
}

// A fill short of opaque blends (AlphaRectangle), as Scintilla's own GDI surface does
void SurfaceImpl::FillRectangle(PRectangle rc, Fill fill) {
    if (!fill.colour.IsOpaque()) {
        AlphaRectangle(rc, 0, FillStroke(fill.colour));
        return;
    }
    BrushColour(fill.colour);
    hdc->SetPen(*wxTRANSPARENT_PEN);
    hdc->DrawRectangle(wxRectFromPRectangle(rc));
}

void SurfaceImpl::FillRectangleAligned(PRectangle rc, Fill fill) {
    FillRectangle(PixelAlign(rc, 1), fill);
}

void SurfaceImpl::FillRectangle(PRectangle rc, Surface &surfacePattern) {
    wxBrush br;
    if (static_cast<SurfaceImpl&>(surfacePattern).bitmap.IsOk())
        br = wxBrush(static_cast<SurfaceImpl&>(surfacePattern).bitmap);
    else    // Something is wrong so display in red
        br = wxBrush(*wxRED);
    hdc->SetPen(*wxTRANSPARENT_PEN);
    hdc->SetBrush(br);
    hdc->DrawRectangle(wxRectFromPRectangle(rc));
}

void SurfaceImpl::RoundedRectangle(PRectangle rc, FillStroke fillStroke) {
    PenColour(fillStroke.stroke.colour, fillStroke.stroke.width);
    BrushColour(fillStroke.fill.colour);
    hdc->DrawRoundedRectangle(wxRectFromPRectangle(rc), 4);
}

#ifdef wxHAS_PREMULTIPLIED_ALPHA
#define wxPy_premultiply(p, a)   ((p) * (a) / 0xff)
#else
#define wxPy_premultiply(p, a)   (p)
#endif

void SurfaceImpl::AlphaRectangle(PRectangle rc, XYPOSITION cornerSize,
                                 FillStroke fillStroke) {
#if wxUSE_GRAPHICS_CONTEXT
    wxGCDC dc(wxGraphicsContext::CreateFromUnknownDC(*hdc));
    wxColour penColour(wxColourFromColourRGBA(fillStroke.stroke.colour));
    wxColour brushColour(wxColourFromColourRGBA(fillStroke.fill.colour));
    dc.SetPen(wxPen(penColour, std::max(1, wxRound(fillStroke.stroke.width))));
    dc.SetBrush(wxBrush(brushColour));
    dc.DrawRoundedRectangle(wxRectFromPRectangle(rc), cornerSize);
    return;
#else

#ifdef wxHAS_RAW_BITMAP

    // TODO:  do something with cornerSize
    wxUnusedVar(cornerSize);

    const ColourRGBA fill = fillStroke.fill.colour;
    const ColourRGBA outline = fillStroke.stroke.colour;
    const int alphaFill = fill.GetAlpha();
    const int alphaOutline = outline.GetAlpha();

    wxRect r = wxRectFromPRectangle(rc);
    wxBitmap bmp(r.width, r.height, 32);

    // This block is needed to ensure that the changes done to the bitmap via
    // pixel data object are committed before the bitmap is drawn.
    {
        int px, py;
        wxAlphaPixelData pixData(bmp);

        // Set the fill pixels
        int red   = fill.GetRed();
        int green = fill.GetGreen();
        int blue  = fill.GetBlue();

        wxAlphaPixelData::Iterator p(pixData);
        for (py=0; py<r.height; py++) {
            p.MoveTo(pixData, 0, py);
            for (px=0; px<r.width; px++) {
                p.Red()   = wxPy_premultiply(red,   alphaFill);
                p.Green() = wxPy_premultiply(green, alphaFill);
                p.Blue()  = wxPy_premultiply(blue,  alphaFill);
                p.Alpha() = alphaFill;
                ++p;
            }
        }

        // Set the outline pixels
        red   = outline.GetRed();
        green = outline.GetGreen();
        blue  = outline.GetBlue();
        for (px=0; px<r.width; px++) {
            p.MoveTo(pixData, px, 0);
            p.Red()   = wxPy_premultiply(red,   alphaOutline);
            p.Green() = wxPy_premultiply(green, alphaOutline);
            p.Blue()  = wxPy_premultiply(blue,  alphaOutline);
            p.Alpha() = alphaOutline;
            p.MoveTo(pixData, px, r.height-1);
            p.Red()   = wxPy_premultiply(red,   alphaOutline);
            p.Green() = wxPy_premultiply(green, alphaOutline);
            p.Blue()  = wxPy_premultiply(blue,  alphaOutline);
            p.Alpha() = alphaOutline;
        }

        for (py=0; py<r.height; py++) {
            p.MoveTo(pixData, 0, py);
            p.Red()   = wxPy_premultiply(red,   alphaOutline);
            p.Green() = wxPy_premultiply(green, alphaOutline);
            p.Blue()  = wxPy_premultiply(blue,  alphaOutline);
            p.Alpha() = alphaOutline;
            p.MoveTo(pixData, r.width-1, py);
            p.Red()   = wxPy_premultiply(red,   alphaOutline);
            p.Green() = wxPy_premultiply(green, alphaOutline);
            p.Blue()  = wxPy_premultiply(blue,  alphaOutline);
            p.Alpha() = alphaOutline;
        }
    }

    // Draw the bitmap
    hdc->DrawBitmap(bmp, r.x, r.y, true);

#else
    wxUnusedVar(cornerSize);
    RectangleDraw(rc, fillStroke);
#endif
#endif
}

void SurfaceImpl::GradientRectangle(PRectangle rc,
                                    const std::vector<ColourStop>& stops,
                                    GradientOptions options)
{
#if wxUSE_GRAPHICS_CONTEXT
    wxGraphicsGradientStops gradientStops;
    for ( size_t i = 0; i < stops.size(); ++i )
    {
        if ( i == 0 )
            gradientStops.SetStartColour(wxColourFromColourRGBA(stops[i].colour));
        else if ( i == stops.size() - 1 )
            gradientStops.SetEndColour(wxColourFromColourRGBA(stops[i].colour));
        else
            gradientStops.Add(wxColourFromColourRGBA(stops[i].colour), stops[i].position);
    }

    wxPoint ep;
    switch ( options )
    {
        case GradientOptions::leftToRight:
            ep = wxPoint(rc.right, rc.top);
            break;
        case GradientOptions::topToBottom:
        default:
            ep = wxPoint(rc.left, rc.bottom);
            break;
    }

    wxGCDC dc(wxGraphicsContext::CreateFromUnknownDC(*hdc));
    wxGraphicsContext* gc = dc.GetGraphicsContext();
    gc->SetBrush(gc->CreateLinearGradientBrush(
        double(rc.left), double(rc.top), ep.x, ep.y, gradientStops));
    gc->DrawRectangle(
        double(rc.left), double(rc.top), double(rc.Width()), double(rc.Height()));
#else
    // limited implementation that only uses the first and last stop
    wxDirection dir;
    switch ( options )
    {
        case GradientOptions::leftToRight:
            dir = wxEAST;
            break;
        case GradientOptions::topToBottom:
        default:
            dir = wxSOUTH;
            break;
    }

    hdc->GradientFillLinear(wxRectFromPRectangle(rc),
                            wxColourFromColourRGBA(stops[0].colour),
                            wxColourFromColourRGBA(stops[stops.size() - 1].colour),
                            dir);
#endif
}

#ifdef wxHAS_RAW_BITMAP
wxBitmap BitmapFromRGBAImage(int width, int height, const unsigned char *pixelsImage)
{
    int x, y;
    wxBitmap bmp(width, height, 32);
    wxAlphaPixelData pixData(bmp);

    wxAlphaPixelData::Iterator p(pixData);
    for (y=0; y<height; y++) {
        p.MoveTo(pixData, 0, y);
        for (x=0; x<width; x++) {
            unsigned char red   = *pixelsImage++;
            unsigned char green = *pixelsImage++;
            unsigned char blue  = *pixelsImage++;
            unsigned char alpha = *pixelsImage++;

            p.Red()   = wxPy_premultiply(red,   alpha);
            p.Green() = wxPy_premultiply(green, alpha);
            p.Blue()  = wxPy_premultiply(blue,  alpha);
            p.Alpha() = alpha;
            ++p;
        }
    }
    return bmp;
}
#else
wxBitmap BitmapFromRGBAImage(int width, int height, const unsigned char *pixelsImage)
{
    const int totalPixels = width * height;
    wxScopedArray<unsigned char> data(3*totalPixels);
    wxScopedArray<unsigned char> alpha(totalPixels);
    int curDataLocation = 0, curAlphaLocation = 0, curPixelsImageLocation = 0;

    for ( int i = 0; i < totalPixels; ++i )
    {
        data[curDataLocation++] = pixelsImage[curPixelsImageLocation++];
        data[curDataLocation++] = pixelsImage[curPixelsImageLocation++];
        data[curDataLocation++] = pixelsImage[curPixelsImageLocation++];
        alpha[curAlphaLocation++] = pixelsImage[curPixelsImageLocation++];
    }

    wxImage img(width, height, data.get(), alpha.get(), true);
    wxBitmap bmp(img);

    return bmp;
}
#endif


void SurfaceImpl::DrawRGBAImage(PRectangle rc, int width, int height,
                                const unsigned char *pixelsImage)
{
    wxRect r = wxRectFromPRectangle(rc);
    wxBitmap bmp = BitmapFromRGBAImage(width, height, pixelsImage);
    hdc->DrawBitmap(bmp, r.x, r.y, true);
}


void SurfaceImpl::Ellipse(PRectangle rc, FillStroke fillStroke) {
    PenColour(fillStroke.stroke.colour, fillStroke.stroke.width);
    BrushColour(fillStroke.fill.colour);
    hdc->DrawEllipse(wxRectFromPRectangle(rc));
}

// The rectangle, ends left as they are - as Scintilla's own GDI surface draws it
void SurfaceImpl::Stadium(PRectangle rc, FillStroke fillStroke, Ends WXUNUSED(ends)) {
    RectangleDraw(rc, fillStroke);
}

void SurfaceImpl::Copy(PRectangle rc, Point from, Surface &surfaceSource) {
    wxRect r = wxRectFromPRectangle(rc);
    hdc->Blit(r.x, r.y, r.width, r.height,
              static_cast<SurfaceImpl&>(surfaceSource).hdc,
              wxRound(from.x), wxRound(from.y), wxCOPY);
}

std::unique_ptr<IScreenLineLayout> SurfaceImpl::Layout(const IScreenLine* WXUNUSED(screenLine))
{
    return {};
}

void SurfaceImpl::DrawTextNoClip(PRectangle rc, const Font *font, XYPOSITION ybase,
                                 std::string_view text,
                                 ColourRGBA fore, ColourRGBA back) {
    SetFont(font);
    hdc->SetTextForeground(wxColourFromColourRGBA(fore.Opaque()));
    hdc->SetTextBackground(wxColourFromColourRGBA(back.Opaque()));
    FillRectangle(rc, back.Opaque());

    // ybase is where the baseline should be, but wxWin uses the upper left
    // corner, so I need to calculate the real position for the text...
    hdc->DrawText(STCString(text), wxRound(rc.left), wxRound(ybase - hdc->GetFontMetrics().ascent));
}

// SEAM vs wx: the text's clip is stacked over the one Scintilla set (SetClip, PopClip) - it was set and destroyed,
// taking the outer clip with it
void SurfaceImpl::DrawTextClipped(PRectangle rc, const Font *font, XYPOSITION ybase,
                                  std::string_view text,
                                  ColourRGBA fore, ColourRGBA back) {
    SetClip(rc);
    DrawTextNoClip(rc, font, ybase, text, fore, back);
    PopClip();
}


void SurfaceImpl::DrawTextTransparent(PRectangle rc, const Font *font, XYPOSITION ybase,
                                      std::string_view text,
                                      ColourRGBA fore) {

    SetFont(font);
    hdc->SetTextForeground(wxColourFromColourRGBA(fore.Opaque()));
    hdc->SetBackgroundMode(wxBRUSHSTYLE_TRANSPARENT);

    // ybase is where the baseline should be, but wxWin uses the upper left
    // corner, so I need to calculate the real position for the text...
    hdc->DrawText(STCString(text), wxRound(rc.left), wxRound(ybase - hdc->GetFontMetrics().ascent));

    hdc->SetBackgroundMode(wxBRUSHSTYLE_SOLID);
}


void SurfaceImpl::MeasureWidths(const Font *font, std::string_view text, XYPOSITION *positions) {

    const wxString& str = STCString(text);
    wxArrayInt tpos;

    SetFont(font);

    hdc->GetPartialTextExtents(str, tpos);

    // Map the widths back to the UTF-8 input string
    size_t utf8i = 0;
    for (size_t wxi = 0; wxi < str.size(); ++wxi) {
        wxUniChar c = str[wxi];

#if SIZEOF_WCHAR_T == 2
        // For surrogate pairs, the position for the lead surrogate is garbage
        // and we need to use the position of the trail surrogate for all four bytes
        if (c >= 0xD800 && c < 0xE000 && wxi + 1 < str.size()) {
            ++wxi;
            positions[utf8i++] = tpos[wxi];
            positions[utf8i++] = tpos[wxi];
            positions[utf8i++] = tpos[wxi];
            positions[utf8i++] = tpos[wxi];
            continue;
        }
#endif

        positions[utf8i++] = tpos[wxi];
        if (c >= 0x80)
            positions[utf8i++] = tpos[wxi];
        if (c >= 0x800)
            positions[utf8i++] = tpos[wxi];
        if (c >= 0x10000)
            positions[utf8i++] = tpos[wxi];
    }

    // Bytes the characters did not cover take the last position, as Scintilla's own surfaces leave them
    const XYPOSITION lastPos = utf8i ? positions[utf8i - 1] : 0.0;
    std::fill(positions + utf8i, positions + text.length(), lastPos);
}


XYPOSITION SurfaceImpl::WidthText(const Font *font, std::string_view text) {
    SetFont(font);
    int w;

    hdc->GetTextExtent(STCString(text), &w, nullptr);
    return w;
}

// SEAM vs wx: Scintilla 5 tells the surface the code page (SetMode) and draws its own UTF-8 by the UTF8 twins - the
// twins are the same calls here, the text being UTF-8 whatever the code page (STCString)
void SurfaceImpl::DrawTextNoClipUTF8(PRectangle rc, const Font *font_, XYPOSITION ybase, std::string_view text,
                                     ColourRGBA fore, ColourRGBA back) {
    DrawTextNoClip(rc, font_, ybase, text, fore, back);
}

void SurfaceImpl::DrawTextClippedUTF8(PRectangle rc, const Font *font_, XYPOSITION ybase, std::string_view text,
                                      ColourRGBA fore, ColourRGBA back) {
    DrawTextClipped(rc, font_, ybase, text, fore, back);
}

void SurfaceImpl::DrawTextTransparentUTF8(PRectangle rc, const Font *font_, XYPOSITION ybase, std::string_view text,
                                          ColourRGBA fore) {
    DrawTextTransparent(rc, font_, ybase, text, fore);
}

void SurfaceImpl::MeasureWidthsUTF8(const Font *font_, std::string_view text, XYPOSITION *positions) {
    MeasureWidths(font_, text, positions);
}

XYPOSITION SurfaceImpl::WidthTextUTF8(const Font *font_, std::string_view text) {
    return WidthText(font_, text);
}

#define EXTENT_TEST wxT(" `~!@#$%^&*()-_=+\\|[]{};:\"\'<,>.?/1234567890abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ")

// SEAM vs wx: the ascent and the descent are the DC's font metrics, the ones DrawText* take the baseline by - they
// were the extent of EXTENT_TEST, the ascent cached in the font for DrawText*
XYPOSITION SurfaceImpl::Ascent(const Font *font) {
    SetFont(font);
    return hdc->GetFontMetrics().ascent;
}

XYPOSITION SurfaceImpl::Descent(const Font *font) {
    SetFont(font);
    return hdc->GetFontMetrics().descent;
}

XYPOSITION SurfaceImpl::InternalLeading(const Font *WXUNUSED(font)) {
    return 0;
}

XYPOSITION SurfaceImpl::Height(const Font *font) {
    SetFont(font);
    return hdc->GetCharHeight() + 1;
}

XYPOSITION SurfaceImpl::AverageCharWidth(const Font *font) {
    SetFont(font);
    return hdc->GetCharWidth();
}

void SurfaceImpl::SetClip(PRectangle rc) {
    wxRect clip = wxRectFromPRectangle(rc);
    if (!clips.empty())
        clip.Intersect(clips.back());
    clips.push_back(clip);
    hdc->DestroyClippingRegion();
    hdc->SetClippingRegion(clip);
}

void SurfaceImpl::PopClip() {
    if (clips.empty())
        return;
    clips.pop_back();
    hdc->DestroyClippingRegion();
    if (!clips.empty())
        hdc->SetClippingRegion(clips.back());
}

void SurfaceImpl::FlushCachedState() {
}

void SurfaceImpl::FlushDrawing() {
}

// SEAM vs wx: one surface, on a wxDC, whatever the technology - the DirectWrite one is gone
std::unique_ptr<Surface> Surface::Allocate(Technology WXUNUSED(technology)) {
    return std::make_unique<SurfaceImpl>();
}


//----------------------------------------------------------------------


Window::~Window() noexcept {
}

void Window::Destroy() noexcept {
    if (wid) {
        Show(false);
        GETWIN(wid)->Destroy();
    }
    wid = nullptr;
}


PRectangle Window::GetPosition() const {
    if (! wid) return PRectangle();
    wxRect rc(GETWIN(wid)->GetPosition(), GETWIN(wid)->GetSize());
    return PRectangleFromwxRect(rc);
}

void Window::SetPosition(PRectangle rc) {
    wxRect r = wxRectFromPRectangle(rc);
    GETWIN(wid)->SetSize(r);
}

void Window::SetPositionRelative(PRectangle rc, const Window *relativeTo) {
    wxWindow *relativeWin = GETWIN(relativeTo->wid);

    // SEAM vs wx: the rectangle is in the client area's coordinates (Scintilla's) - inside the engine's border
    wxPoint position = relativeWin->GetScreenPosition() + relativeWin->GetClientAreaOrigin();
    position.x = wxRound(position.x + rc.left);
    position.y = wxRound(position.y + rc.top);

    const wxRect displayRect = wxDisplay(relativeWin).GetClientArea();

    if (position.x < displayRect.GetLeft())
        position.x = displayRect.GetLeft();

    const int width = rc.Width();
    if (width > displayRect.GetWidth())
    {
        // We want to show at least the beginning of the window.
        position.x = displayRect.GetLeft();
    }
    else if (position.x + width > displayRect.GetRight())
        position.x = displayRect.GetRight() - width;

    const int height = rc.Height();
    if (position.y + height > displayRect.GetBottom())
        position.y = displayRect.GetBottom() - height;

    wxWindow *window = GETWIN(wid);
    window->SetSize(position.x, position.y, width, height);
}

PRectangle Window::GetClientPosition() const {
    if (! wid) return PRectangle();
    wxSize sz = GETWIN(wid)->GetClientSize();
    return  PRectangle(0, 0, sz.x, sz.y);
}

void Window::Show(bool show) {
    GETWIN(wid)->Show(show);
}

void Window::InvalidateAll() {
    GETWIN(wid)->Refresh(false);
}

void Window::InvalidateRectangle(PRectangle rc) {
    wxRect r = wxRectFromPRectangle(rc);
    GETWIN(wid)->Refresh(false, &r);
}

void Window::SetCursor(Cursor curs) {
    wxStockCursor cursorId;

    switch (curs) {
    case Cursor::text:
        cursorId = wxCURSOR_IBEAM;
        break;
    case Cursor::arrow:
        cursorId = wxCURSOR_ARROW;
        break;
    case Cursor::up:
        cursorId = wxCURSOR_ARROW; // ** no up arrow...  wxCURSOR_UPARROW;
        break;
    case Cursor::wait:
        cursorId = wxCURSOR_WAIT;
        break;
    case Cursor::horizontal:
        cursorId = wxCURSOR_SIZEWE;
        break;
    case Cursor::vertical:
        cursorId = wxCURSOR_SIZENS;
        break;
    case Cursor::reverseArrow:
        cursorId = wxCURSOR_RIGHT_ARROW;
        break;
    case Cursor::hand:
        cursorId = wxCURSOR_HAND;
        break;
    default:
        cursorId = wxCURSOR_ARROW;
        break;
    }

    if(curs != cursorLast)
    {
        wxCursor wc = wxCursor(cursorId);
        GETWIN(wid)->SetCursor(wc);
        cursorLast = curs;
    }
}


// Returns rectangle of monitor pt is on
PRectangle Window::GetMonitorRect(Point pt) {
    if (! wid) return PRectangle();
    // Get the display the point is found on
    int n = wxDisplay::GetFromPoint(wxPoint(wxRound(pt.x), wxRound(pt.y)));
    wxDisplay dpy(n == wxNOT_FOUND ? 0 : n);
    return PRectangleFromwxRect(dpy.GetGeometry());
}

} // namespace Scintilla::Internal

//----------------------------------------------------------------------
// ibSTCPopupBase and ibSTCPopupWindow

#ifdef __WXOSX_COCOA__

    ibSTCPopupBase::ibSTCPopupBase(wxWindow* parent):wxNonOwnedWindow()
    {
        m_nativeWin = CreateFloatingWindow(this);
        wxNonOwnedWindow::Create(parent, m_nativeWin);
        m_stc = wxDynamicCast(parent, ibStyledTextCtrl);
        m_isShown = false;
        m_cursorSetByPopup = false;
        m_prevCursor = ibSTC_CURSORNORMAL;

        Bind(wxEVT_ENTER_WINDOW, &ibSTCPopupBase::OnMouseEnter, this);
        Bind(wxEVT_LEAVE_WINDOW, &ibSTCPopupBase::OnMouseLeave, this);

        if ( m_stc )
            m_stc->Bind(wxEVT_DESTROY, &ibSTCPopupBase::OnParentDestroy, this);
    }

    ibSTCPopupBase::~ibSTCPopupBase()
    {
        UnsubclassWin();
        CloseFloatingWindow(m_nativeWin);

        if ( m_stc )
        {
            m_stc->Unbind(wxEVT_DESTROY, &ibSTCPopupBase::OnParentDestroy,this);
            RestoreSTCCursor();
        }
    }

    bool ibSTCPopupBase::Show(bool show)
    {
        if ( !wxWindowBase::Show(show) )
            return false;

        if ( show )
        {
            ShowFloatingWindow(m_nativeWin);

            if ( GetRect().Contains(::wxMouseState().GetPosition()) )
                SetSTCCursor(ibSTC_CURSORARROW);
        }
        else
        {
            HideFloatingWindow(m_nativeWin);
            RestoreSTCCursor();
        }

        return true;
    }

    void ibSTCPopupBase::DoSetSize(int x, int y, int width, int ht, int flags)
    {
        wxSize oldSize = GetSize();
        wxNonOwnedWindow::DoSetSize(x, y, width, ht, flags);

        if ( oldSize != GetSize() )
            SendSizeEvent();
    }

    void ibSTCPopupBase::SetSTCCursor(int cursor)
    {
        if ( m_stc )
        {
            m_cursorSetByPopup = true;
            m_prevCursor = m_stc->GetSTCCursor();
            m_stc->SetSTCCursor(cursor);
        }
    }

    void ibSTCPopupBase::RestoreSTCCursor()
    {
        if ( m_stc != nullptr && m_cursorSetByPopup )
            m_stc->SetSTCCursor(m_prevCursor);

        m_cursorSetByPopup = false;
        m_prevCursor = ibSTC_CURSORNORMAL;
    }

    void ibSTCPopupBase::OnMouseEnter(wxMouseEvent& WXUNUSED(event))
    {
        SetSTCCursor(ibSTC_CURSORARROW);
    }

    void ibSTCPopupBase::OnMouseLeave(wxMouseEvent& WXUNUSED(event))
    {
        RestoreSTCCursor();
    }

    void ibSTCPopupBase::OnParentDestroy(wxWindowDestroyEvent& WXUNUSED(event))
    {
        m_stc = nullptr;
    }

#elif wxUSE_POPUPWIN

    ibSTCPopupBase::ibSTCPopupBase(wxWindow* parent)
                   :wxPopupWindow(parent, wxPU_CONTAINS_CONTROLS)
    {
    }

    #ifdef __WXGTK__

        ibSTCPopupBase::~ibSTCPopupBase()
        {
            wxRect rect = GetRect();
            GetParent()->ScreenToClient(&(rect.x), &(rect.y));
            GetParent()->Refresh(false, &rect);
        }

    #elif defined(__WXMSW__)

        // Do not activate the window when it is shown.
        bool ibSTCPopupBase::Show(bool show)
        {
            if ( show )
            {
                // Check if the window is changing from hidden to shown.
                bool changingVisibility = wxWindowBase::Show(true);

                if ( changingVisibility )
                {
                    HWND hWnd = reinterpret_cast<HWND>(GetHandle());
                    if ( GetName() == "ibSTCCallTip" )
                        ::AnimateWindow(hWnd, 25, AW_BLEND);
                    else
                        ::ShowWindow(hWnd, SW_SHOWNA );

                    ::SetWindowPos(hWnd, HWND_NOTOPMOST, 0, 0, 0, 0,
                                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                }

                return changingVisibility;
            }
            else
                return wxPopupWindow::Show(false);
        }

        // Do not activate in response to mouse clicks on this window.
        bool ibSTCPopupBase::MSWHandleMessage(WXLRESULT *res, WXUINT msg,
                                              WXWPARAM wParam, WXLPARAM lParam)
        {
            if ( msg == WM_MOUSEACTIVATE )
            {
                *res = MA_NOACTIVATE;
                return true;
            }
            else
                return wxPopupWindow::MSWHandleMessage(res, msg, wParam,lParam);
        }

    #endif // __WXGTK__

#else

    ibSTCPopupBase::ibSTCPopupBase(wxWindow* parent)
                   :wxFrame(parent, wxID_ANY, wxEmptyString,
                            wxDefaultPosition, wxDefaultSize,
                            wxFRAME_FLOAT_ON_PARENT | wxBORDER_NONE)
    {
        #if defined(__WXGTK__)
            gtk_window_set_accept_focus(GTK_WINDOW(this->GetHandle()), FALSE);
        #endif
    }

    #ifdef __WXMSW__

        // Use ShowWithoutActivating instead of show.
        bool ibSTCPopupBase::Show(bool show)
        {
            if ( show )
            {
                if ( IsShown() )
                    return false;
                else
                {
                    ShowWithoutActivating();
                    return true;
                }
            }
            else
                return wxFrame::Show(false);
        }

        // Do not activate in response to mouse clicks on this window.
        bool ibSTCPopupBase::MSWHandleMessage(WXLRESULT *res, WXUINT msg,
                                              WXWPARAM wParam, WXLPARAM lParam)
        {
            if ( msg == WM_MOUSEACTIVATE )
            {
                *res = MA_NOACTIVATE;
                return true;
            }
            else
                return wxFrame::MSWHandleMessage(res, msg, wParam, lParam);
        }

    #elif !ibSTC_POPUP_IS_CUSTOM

        void ibSTCPopupBase::ActivateParent()
        {
            // Although we're a frame, we always want the parent to be active,
            // so raise it whenever we get shown, focused, etc.
            wxTopLevelWindow *frame = wxDynamicCast(
                wxGetTopLevelParent(GetParent()), wxTopLevelWindow);
            if (frame)
                frame->Raise();
        }

        bool ibSTCPopupBase::Show(bool show)
        {
            bool rv = wxFrame::Show(show);
            if (rv && show)
                ActivateParent();

            #ifdef __WXMAC__
                GetParent()->Refresh(false);
            #endif
        }

    #endif

#endif // __WXOSX_COCOA__

ibSTCPopupWindow::ibSTCPopupWindow(wxWindow* parent)
    : ibSTCPopupBase(parent)
    , m_relPos(wxDefaultPosition)
    , m_absPos(wxDefaultPosition)
{
    #if !ibSTC_POPUP_IS_CUSTOM
        Bind(wxEVT_SET_FOCUS, &ibSTCPopupWindow::OnFocus, this);
    #endif

    m_tlw = wxDynamicCast(wxGetTopLevelParent(parent), wxTopLevelWindow);
    if ( m_tlw )
    {
        m_tlw->Bind(wxEVT_MOVE, &ibSTCPopupWindow::OnParentMove, this);
    #if defined(__WXOSX_COCOA__) || (defined(__WXGTK__)&&!ibSTC_POPUP_IS_FRAME)
        m_tlw->Bind(wxEVT_ICONIZE, &ibSTCPopupWindow::OnIconize, this);
    #endif
    }
}

ibSTCPopupWindow::~ibSTCPopupWindow()
{
    if ( m_tlw )
    {
        m_tlw->Unbind(wxEVT_MOVE, &ibSTCPopupWindow::OnParentMove, this);
    #if defined(__WXOSX_COCOA__) || (defined(__WXGTK__)&&!ibSTC_POPUP_IS_FRAME)
        m_tlw->Unbind(wxEVT_ICONIZE, &ibSTCPopupWindow::OnIconize, this);
    #endif
    }
}

bool ibSTCPopupWindow::Destroy()
{
    #if defined(__WXMAC__) && ibSTC_POPUP_IS_FRAME && !ibSTC_POPUP_IS_CUSTOM
        // The bottom edge of this window is not getting properly
        // refreshed upon deletion, so help it out...
        wxWindow* p = GetParent();
        wxRect r(GetPosition(), GetSize());
        r.SetHeight(r.GetHeight()+1);
        p->Refresh(false, &r);
    #endif

    if ( !wxPendingDelete.Member(this) )
        wxPendingDelete.Append(this);

    return true;
}

bool ibSTCPopupWindow::AcceptsFocus() const
{
    return false;
}

void ibSTCPopupWindow::DoSetSize(int x, int y, int width, int height, int flags)
{
    wxPoint pos(x, y);
    if ( pos.IsFullySpecified() && !m_relPos.IsFullySpecified() )
    {
        m_relPos = GetParent()->ScreenToClient(pos);
    }

    m_absPos = GetParent()->ClientToScreen(m_relPos);

    ibSTCPopupBase::DoSetSize(m_absPos.x, m_absPos.y, width, height, flags);
}

void ibSTCPopupWindow::OnParentMove(wxMoveEvent& event)
{
    SetPosition(m_absPos);
    event.Skip();
}

#if defined(__WXOSX_COCOA__) || (defined(__WXGTK__) && !ibSTC_POPUP_IS_FRAME)

    void ibSTCPopupWindow::OnIconize(wxIconizeEvent& event)
    {
        Show(!event.IsIconized());
    }

#elif !ibSTC_POPUP_IS_CUSTOM

    void ibSTCPopupWindow::OnFocus(wxFocusEvent& event)
    {
        #if ibSTC_POPUP_IS_FRAME
            ActivateParent();
        #endif

        GetParent()->SetFocus();
        event.Skip();
    }

#endif // __WXOSX_COCOA__


//----------------------------------------------------------------------
// Helper classes for ListBox

// The class manages the colours, images, and other data needed for popup lists.
class ibSTCListBoxVisualData
{
public:
    ibSTCListBoxVisualData(int d);
    virtual ~ibSTCListBoxVisualData();

    // ListBoxImpl implementation
    void SetDesiredVisibleRows(int d);
    int  GetDesiredVisibleRows() const;
    void RegisterImage(int type, const wxBitmap& bmp);
    void RegisterImage(int, const char *);
    void RegisterRGBAImage(int, int, int,const unsigned char *);
    void ClearRegisteredImages();

    // Image data
    const wxBitmap* GetImage(int i) const;
    int GetImageAreaWidth() const;
    int GetImageAreaHeight() const;

    // Colour data
    void ComputeColours();
    const wxColour& GetBorderColour() const;
    void SetColours(const wxColour&, const wxColour&,
                    const wxColour&,const wxColour&);
    const wxColour& GetBgColour() const;
    const wxColour& GetTextColour() const;
    const wxColour& GetHighlightBgColour() const;
    const wxColour& GetHighlightTextColour() const;

    // ListCtrl Style
    void UseListCtrlStyle(bool, const wxColour&, const wxColour&);
    bool HasListCtrlAppearance() const;
    const wxColour& GetCurrentBgColour() const;
    const wxColour& GetCurrentTextColour() const;

private:
    using ImgList = std::unordered_map<int, wxBitmap>;

    int      m_desiredVisibleRows;
    ImgList  m_imgList;
    wxSize   m_imgAreaSize;

    wxColour m_borderColour;
    wxColour m_bgColour;
    wxColour m_textColour;
    wxColour m_highlightBgColour;
    wxColour m_highlightTextColour;
    bool     m_useDefaultBgColour;
    bool     m_useDefaultTextColour;
    bool     m_useDefaultHighlightBgColour;
    bool     m_useDefaultHighlightTextColour;

    bool     m_hasListCtrlAppearance;
    wxColour m_currentBgColour;
    wxColour m_currentTextColour;
    bool     m_useDefaultCurrentBgColour;
    bool     m_useDefaultCurrentTextColour;
};

ibSTCListBoxVisualData::ibSTCListBoxVisualData(int d):m_desiredVisibleRows(d),
                        m_useDefaultBgColour(true),
                        m_useDefaultTextColour(true),
                        m_useDefaultHighlightBgColour(true),
                        m_useDefaultHighlightTextColour(true),
                        m_hasListCtrlAppearance(true),
                        m_useDefaultCurrentBgColour(true),
                        m_useDefaultCurrentTextColour(true)
{
    ComputeColours();
}

ibSTCListBoxVisualData::~ibSTCListBoxVisualData()
{
    m_imgList.clear();
}

void ibSTCListBoxVisualData::SetDesiredVisibleRows(int d)
{
    m_desiredVisibleRows=d;
}

int ibSTCListBoxVisualData::GetDesiredVisibleRows() const
{
    return m_desiredVisibleRows;
}

void ibSTCListBoxVisualData::RegisterImage(int type, const wxBitmap& bmp)
{
    if ( !bmp.IsOk() )
        return;

    ImgList::iterator it=m_imgList.find(type);
    bool preExistingWithDifferentSize = false;
    if ( it != m_imgList.end() )
    {
        if ( it->second.GetSize() != bmp.GetSize() )
        {
            preExistingWithDifferentSize = true;
        }

        m_imgList.erase(it);
    }

    m_imgList[type] = bmp;

    if ( preExistingWithDifferentSize )
    {
        m_imgAreaSize.Set(0,0);

        for ( ImgList::iterator imgIt = m_imgList.begin() ;
              imgIt != m_imgList.end() ; ++imgIt )
        {
            m_imgAreaSize.IncTo(it->second.GetSize());
        }
    }
    else
    {
        m_imgAreaSize.IncTo(bmp.GetSize());
    }
}

void ibSTCListBoxVisualData::RegisterImage(int type, const char *xpm_data)
{
    wxXPMDecoder dec;
    wxImage img;

    // This check is borrowed from src/stc/scintilla/src/XPM.cpp.
    // Test done is two parts to avoid possibility of overstepping the memory
    // if memcmp implemented strangely. Must be 4 bytes at least at destination.
    if ( (0 == memcmp(xpm_data, "/* X", 4)) &&
         (0 == memcmp(xpm_data, "/* XPM */", 9)) )
    {
        wxMemoryInputStream stream(xpm_data, strlen(xpm_data)+1);
        img = dec.ReadFile(stream);
    }
    else
        img = dec.ReadData(reinterpret_cast<const char* const*>(xpm_data));

    wxBitmap bmp(img);
    RegisterImage(type, bmp);
}

void ibSTCListBoxVisualData::RegisterRGBAImage(int type, int width, int height,
                                    const unsigned char *pixelsImage)
{
    wxBitmap bmp = BitmapFromRGBAImage(width, height, pixelsImage);
    RegisterImage(type, bmp);
}

void ibSTCListBoxVisualData::ClearRegisteredImages()
{
    m_imgList.clear();
    m_imgAreaSize.Set(0,0);
}

const wxBitmap* ibSTCListBoxVisualData::GetImage(int i) const
{
    ImgList::const_iterator it = m_imgList.find(i);

    if ( it != m_imgList.end() )
        return &(it->second);
    else
        return nullptr;
}

int ibSTCListBoxVisualData::GetImageAreaWidth() const
{
    return m_imgAreaSize.GetWidth();
}

int ibSTCListBoxVisualData::GetImageAreaHeight() const
{
    return m_imgAreaSize.GetHeight();
}

// SEAM vs wx: the theme's colours, not the system's - the paper and the text an input surface's, the selection the
// highlight, the row under the mouse the control's current colour, the frame the dark shadow. wx left the list-control
// look's selection to wxRendererNative (an invalid colour here); there is no native painter, so every colour is one.
void ibSTCListBoxVisualData::ComputeColours()
{
    m_borderColour = wxTHEME_COLOUR(SHADOW_DARK);

    if ( m_useDefaultBgColour )
        m_bgColour = wxTHEME_COLOUR(WINDOW);

    if ( m_useDefaultTextColour )
        m_textColour = wxTHEME_COLOUR(CONTROL_TEXT);

    if ( m_useDefaultHighlightBgColour )
        m_highlightBgColour = wxTHEME_COLOUR(HIGHLIGHT);

    if ( m_useDefaultHighlightTextColour )
        m_highlightTextColour = wxTHEME_COLOUR(HIGHLIGHT_TEXT);

    if ( m_useDefaultCurrentBgColour )
        m_currentBgColour = wxTHEME_COLOUR(CONTROL_CURRENT);

    if ( m_useDefaultCurrentTextColour )
        m_currentTextColour = wxTHEME_COLOUR(CONTROL_TEXT);
}

static void SetColourHelper(bool& isDefault, wxColour& itemColour,
                            const wxColour& newColour)
{
    isDefault = !newColour.IsOk();
    itemColour = newColour;
}

void ibSTCListBoxVisualData::SetColours(const wxColour& bg,
                                        const wxColour& txt,
                                        const wxColour& hlbg,
                                        const wxColour& hltext)
{
    SetColourHelper(m_useDefaultBgColour, m_bgColour, bg);
    SetColourHelper(m_useDefaultTextColour, m_textColour, txt);
    SetColourHelper(m_useDefaultHighlightBgColour, m_highlightBgColour, hlbg);
    SetColourHelper(m_useDefaultHighlightTextColour, m_highlightTextColour,
                    hltext);
    ComputeColours();
}

const wxColour& ibSTCListBoxVisualData::GetBorderColour() const
{
    return m_borderColour;
}

const wxColour& ibSTCListBoxVisualData::GetBgColour() const
{
    return m_bgColour;
}

const wxColour& ibSTCListBoxVisualData::GetTextColour() const
{
    return m_textColour;
}

const wxColour& ibSTCListBoxVisualData::GetHighlightBgColour() const
{
    return m_highlightBgColour;
}

const wxColour& ibSTCListBoxVisualData::GetHighlightTextColour() const
{
    return m_highlightTextColour;
}

void ibSTCListBoxVisualData::UseListCtrlStyle(bool useListCtrlStyle,
                                              const wxColour& curBg,
                                              const wxColour& curText)
{
    m_hasListCtrlAppearance = useListCtrlStyle;
    SetColourHelper(m_useDefaultCurrentBgColour, m_currentBgColour, curBg);
    SetColourHelper(m_useDefaultCurrentTextColour, m_currentTextColour,
                    curText);
    ComputeColours();
}

bool ibSTCListBoxVisualData::HasListCtrlAppearance() const
{
    return m_hasListCtrlAppearance;
}

const wxColour& ibSTCListBoxVisualData::GetCurrentBgColour() const
{
    return m_currentBgColour;
}

const wxColour& ibSTCListBoxVisualData::GetCurrentTextColour() const
{
    return m_currentTextColour;
}

// The class is intended to look like a standard listbox (with an optional
// icon). However, it needs to look like it has focus even when it doesn't.
// SEAM vs wx: the library's virtual list (ibVListBox, the generic wxVListBox forked), drawn in the theme's colours - not
// wx's list wrapped in the system's theme (wxSystemThemedControl)
class ibSTCListBox : public ibVListBox
{
public:
    ibSTCListBox(wxWindow*, ibSTCListBoxVisualData*, int);

    // wxWindow overrides
    virtual bool AcceptsFocus() const override;
    virtual void SetFocus() override;

    // Setters
    void SetContainerBorderSize(int);

    // ListBoxImpl implementation
    virtual void SetListBoxFont(const Font *font);
    void SetAverageCharWidth(int width);
    PRectangle GetDesiredRect() const;
    int CaretFromEdge() const;
    void Clear();
    void Append(char *s, int type = -1);
    int Length() const;
    void Select(int n);
    std::string GetValue(int n) const;
    void SetDelegate(IListBoxDelegate* lbDelegate);
    void SetList(const char* list, char separator, char typesep);

protected:
    // Helpers
    void AppendHelper(const wxString& text, int type);
    void RecalculateItemHeight();
    int TextBoxFromClientEdge() const;
    virtual void OnDrawItemText(wxDC&, const wxRect&,
                                const wxString&, const wxColour&) const;

    // Event handlers
    void OnSelection(wxCommandEvent&);
    void OnDClick(wxCommandEvent&);
    void OnSysColourChanged(wxSysColourChangedEvent& event);
    void OnDPIChanged(wxDPIChangedEvent& event);
    void OnMouseMotion(wxMouseEvent& event);
    void OnMouseLeaveWindow(wxMouseEvent& event);

    // ibVListBox overrides
    virtual wxCoord OnMeasureItem(size_t) const override;
    virtual void OnDrawItem(wxDC& , const wxRect &, size_t) const override;
    virtual void OnDrawBackground(wxDC&, const wxRect&,size_t) const override;

    ibSTCListBoxVisualData* m_visualData;
    wxVector<wxString>      m_labels;
    wxVector<int>           m_imageNos;
    size_t                  m_maxStrWidth;
    int                     m_currentRow;

    IListBoxDelegate*       m_lbDelegate;
    int                     m_aveCharWidth;

    // These drawing parameters are computed or set externally.
    int m_borderSize;
    int m_textHeight;
    int m_itemHeight;
    int m_textTopGap;

    // These drawing parameters are set internally and can be changed if needed
    // to better match the appearance of a list box on a specific platform.
    int m_imagePadding;
    int m_textBoxToTextGap;
    int m_textExtraVerticalPadding;
};

ibSTCListBox::ibSTCListBox(wxWindow* parent, ibSTCListBoxVisualData* v, int ht)
             :ibVListBox(),
              m_visualData(v), m_maxStrWidth(0), m_currentRow(wxNOT_FOUND),
              m_lbDelegate(nullptr),
              m_aveCharWidth(8), m_textHeight(ht), m_itemHeight(ht),
              m_textTopGap(0)
{
    ibVListBox::Create(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                       wxBORDER_NONE, "AutoCompListBox");

    m_imagePadding             = FromDIP(1);
    m_textBoxToTextGap         = FromDIP(3);
    m_textExtraVerticalPadding = FromDIP(1);

    SetBackgroundColour(m_visualData->GetBgColour());

    Bind(wxEVT_LISTBOX, &ibSTCListBox::OnSelection, this);
    Bind(wxEVT_LISTBOX_DCLICK, &ibSTCListBox::OnDClick, this);
    Bind(wxEVT_SYS_COLOUR_CHANGED, &ibSTCListBox::OnSysColourChanged, this);
    Bind(wxEVT_DPI_CHANGED, &ibSTCListBox::OnDPIChanged, this);

    // SEAM vs wx: no system theme to enable and no parent's background for wxRendererNative to take its colours from -
    // the rows are drawn in the theme's (ibSTCListBoxVisualData::ComputeColours)
    if ( m_visualData->HasListCtrlAppearance() )
    {
        Bind(wxEVT_MOTION, &ibSTCListBox::OnMouseMotion, this);
        Bind(wxEVT_LEAVE_WINDOW, &ibSTCListBox::OnMouseLeaveWindow, this);
    }
}

bool ibSTCListBox::AcceptsFocus() const
{
    return false;
}

// Do nothing in response to an attempt to set focus.
void ibSTCListBox::SetFocus()
{
}

void ibSTCListBox::SetContainerBorderSize(int s)
{
    m_borderSize = s;
}

void ibSTCListBox::SetListBoxFont(const Font *font)
{
    SetFont(FontWX::FromFont(font));
    GetTextExtent(EXTENT_TEST, nullptr, &m_textHeight);
    RecalculateItemHeight();
}

void ibSTCListBox::SetAverageCharWidth(int width)
{
    m_aveCharWidth = width;
}

PRectangle ibSTCListBox::GetDesiredRect() const
{
    int maxw = m_maxStrWidth * m_aveCharWidth;
    int maxh ;

    // give it a default if there are no lines, and/or add a bit more
    if ( maxw == 0 )
        maxw = 100;

    maxw += TextBoxFromClientEdge() + m_textBoxToTextGap + m_aveCharWidth * 3;

    // estimate a desired height
    const int count = Length();
    const int desiredVisibleRows = m_visualData->GetDesiredVisibleRows();
    if ( count )
    {
        if ( count <= desiredVisibleRows )
            maxh = count * m_itemHeight;
        else
            maxh = desiredVisibleRows * m_itemHeight;
    }
    else
        maxh = 100;

    // Add space for a scrollbar if needed.
    if ( count > desiredVisibleRows )
        maxw += GetScrollbarSize(wxVERTICAL);

    // Add borders.
    maxw += 2 * m_borderSize;
    maxh += 2 * m_borderSize;

    PRectangle rc;
    rc.top = 0;
    rc.left = 0;
    rc.right = maxw;
    rc.bottom = maxh;
    return rc;
}

int ibSTCListBox::CaretFromEdge() const
{
    return m_borderSize + TextBoxFromClientEdge() + m_textBoxToTextGap;
}

void ibSTCListBox::Clear()
{
    m_labels.clear();
    m_imageNos.clear();
}

void ibSTCListBox::Append(char *s, int type)
{
    AppendHelper(wxString::FromUTF8(s), type);
    RecalculateItemHeight();
}

int ibSTCListBox::Length() const
{
    return GetItemCount();
}

void ibSTCListBox::Select(int n)
{
    SetSelection(n);

    wxCommandEvent e;
    OnSelection(e);
}

std::string ibSTCListBox::GetValue(int n) const
{
    return std::string(m_labels[n].utf8_str());
}

void ibSTCListBox::SetDelegate(IListBoxDelegate* lbDelegate)
{
    m_lbDelegate = lbDelegate;
}

void ibSTCListBox::SetList(const char* list, char separator, char typesep)
{
    wxWindowUpdateLocker noUpdates(this);
    Clear();
    wxStringTokenizer tkzr(wxString::FromUTF8(list), (wxChar)separator);
    while ( tkzr.HasMoreTokens() ) {
        wxString token = tkzr.GetNextToken();
        long type = -1;
        int pos = token.Find(typesep);
        if (pos != -1) {
            token.Mid(pos+1).ToLong(&type);
            token.Truncate(pos);
        }
        AppendHelper(token, (int)type);
    }

    RecalculateItemHeight();
}

void ibSTCListBox::AppendHelper(const wxString& text, int type)
{
    m_maxStrWidth = wxMax(m_maxStrWidth, text.length());
    m_labels.push_back(text);
    m_imageNos.push_back(type);
    SetItemCount(m_labels.size());
}

void ibSTCListBox::RecalculateItemHeight()
{
    m_itemHeight = wxMax(m_textHeight + 2 * m_textExtraVerticalPadding,
                       m_visualData->GetImageAreaHeight() + 2 * m_imagePadding);
    m_textTopGap = (m_itemHeight - m_textHeight)/2;
}

int ibSTCListBox::TextBoxFromClientEdge() const
{
    const int width = m_visualData->GetImageAreaWidth();
    return (width == 0 ? 0 : width + 2 * m_imagePadding);
}

void ibSTCListBox::OnSelection(wxCommandEvent& WXUNUSED(event))
{
    if ( m_lbDelegate )
    {
        ListBoxEvent lbe(ListBoxEvent::EventType::selectionChange);
        m_lbDelegate->ListNotify(&lbe);
    }
}

void ibSTCListBox::OnDClick(wxCommandEvent& WXUNUSED(event))
{
    if ( m_lbDelegate )
    {
        ListBoxEvent lbe(ListBoxEvent::EventType::doubleClick);
        m_lbDelegate->ListNotify(&lbe);
    }
}

void ibSTCListBox::OnSysColourChanged(wxSysColourChangedEvent& WXUNUSED(event))
{
    m_visualData->ComputeColours();
    GetParent()->SetOwnBackgroundColour(m_visualData->GetBgColour());
    SetBackgroundColour(m_visualData->GetBgColour());
    GetParent()->Refresh();
}

void ibSTCListBox::OnDPIChanged(wxDPIChangedEvent& event)
{
    m_imagePadding = FromDIP(1);
    m_textBoxToTextGap = FromDIP(3);
    m_textExtraVerticalPadding = FromDIP(1);

    GetTextExtent(EXTENT_TEST, nullptr, &m_textHeight);

    RecalculateItemHeight();

    event.Skip();
}

void ibSTCListBox::OnMouseLeaveWindow(wxMouseEvent& event)
{
    const int old = m_currentRow;
    m_currentRow = wxNOT_FOUND;

    if ( old != wxNOT_FOUND )
        RefreshRow(old);

    event.Skip();
}

void ibSTCListBox::OnMouseMotion(wxMouseEvent& event)
{
    const int old = m_currentRow;
    m_currentRow = VirtualHitTest(event.GetY());

    if ( old != m_currentRow )
    {
        if( m_currentRow != wxNOT_FOUND )
            RefreshRow(m_currentRow);

        if( old != wxNOT_FOUND )
            RefreshRow(old);
    }

    event.Skip();
}

wxCoord ibSTCListBox::OnMeasureItem(size_t WXUNUSED(n)) const
{
    return static_cast<wxCoord>(m_itemHeight);
}

// This control will be drawn so that a typical row of pixels looks like:
//
//    +++++++++++++++++++++++++   =====ITEM TEXT================
//  |         |                 |    |
//  |       imageAreaWidth      |    |
//  |                           |    |
// m_imagePadding               |   m_textBoxToTextGap
//                              |
//                   m_imagePadding
//
//
// m_imagePadding            : Used to give a little extra space between the
//                             client edge and an item's bitmap.
// imageAreaWidth            : Computed as the width of the largest registered
//                             bitmap (part of ibSTCListBoxVisualData).
// m_textBoxToTextGap        : Used so that item text does not begin immediately
//                             at the edge of the highlight box.
//
// Images are drawn centered in the image area.
// If a selection rectangle is drawn, its left edge is at x=0 if there are
// no bitmaps. Otherwise
//       x = m_imagePadding + m_imageAreaWidth + m_imagePadding.
// Text is drawn at x + m_textBoxToTextGap and centered vertically.

void ibSTCListBox::OnDrawItemText(wxDC& dc, const wxRect& rect,
                                  const wxString& label,
                                  const wxColour& textCol) const
{
    wxDCTextColourChanger tcc(dc, textCol);

    wxString ellipsizedlabel = wxControl::Ellipsize(label, dc, wxELLIPSIZE_END,
                                                    rect.GetWidth());
    dc.DrawText(ellipsizedlabel, rect.GetLeft(), rect.GetTop());
}

void ibSTCListBox::OnDrawItem(wxDC& dc, const wxRect& rect, size_t n) const
{
    wxString label;
    int imageNo = -1;
    if ( n < m_labels.size() )
    {
        label   = m_labels[n];
        imageNo = m_imageNos[n];
    }

    int topGap = m_textTopGap;
    int leftGap = TextBoxFromClientEdge() + m_textBoxToTextGap;

    wxColour textCol;

    if ( IsSelected(n) )
        textCol = m_visualData->GetHighlightTextColour();
    else if ( static_cast<int>(n) == m_currentRow )
        textCol = m_visualData->GetCurrentTextColour();
    else
        textCol = m_visualData->GetTextColour();

    wxRect rect2(rect.GetLeft() + leftGap, rect.GetTop() + topGap,
                 rect.GetWidth() - leftGap, m_textHeight);

    OnDrawItemText(dc, rect2, label, textCol);

    const wxBitmap* b = m_visualData->GetImage(imageNo);
    if ( b )
    {
        const int width = m_visualData->GetImageAreaWidth();
        topGap = (m_itemHeight - b->GetHeight())/2;
        leftGap = m_imagePadding + (width - b->GetWidth())/2;
        dc.DrawBitmap(*b, rect.GetLeft()+leftGap, rect.GetTop()+topGap, true);
    }
}

void ibSTCListBox::OnDrawBackground(wxDC &dc, const wxRect &rect,size_t n) const
{
    if ( IsSelected(n) )
    {
        wxRect selectionRect(rect);
        const wxColour& highlightBgColour =m_visualData->GetHighlightBgColour();

        #ifdef __WXMSW__
            if ( !m_visualData->HasListCtrlAppearance() )
            {
                // On windows the selection rectangle in Scintilla's
                // autocompletion list only covers the text and not the icon.

                const int textBoxFromClientEdge = TextBoxFromClientEdge();
                selectionRect.SetLeft(rect.GetLeft() + textBoxFromClientEdge);
                selectionRect.SetWidth(rect.GetWidth() - textBoxFromClientEdge);
            }
        #endif // __WXMSW__

        // SEAM vs wx: the colour is always one (ComputeColours) - no wxRendererNative pad; the classic look's focus
        // rectangle is the theme renderer's
        wxDCBrushChanger bc(dc, highlightBgColour);
        wxDCPenChanger   pc(dc, highlightBgColour);
        dc.DrawRectangle(selectionRect);

        if ( !m_visualData->HasListCtrlAppearance() )
            GetRenderer()->DrawFocusRect(const_cast<ibSTCListBox*>(this), dc, selectionRect);
    }
    else if ( static_cast<int>(n) == m_currentRow )
    {
        const wxColour& currentBgColour = m_visualData->GetCurrentBgColour();

        wxDCBrushChanger bc(dc, currentBgColour);
        wxDCPenChanger   pc(dc, currentBgColour);
        dc.DrawRectangle(rect);
    }
}


// A popup window to place the ibSTCListBox upon
class ibSTCListBoxWin : public ibSTCPopupWindow
{
public:
    ibSTCListBoxWin(wxWindow*, ibSTCListBox**, ibSTCListBoxVisualData*,
                    int);

protected:
    void OnPaint(wxPaintEvent&);

private:
    ibSTCListBoxVisualData* m_visualData;
};

ibSTCListBoxWin::ibSTCListBoxWin(wxWindow* parent, ibSTCListBox** lb,
                                 ibSTCListBoxVisualData* v, int h)
                :ibSTCPopupWindow(parent)
{
    // SEAM vs wx: one list, drawn through the wxDC - the DirectWrite one went with its surface
    *lb = new ibSTCListBox(this, v, h);

    // Use the background of this window to form a frame around the listbox
    // except on macos where the native Scintilla popup has no frame.
#ifdef __WXOSX_COCOA__
    const int borderThickness = 0;
#else
    const int borderThickness = FromDIP(1);
#endif
    wxBoxSizer* bSizer = new wxBoxSizer(wxVERTICAL);
    bSizer->Add(*lb, 1, wxEXPAND|wxALL, borderThickness);
    SetSizer(bSizer);
    (*lb)->SetContainerBorderSize(borderThickness);

    // The frame round the list is this window, painted in the border colour.
    m_visualData = v;
    Bind(wxEVT_PAINT, &ibSTCListBoxWin::OnPaint, this);
    SetBackgroundStyle(wxBG_STYLE_PAINT);
}

void ibSTCListBoxWin::OnPaint(wxPaintEvent& WXUNUSED(evt))
{
    wxPaintDC dc(this);
    dc.SetBackground(m_visualData->GetBorderColour());
    dc.Clear();
}


//----------------------------------------------------------------------

ListBoxImpl::ListBoxImpl()
            :m_listBox(nullptr), m_visualData(new ibSTCListBoxVisualData(5))
{
}

ListBoxImpl::~ListBoxImpl() noexcept {
    delete m_visualData;
}


void ListBoxImpl::SetFont(const Font *font) {
    m_listBox->SetListBoxFont(font);
}


void ListBoxImpl::Create(Window &parent, int WXUNUSED(ctrlID),
                         Point WXUNUSED(location_), int lineHeight_,
                         bool WXUNUSED(unicodeMode_), Technology WXUNUSED(technology_)) {
    wid = new ibSTCListBoxWin(GETWIN(parent.GetID()), &m_listBox, m_visualData,
                              lineHeight_);
}


void ListBoxImpl::SetAverageCharWidth(int width) {
    m_listBox->SetAverageCharWidth(width);
}


void ListBoxImpl::SetVisibleRows(int rows) {
    m_visualData->SetDesiredVisibleRows(rows);
}


int ListBoxImpl::GetVisibleRows() const {
    return m_visualData->GetDesiredVisibleRows();
}

PRectangle ListBoxImpl::GetDesiredRect() {
    return m_listBox->GetDesiredRect();
}


int ListBoxImpl::CaretFromEdge() {
    return m_listBox->CaretFromEdge();
}


void ListBoxImpl::Clear() noexcept {
    m_listBox->Clear();
}


void ListBoxImpl::Append(char *s, int type) {
    m_listBox->Append(s, type);
}


void ListBoxImpl::SetList(const char* list, char separator, char typesep) {
    m_listBox->SetList(list, separator, typesep);
}


int ListBoxImpl::Length() {
    return m_listBox->Length();
}


void ListBoxImpl::Select(int n) {
    m_listBox->Select(n);
}


int ListBoxImpl::GetSelection() {
    return m_listBox->GetSelection();
}


int ListBoxImpl::Find(const char *WXUNUSED(prefix)) {
    // No longer used
    return wxNOT_FOUND;
}


std::string ListBoxImpl::GetValue(int n) {
    return m_listBox->GetValue(n);
}


void ListBoxImpl::RegisterImage(int type, const char *xpm_data) {
    m_visualData->RegisterImage(type, xpm_data);
}


void ListBoxImpl::RegisterRGBAImage(int type, int width, int height,
                                    const unsigned char *pixelsImage)
{
    m_visualData->RegisterRGBAImage(type, width, height, pixelsImage);
}


void ListBoxImpl::ClearRegisteredImages() {
    m_visualData->ClearRegisteredImages();
}


void ListBoxImpl::SetDelegate(IListBoxDelegate* lbDelegate) {
    m_listBox->SetDelegate(lbDelegate);
}


// SEAM vs wx: Scintilla 5 hands the list its colours (the SC_ELEMENT_LIST* elements) before it is created - a colour
// it leaves unset is the system's, as SetColours takes wxNullColour
void ListBoxImpl::SetOptions(ListOptions options) {
    const auto colour = [](const std::optional<ColourRGBA>& c) {
        return c ? wxColourFromColourRGBA(*c) : wxNullColour;
    };
    m_visualData->SetColours(colour(options.back), colour(options.fore),
                             colour(options.backSelected), colour(options.foreSelected));
}


void ListBoxImpl::RegisterImageHelper(int type, const wxBitmap& bmp)
{
    m_visualData->RegisterImage(type, bmp);
}


ListBox::ListBox() noexcept {
}

ListBox::~ListBox() noexcept {
}

std::unique_ptr<ListBox> ListBox::Allocate() {
    return std::make_unique<ListBoxImpl>();
}

//----------------------------------------------------------------------

Menu::Menu() noexcept : mid(nullptr) {
}

// SEAM vs wx: the popup is the engine's themed menu, ibMenu shown by ibWindow::PopupMenu - its items' commands reach
// the editor as the native menu's did (ibStyledTextCtrl::OnMenu)
void Menu::CreatePopUp() {
    Destroy();
    mid = new ibMenu();
}

void Menu::Destroy() noexcept {
    if (mid)
        delete (ibMenu*)mid;
    mid = nullptr;
}

// SEAM vs wx: the point is the client area's (Scintilla's), the popup takes the window's - moved by its origin
void Menu::Show(Point pt, const Window &w) {
    ibWindow* const win = wxStaticCast(GETWIN(w.GetID()), ibWindow);
    const wxPoint origin = win->GetClientAreaOrigin();
    win->PopupMenu((ibMenu*)mid, wxRound(pt.x - 4) + origin.x, wxRound(pt.y) + origin.y);
    Destroy();
}

//----------------------------------------------------------------------

// SEAM vs wx: the chrome (the fold margin's) is the theme's - the control's face and its light edge, not the system's
ColourRGBA Platform::Chrome() {
    wxColour c;
    c = wxTHEME_COLOUR(CONTROL);
    return ColourRGBA(c.Red(), c.Green(), c.Blue());
}

ColourRGBA Platform::ChromeHighlight() {
    wxColour c;
    c = wxTHEME_COLOUR(SHADOW_HIGHLIGHT);
    return ColourRGBA(c.Red(), c.Green(), c.Blue());
}

const char *Platform::DefaultFont() {
    static char buf[128];
    wxStrlcpy(buf, wxNORMAL_FONT->GetFaceName().mbc_str(), WXSIZEOF(buf));
    return buf;
}

int Platform::DefaultFontSize() {
    return wxNORMAL_FONT->GetPointSize();
}

unsigned int Platform::DoubleClickTime() {
    return 500;   // **** ::GetDoubleClickTime();
}


//#define TRACE

void Platform::DebugDisplay(const char *s) noexcept {
#ifdef TRACE
    wxLogDebug(wxString::FromUTF8(s));
#else
    wxUnusedVar(s);
#endif
}

void Platform::DebugPrintf(const char *format, ...) noexcept {
#ifdef TRACE
    char buffer[2000];
    va_list pArguments;
    va_start(pArguments, format);
    vsprintf(buffer,format,pArguments);
    va_end(pArguments);
    Platform::DebugDisplay(buffer);
#else
    wxUnusedVar(format);
#endif
}


static bool assertionPopUps = true;

bool Platform::ShowAssertionPopUps(bool assertionPopUps_) noexcept {
    bool ret = assertionPopUps;
    assertionPopUps = assertionPopUps_;
    return ret;
}

void Platform::Assert(const char *c, const char *file, int line) noexcept {
#ifdef TRACE
    char buffer[2000];
    sprintf(buffer, "Assertion [%s] failed at %s %d", c, file, line);
    if (assertionPopUps) {
        /*int idButton = */
        wxMessageBox(wxString::FromUTF8(buffer),
                     wxT("Assertion failure"),
                     wxICON_HAND | wxOK);
    } else {
        strcat(buffer, "\r\n");
        Platform::DebugDisplay(buffer);
        abort();
    }
#else
    wxUnusedVar(c);
    wxUnusedVar(file);
    wxUnusedVar(line);
#endif
}


#endif // wxUSE_STC
