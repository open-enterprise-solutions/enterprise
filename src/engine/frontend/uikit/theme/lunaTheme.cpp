// ----------------------------------------------------------------------------
// Luna theme — the default uikit theme, built the way every univ theme is:
// an ibThemeRenderer subclass + an ibColourScheme carrying the interior-design
// palette from docs/ui-palette.md (cool dusty-blue chrome, warm-cream
// content, terracotta accent). Registered as the default theme "luna".
// ----------------------------------------------------------------------------

#include <wx/dc.h>
#include <wx/dcmemory.h>
#include <wx/dcgraph.h>
#include <wx/settings.h>
#include <wx/window.h>
#include <wx/artprov.h>

#include "frontend/uikit/theme.h"
#include "frontend/uikit/renderer.h"
#include "frontend/uikit/colourScheme.h"
#include "frontend/uikit/inputConsumer.h"
#include "frontend/uikit/window.h"
#include "frontend/uikit/theme/themeRenderer.h"
#include "frontend/uikit/menu.h"
#include "frontend/uikit/menuItem.h"
#include "frontend/uikit/ctrl/textCtrl.h"
#include "frontend/uikit/ctrl/listBox.h"

namespace {

// palette tiers (docs/ui-palette.md — reuse a tier, don't invent a shade)
const wxColour kChrome(0xB8, 0xC9, 0xD4);        // chrome        #B8C9D4
const wxColour kChromeLight(0xC8, 0xD6, 0xDF);   // chrome-light  #C8D6DF
const wxColour kContent(0xFA, 0xF7, 0xF0);       // content       #FAF7F0
const wxColour kBorder(0xA8, 0xBA, 0xC8);        // border        #A8BAC8
const wxColour kCaptionTop(0x5A, 0x7B, 0x95);    // caption-active-top
const wxColour kCaptionBottom(0x3F, 0x5C, 0x77); // caption-active-bottom
const wxColour kAccent(0xD9, 0x77, 0x57);        // accent        #D97757
const wxColour kAccentDeep(0xB8, 0x5A, 0x38);    // accent-deep   #B85A38

// menu metrics (FORKED from the wxWin32Renderer menu block — the only canon
// implementation of the univ menu geometry contract; margins widened from
// the Win95 values 9/18/3 — modern menus breathe)
const wxCoord MENU_LEFT_MARGIN = 22;
const wxCoord MENU_RIGHT_MARGIN = 24;
const wxCoord MENU_VERT_MARGIN = 5;

// the margin around bitmap/check marks (on each side)
const wxCoord MENU_BMP_MARGIN = 2;

// the margin between the labels and accel strings
const wxCoord MENU_ACCEL_MARGIN = 16;

// the separator height in pixels: in fact, strangely enough, the real height
// is 2 but Windows adds one extra pixel in the bottom margin, so take it into
// account here
const wxCoord MENU_SEPARATOR_HEIGHT = 3;

} // anonymous namespace

// ----------------------------------------------------------------------------
// ibLunaMenuGeometryInfo (FORKED from wxWin32MenuGeometryInfo)
// ----------------------------------------------------------------------------

class ibLunaMenuGeometryInfo : public ibMenuGeometryInfo {
public:
	virtual wxSize GetSize() const override { return m_size; }

	wxCoord GetLabelOffset() const { return m_ofsLabel; }
	wxCoord GetAccelOffset() const { return m_ofsAccel; }

	wxCoord GetItemHeight() const { return m_heightItem; }

private:
	// the total size of the menu
	wxSize m_size;

	// the offset of the start of the menu item label
	wxCoord m_ofsLabel;

	// the offset of the start of the accel label
	wxCoord m_ofsAccel;

	// the height of a normal (not separator) item
	wxCoord m_heightItem;

	friend class ibLunaRenderer;
};

// ----------------------------------------------------------------------------
// ibLunaColourScheme
// ----------------------------------------------------------------------------

class ibLunaColourScheme : public ibColourScheme {
public:
	virtual wxColour Get(StdColour col) const override;
	virtual wxColour GetBackground(ibWindow* win) const override;
};

wxColour ibLunaColourScheme::Get(ibLunaColourScheme::StdColour col) const {
	switch (col) {
		case WINDOW:                      return kContent;
		case CONTROL:                     return kChromeLight;
		case CONTROL_PRESSED:             return kChromeLight.ChangeLightness(92);
		case CONTROL_CURRENT:             return kChromeLight.ChangeLightness(106);
		case CONTROL_TEXT:                return *wxBLACK;
		case CONTROL_TEXT_DISABLED:       return kBorder;
		case CONTROL_TEXT_DISABLED_SHADOW:return kChromeLight;
		case SCROLLBAR:                   return kChromeLight;
		case SCROLLBAR_PRESSED:           return kBorder;
		case HIGHLIGHT:                   return kAccent;
		case HIGHLIGHT_TEXT:              return *wxWHITE;
		case SHADOW_DARK:                 return kCaptionBottom;
		case SHADOW_HIGHLIGHT:            return *wxWHITE;
		case SHADOW_IN:                   return kBorder;
		case SHADOW_OUT:                  return kChromeLight.ChangeLightness(90);
		case TITLEBAR:                    return kChromeLight;
		case TITLEBAR_ACTIVE:             return kCaptionTop;
		case TITLEBAR_TEXT:               return kCaptionBottom;
		case TITLEBAR_ACTIVE_TEXT:        return *wxWHITE;
		case GAUGE:                       return kAccent;
		case DESKTOP:                     return kChrome;
		case FRAME:                       return kChrome;
		case MAX:                         break;
	}
	wxFAIL_MSG(wxT("invalid standard colour"));
	return *wxBLACK;
}

wxColour ibLunaColourScheme::GetBackground(ibWindow* win) const {
	// a window with an explicitly set background keeps it; otherwise blend
	// with the parent so label-like controls sit naturally on any surface
	if (win->UseBgCol()) {
		// the theme engine adapts the custom colour to the control state,
		// so a branded control hovers/presses like a themed one
		wxColour col = win->GetBackgroundColour();
		const int flags = win->GetStateFlags();
		if (flags & wxCONTROL_PRESSED)
			return col.ChangeLightness(92);
		if ((flags & wxCONTROL_CURRENT) && win->CanBeHighlighted())
			return col.ChangeLightness(106);
		return col;
	}

	// input surfaces (text fields, list boxes — the win32 canon classified
	// the same way) live on the content colour, not on the chrome
	if (dynamic_cast<ibTextCtrl*>(win) != nullptr ||
			dynamic_cast<ibListBox*>(win) != nullptr)
		return Get(WINDOW);

	// highlightable controls (buttons and friends) take the control colour
	// modulated by state — pressed must be UNMISTAKABLE (a toggle button
	// stays pressed, the old chrome-8% shade was nearly invisible)
	if (win->CanBeHighlighted()) {
		const int flags = win->GetStateFlags();
		if (flags & wxCONTROL_PRESSED)
			return kAccent.ChangeLightness(172);
		if (flags & wxCONTROL_CURRENT)
			return Get(CONTROL_CURRENT);
		return Get(CONTROL);
	}

	// label-like statics blend with the parent surface
	wxWindow* parent = win->GetParent();
	if (parent != nullptr)
		return parent->GetBackgroundColour();
	return Get(WINDOW);
}

// ----------------------------------------------------------------------------
// ibLunaRenderer: the standard renderer with Luna indicator bitmaps
// ----------------------------------------------------------------------------

class ibLunaRenderer : public ibThemeRenderer {
public:
	ibLunaRenderer(const ibColourScheme* scheme) : ibThemeRenderer(scheme) {}

	virtual wxBitmap GetRadioBitmap(int flags) override;
	virtual wxBitmap GetCheckBitmap(int flags) override;
	virtual wxBitmap GetFrameButtonBitmap(FrameButtonType type) override;

	// the default (primary) button is the palette accent — the "ottoman":
	// flat terracotta face, deep border, white label
	virtual void DrawButtonSurface(wxDC& dc, const wxColour& col,
		const wxRect& rect, int flags) override {
		if (flags & wxCONTROL_ISDEFAULT) {
			wxColour face = kAccent;
			if (flags & wxCONTROL_PRESSED)
				face = kAccentDeep;
			else if (flags & wxCONTROL_CURRENT)
				face = kAccent.ChangeLightness(106);
			dc.SetPen(*wxTRANSPARENT_PEN);
			dc.SetBrush(wxBrush(face));
			dc.DrawRectangle(rect);
			return;
		}
		ibThemeRenderer::DrawButtonSurface(dc, col, rect, flags);
	}

	virtual void DrawButtonBorder(wxDC& dc, const wxRect& rect,
		int flags = 0, wxRect* rectIn = nullptr) override {
		wxColour border = m_scheme->Get(ibColourScheme::SHADOW_IN);
		if (flags & (wxCONTROL_ISDEFAULT | wxCONTROL_FOCUSED))
			border = kAccentDeep;
		dc.SetPen(wxPen(border));
		dc.SetBrush(*wxTRANSPARENT_BRUSH);
		dc.DrawRectangle(rect);
		if (rectIn != nullptr) {
			// reserve exactly the 1px line drawn above — a wider inset read
			// as a fat border around the label
			*rectIn = rect;
			rectIn->Deflate(1);
		}
	}

	virtual void DrawButtonLabel(wxDC& dc, const wxString& label,
		const wxBitmap& image, const wxRect& rect, int flags = 0,
		int alignment = wxALIGN_LEFT | wxALIGN_TOP, int indexAccel = -1,
		wxRect* rectBounds = nullptr) override {
		if ((flags & wxCONTROL_ISDEFAULT) && !(flags & wxCONTROL_DISABLED))
			dc.SetTextForeground(*wxWHITE);

		// the accent border already signals focus — the canon's dotted
		// focus rect on top of it read as a second fat border
		flags &= ~wxCONTROL_FOCUSED;

		ibThemeRenderer::DrawButtonLabel(dc, label, image, rect, flags,
			alignment, indexAccel, rectBounds);
	}

	// the scrollbars live INSIDE the control frame (the std default of
	// "outside" left the bar hanging next to the border like a separate
	// strip of its own)
	virtual bool AreScrollbarsInsideBorder() const override { return true; }

	// text field padding: the canon reserved just the 1px border around the
	// text line, visually clipping font descenders — give the line breathing
	// room (Total and Client MUST stay exact inverses: best-size goes through
	// the former, layout through the latter)
	virtual wxRect GetTextTotalArea(const ibTextCtrl* text,
		const wxRect& rect) const override {
		wxRect total = rect;
		total.Inflate(GetTextBorderWidth(text) + 3, GetTextBorderWidth(text) + 1);
		return total;
	}

	virtual wxRect GetTextClientArea(const ibTextCtrl* text, const wxRect& rect,
		wxCoord* extraSpaceBeyond) const override {
		wxRect client = rect;
		client.Deflate(GetTextBorderWidth(text) + 3, GetTextBorderWidth(text) + 1);

		if (extraSpaceBeyond != nullptr)
			*extraSpaceBeyond = 0;

		return client;
	}

	// a focused or default control gets the accent border — the activation
	// highlight; wxBORDER_SIMPLE is intercepted too: the base draws it with
	// the BLACK pen, the only non-palette border left
	virtual void DrawBorder(wxDC& dc, wxBorder border, const wxRect& rectTotal,
		int flags = 0, wxRect* rectIn = nullptr) override {
		const bool accent =
			(flags & (wxCONTROL_FOCUSED | wxCONTROL_ISDEFAULT)) != 0;
		if (border != wxBORDER_NONE &&
				(accent || border == wxBORDER_SIMPLE)) {
			wxRect rect = rectTotal;
			DrawRect(dc, &rect, wxPen(accent ? kAccentDeep : kBorder));
			if (rectIn != nullptr)
				*rectIn = rect;
			return;
		}
		ibThemeRenderer::DrawBorder(dc, border, rectTotal, flags, rectIn);
	}

	// flat borders: the ibThemeRenderer base shades every border the Win95
	// way (two-tone 3-D bevels) — Luna replaces all of them with a single
	// 1px palette-border line, the modern flat look
	virtual void DrawRaisedBorder(wxDC& dc, wxRect* rect) override
		{ DrawFlatBorder(dc, rect); }
	virtual void DrawSunkenBorder(wxDC& dc, wxRect* rect) override
		{ DrawFlatBorder(dc, rect); }
	virtual void DrawAntiSunkenBorder(wxDC& dc, wxRect* rect) override
		{ DrawFlatBorder(dc, rect); }
	virtual void DrawBoxBorder(wxDC& dc, wxRect* rect) override
		{ DrawFlatBorder(dc, rect); }
	virtual void DrawStaticBorder(wxDC& dc, wxRect* rect) override
		{ DrawFlatBorder(dc, rect); }
	virtual void DrawExtraBorder(wxDC& WXUNUSED(dc), wxRect* WXUNUSED(rect)) override
		{ /* no extra bevel layer in the flat look */ }

	void DrawFlatBorder(wxDC& dc, wxRect* rect)
	{
		DrawRect(dc, rect, wxPen(kBorder));
	}

	// the methods ibThemeRenderer leaves to the concrete theme — flat Luna
	// style; controls not revived yet get honest minimal implementations
	// to be refined when their control comes alive

	virtual void DrawArrow(wxDC& dc, wxDirection dir, const wxRect& rect,
		int flags = 0) override {
		const wxColour col = (flags & wxCONTROL_DISABLED)
			? m_scheme->Get(ibColourScheme::CONTROL_TEXT_DISABLED)
			: m_scheme->Get(ibColourScheme::CONTROL_TEXT);

		// float coordinates + AA: spin/scroll arrows are tiny (~8x6 px) and
		// the integer triangle came out lopsided — the /3 and /2 roundings
		// land differently on each vertex
		const double cx = rect.x + rect.width / 2.0;
		const double cy = rect.y + rect.height / 2.0;

		// isosceles triangle: base 0.7 of the cross size, height 0.4 of the
		// along size, but never smaller than 3px
		const double halfBase = wxMax(1.5,
			((dir == wxUP || dir == wxDOWN) ? rect.width : rect.height) * 0.35);
		const double halfHeight = wxMax(1.5,
			((dir == wxUP || dir == wxDOWN) ? rect.height : rect.width) * 0.2);

		wxPoint2DDouble p[3];
		switch (dir) {
			case wxUP:
				p[0] = wxPoint2DDouble(cx - halfBase, cy + halfHeight);
				p[1] = wxPoint2DDouble(cx + halfBase, cy + halfHeight);
				p[2] = wxPoint2DDouble(cx, cy - halfHeight);
				break;
			case wxDOWN:
				p[0] = wxPoint2DDouble(cx - halfBase, cy - halfHeight);
				p[1] = wxPoint2DDouble(cx + halfBase, cy - halfHeight);
				p[2] = wxPoint2DDouble(cx, cy + halfHeight);
				break;
			case wxLEFT:
				p[0] = wxPoint2DDouble(cx + halfHeight, cy - halfBase);
				p[1] = wxPoint2DDouble(cx + halfHeight, cy + halfBase);
				p[2] = wxPoint2DDouble(cx - halfHeight, cy);
				break;
			default: // wxRIGHT
				p[0] = wxPoint2DDouble(cx - halfHeight, cy - halfBase);
				p[1] = wxPoint2DDouble(cx - halfHeight, cy + halfBase);
				p[2] = wxPoint2DDouble(cx + halfHeight, cy);
				break;
		}

		wxGraphicsContext* gc = wxGraphicsContext::CreateFromUnknownDC(dc);
		if (gc != nullptr) {
			gc->SetPen(*wxTRANSPARENT_PEN);
			gc->SetBrush(wxBrush(col));
			gc->DrawLines(3, p, wxWINDING_RULE);
			delete gc;
			return;
		}

		// fallback: plain integer polygon
		wxPoint pi[3];
		for (int i = 0; i < 3; i++)
			pi[i] = wxPoint(wxRound(p[i].m_x), wxRound(p[i].m_y));
		dc.SetPen(*wxTRANSPARENT_PEN);
		dc.SetBrush(wxBrush(col));
		dc.DrawPolygon(3, pi);
	}

	virtual void DrawScrollbarThumb(wxDC& dc, wxOrientation WXUNUSED(orient),
		const wxRect& rect, int flags = 0) override {
		wxColour face = m_scheme->Get(ibColourScheme::CONTROL);
		if (flags & wxCONTROL_PRESSED)
			face = m_scheme->Get(ibColourScheme::SCROLLBAR_PRESSED);
		else if (flags & wxCONTROL_CURRENT)
			face = face.ChangeLightness(106);
		dc.SetPen(wxPen(m_scheme->Get(ibColourScheme::SHADOW_IN)));
		dc.SetBrush(wxBrush(face));
		dc.DrawRectangle(rect);
	}

	virtual void DrawScrollbarShaft(wxDC& dc, wxOrientation WXUNUSED(orient),
		const wxRect& rect, int flags = 0) override {
		wxColour face = m_scheme->Get(ibColourScheme::SCROLLBAR);
		if (flags & wxCONTROL_PRESSED)
			face = m_scheme->Get(ibColourScheme::SCROLLBAR_PRESSED);
		dc.SetPen(*wxTRANSPARENT_PEN);
		dc.SetBrush(wxBrush(face));
		dc.DrawRectangle(rect);
	}

	virtual void DrawToolBarButton(wxDC& dc, const wxString& label,
		const wxBitmap& bitmap, const wxRect& rect, int flags = 0,
		long WXUNUSED(style) = 0, int WXUNUSED(tbarStyle) = 0) override {
		// a checked tool sits on a light accent pad, hover/press on a
		// chrome pad; both get the 1px palette border
		if (flags & (wxCONTROL_PRESSED | wxCONTROL_CURRENT | wxCONTROL_CHECKED)) {
			wxColour pad;
			if (flags & wxCONTROL_CHECKED)
				pad = kAccent.ChangeLightness(170);
			else if (flags & wxCONTROL_PRESSED)
				pad = m_scheme->Get(ibColourScheme::CONTROL_PRESSED);
			else
				pad = m_scheme->Get(ibColourScheme::CONTROL_CURRENT);

			dc.SetPen(wxPen((flags & wxCONTROL_CHECKED) ? kAccentDeep : kBorder));
			dc.SetBrush(wxBrush(pad));
			dc.DrawRectangle(rect);
		}
		dc.DrawLabel(label, bitmap, rect, wxALIGN_CENTRE);
	}

	virtual void DrawTab(wxDC& dc, const wxRect& rect, wxDirection WXUNUSED(dir),
		const wxString& label, const wxBitmap& bitmap = wxNullBitmap,
		int flags = 0, int indexAccel = -1) override {
		const bool selected = (flags & wxCONTROL_SELECTED) != 0;
		dc.SetPen(wxPen(m_scheme->Get(ibColourScheme::SHADOW_IN)));
		dc.SetBrush(wxBrush(selected ? m_scheme->Get(ibColourScheme::WINDOW)
		                             : m_scheme->Get(ibColourScheme::CONTROL)));
		dc.DrawRectangle(rect);
		DrawLabel(dc, label, rect, flags, wxALIGN_CENTRE, indexAccel);
		wxUnusedVar(bitmap);
	}

	// sliders: not revived yet — simple flat geometry
	virtual void DrawSliderShaft(wxDC& dc, const wxRect& rect, int lenThumb,
		wxOrientation orient, int flags = 0, long style = 0,
		wxRect* rectShaft = nullptr) override {
		const wxRect shaft = GetSliderShaftRect(rect, lenThumb, orient, style);
		dc.SetPen(wxPen(m_scheme->Get(ibColourScheme::SHADOW_IN)));
		dc.SetBrush(wxBrush(m_scheme->Get(ibColourScheme::WINDOW)));
		dc.DrawRectangle(shaft);
		if (rectShaft != nullptr)
			*rectShaft = shaft;
		wxUnusedVar(flags);
	}

	virtual void DrawSliderThumb(wxDC& dc, const wxRect& rect,
		wxOrientation WXUNUSED(orient), int flags = 0,
		long WXUNUSED(style) = 0) override {
		dc.SetPen(wxPen(kAccentDeep));
		dc.SetBrush(wxBrush((flags & wxCONTROL_PRESSED)
			? kAccentDeep : kAccent));
		dc.DrawRectangle(rect);
	}

	virtual void DrawSliderTicks(wxDC& dc, const wxRect& rect, int lenThumb,
		wxOrientation orient, int start, int end, int step = 1,
		int WXUNUSED(flags) = 0, long style = 0) override {
		if (end <= start || step <= 0)
			return;
		const wxRect shaft = GetSliderShaftRect(rect, lenThumb, orient, style);
		dc.SetPen(wxPen(m_scheme->Get(ibColourScheme::SHADOW_IN)));
		for (int n = start; n <= end; n += step) {
			if (orient == wxHORIZONTAL) {
				const wxCoord x = shaft.x + (shaft.width - 1) * (n - start) / (end - start);
				dc.DrawLine(x, rect.y, x, shaft.y);
			}
			else {
				const wxCoord y = shaft.y + (shaft.height - 1) * (n - start) / (end - start);
				dc.DrawLine(rect.x, y, shaft.x, y);
			}
		}
	}

	// menus (FORKED from the wxWin32Renderer menu block, Luna colours; the
	// check mark comes from our terracotta GetCheckBitmap instead of the
	// win32-private GetIndicator)
	virtual void DrawMenuBarItem(wxDC& dc, const wxRect& rectOrig,
		const wxString& label, int flags = 0, int indexAccel = -1) override {
		wxRect rect = rectOrig;
		rect.height--;

		wxDCTextColourChanger colChanger(dc);

		if (flags & wxCONTROL_SELECTED) {
			colChanger.Set(m_scheme->Get(ibColourScheme::HIGHLIGHT_TEXT));

			const wxColour colBg = m_scheme->Get(ibColourScheme::HIGHLIGHT);
			dc.SetBrush(colBg);
			dc.SetPen(colBg);
			dc.DrawRectangle(rect);
		}

		// don't draw the focus rect around menu bar items
		DrawLabel(dc, label, rect, flags & ~wxCONTROL_FOCUSED,
			wxALIGN_CENTRE, indexAccel);
	}

	virtual void DrawMenuItem(wxDC& dc, wxCoord y,
		const ibMenuGeometryInfo& gi, const wxString& label,
		const wxString& accel, const wxBitmap& bitmap = wxNullBitmap,
		int flags = 0, int indexAccel = -1) override {
		const ibLunaMenuGeometryInfo& geometryInfo =
			(const ibLunaMenuGeometryInfo&)gi;

		wxRect rect;
		rect.x = 0;
		rect.y = y;
		rect.width = geometryInfo.GetSize().x;
		rect.height = geometryInfo.GetItemHeight();

		// draw the selected item specially
		wxDCTextColourChanger colChanger(dc);
		if (flags & wxCONTROL_SELECTED) {
			colChanger.Set(m_scheme->Get(ibColourScheme::HIGHLIGHT_TEXT));

			const wxColour colBg = m_scheme->Get(ibColourScheme::HIGHLIGHT);
			dc.SetBrush(colBg);
			dc.SetPen(colBg);
			dc.DrawRectangle(rect);
		}

		// draw the bitmap: use the bitmap provided or the standard checkmark
		// for the checkable items
		wxBitmap bmp = bitmap;
		if (!bmp.IsOk() && (flags & wxCONTROL_CHECKED)) {
			bmp = GetCheckBitmap(flags);
		}

		if (bmp.IsOk()) {
			rect.SetRight(geometryInfo.GetLabelOffset());
			ibControlRenderer::DrawBitmap(dc, bmp, rect);
		}

		// draw the label
		rect.x = geometryInfo.GetLabelOffset();
		rect.SetRight(geometryInfo.GetAccelOffset());

		DrawLabel(dc, label, rect, flags, wxALIGN_CENTRE_VERTICAL, indexAccel);

		// draw the accel string
		rect.x = geometryInfo.GetAccelOffset();
		rect.SetRight(geometryInfo.GetSize().x);

		// NB: no accel index here
		DrawLabel(dc, accel, rect, flags, wxALIGN_CENTRE_VERTICAL);

		// draw the submenu indicator
		if (flags & wxCONTROL_ISSUBMENU) {
			rect.x = geometryInfo.GetSize().x - MENU_RIGHT_MARGIN;
			rect.width = MENU_RIGHT_MARGIN;

			DrawArrow(dc, wxRIGHT, rect, flags);
		}
	}

	virtual void DrawMenuSeparator(wxDC& dc, wxCoord y,
		const ibMenuGeometryInfo& geomInfo) override {
		dc.SetPen(wxPen(m_scheme->Get(ibColourScheme::SHADOW_IN)));
		dc.DrawLine(0, y + MENU_VERT_MARGIN, geomInfo.GetSize().x,
			y + MENU_VERT_MARGIN);
	}

	virtual void GetComboBitmaps(wxBitmap* bmpNormal, wxBitmap* bmpFocus,
		wxBitmap* bmpPressed, wxBitmap* bmpDisabled) override {
		// one flat drop-arrow for every state
		const wxSize size = GetScrollbarArrowSize();
		wxBitmap bmp(size.x, size.y);
		{
			wxMemoryDC dc(bmp);
			dc.SetBackground(wxBrush(m_scheme->Get(ibColourScheme::CONTROL)));
			dc.Clear();
			DrawArrow(dc, wxDOWN, wxRect(size), 0);
		}
		if (bmpNormal != nullptr)   *bmpNormal = bmp;
		if (bmpFocus != nullptr)    *bmpFocus = bmp;
		if (bmpPressed != nullptr)  *bmpPressed = bmp;
		if (bmpDisabled != nullptr) *bmpDisabled = bmp;
	}

	// metrics
	virtual wxSize GetScrollbarArrowSize() const override { return wxSize(16, 16); }
	virtual wxSize GetCheckBitmapSize() const override { return wxSize(16, 16); }
	virtual wxSize GetRadioBitmapSize() const override { return wxSize(16, 16); }
	virtual wxSize GetToolBarButtonSize(wxCoord* separator) const override {
		if (separator != nullptr)
			*separator = 5;
		return wxSize(24, 24);
	}
	virtual wxSize GetToolBarMargin() const override { return wxSize(4, 4); }
	virtual wxSize GetTabIndent() const override { return wxSize(2, 2); }
	virtual wxSize GetTabPadding() const override { return wxSize(6, 6); }
	virtual wxCoord GetSliderDim() const override { return 20; }
	virtual wxCoord GetSliderTickLen() const override { return 4; }

	virtual wxRect GetSliderShaftRect(const wxRect& rect, int WXUNUSED(lenThumb),
		wxOrientation orient, long WXUNUSED(style) = 0) const override {
		wxRect shaft = rect;
		if (orient == wxHORIZONTAL)
			shaft.Deflate(0, rect.height * 2 / 5);
		else
			shaft.Deflate(rect.width * 2 / 5, 0);
		return shaft;
	}

	virtual wxSize GetSliderThumbSize(const wxRect& rect, int WXUNUSED(lenThumb),
		wxOrientation orient) const override {
		return orient == wxHORIZONTAL
			? wxSize(rect.height / 2, rect.height)
			: wxSize(rect.width, rect.width / 2);
	}

	virtual wxSize GetProgressBarStep() const override { return wxSize(8, 8); }
	virtual wxSize GetMenuBarItemSize(const wxSize& sizeText) const override {
		return wxSize(sizeText.x + 16, sizeText.y + 8);
	}

	// FORKED from wxWin32Renderer::GetMenuGeometry — the canon authority on
	// the univ menu layout contract: it MUST call SetGeometry on every item
	// (ibMenuItem::GetPosition/GetHeight assert otherwise)
	virtual ibMenuGeometryInfo* GetMenuGeometry(wxWindow* win,
		const ibMenu& menu) const override {
		// prepare the dc: for now we draw all the items with the system font
		wxInfoDC dc(win);
		dc.SetFont(wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT));

		// the height of a normal item
		wxCoord heightText = dc.GetCharHeight();

		// the total height
		wxCoord height = 0;

		// the max length of label and accel strings: the menu width is the
		// sum of them, even if they're for different items (as the accels
		// should be aligned)
		//
		// the max length of the bitmap is never 0 as Windows always leaves
		// enough space for a check mark indicator
		wxCoord widthLabelMax = 0,
		        widthAccelMax = 0,
		        widthBmpMax = MENU_LEFT_MARGIN;

		for (wxMenuItemList::compatibility_iterator node = menu.GetMenuItems().GetFirst();
		     node;
		     node = node->GetNext()) {
			// height of this item
			wxCoord h;

			ibMenuItem* item = static_cast<ibMenuItem*>(node->GetData());
			if (item->IsSeparator()) {
				h = MENU_SEPARATOR_HEIGHT;
			}
			else { // not separator
				h = heightText;

				wxCoord widthLabel;
				dc.GetTextExtent(item->GetItemLabelText(), &widthLabel, nullptr);
				if (widthLabel > widthLabelMax) {
					widthLabelMax = widthLabel;
				}

				wxCoord widthAccel;
				dc.GetTextExtent(item->GetAccelString(), &widthAccel, nullptr);
				if (widthAccel > widthAccelMax) {
					widthAccelMax = widthAccel;
				}

				const wxBitmap& bmp = item->GetBitmap();
				if (bmp.IsOk()) {
					wxCoord widthBmp = bmp.GetWidth();
					if (widthBmp > widthBmpMax)
						widthBmpMax = widthBmp;
				}
				//else if ( item->IsCheckable() ): no need to check for this
				// as MENU_LEFT_MARGIN is big enough to show the check mark
			}

			h += 2 * MENU_VERT_MARGIN;

			// remember the item position and height
			item->SetGeometry(height, h);

			height += h;
		}

		// bundle the metrics into a struct and return it
		ibLunaMenuGeometryInfo* gi = new ibLunaMenuGeometryInfo;

		gi->m_ofsLabel = widthBmpMax + 2 * MENU_BMP_MARGIN;
		gi->m_ofsAccel = gi->m_ofsLabel + widthLabelMax;
		if (widthAccelMax > 0) {
			// if we actually have any accels, add a margin
			gi->m_ofsAccel += MENU_ACCEL_MARGIN;
		}

		gi->m_heightItem = heightText + 2 * MENU_VERT_MARGIN;

		gi->m_size.x = gi->m_ofsAccel + widthAccelMax + MENU_RIGHT_MARGIN;
		gi->m_size.y = height;

		return gi;
	}

private:
	// draw the indicator into a fresh bitmap; size follows the system font
	wxBitmap CreateIndicatorBitmap(int flags, bool radio);
};

wxBitmap ibLunaRenderer::CreateIndicatorBitmap(int flags, bool radio) {
	const int size = wxSystemSettings::GetMetric(wxSYS_SMALLICON_X) > 0
		? wxSystemSettings::GetMetric(wxSYS_SMALLICON_X) : 16;

	// 32bpp + transparent clear + wxGCDC: the indicator corners blend with
	// ANY parent surface and the circles come out anti-aliased (a plain
	// wxMemoryDC gave solid corners and ragged 16px circles)
	wxBitmap bmp(size, size, 32);
	bmp.UseAlpha();
	wxMemoryDC mdc(bmp);
	wxGCDC dc(mdc);

	const bool on = (flags & wxCONTROL_CHECKED) != 0;
	const bool disabled = (flags & wxCONTROL_DISABLED) != 0;

	wxColour face = on ? kAccent : kContent;
	wxColour border = on ? kAccentDeep : kBorder;
	if (disabled) {
		face = kChromeLight;
		border = kBorder;
	}
	else if (flags & wxCONTROL_PRESSED)
		face = face.ChangeLightness(92);
	else if (flags & wxCONTROL_CURRENT)
		border = kCaptionBottom;

	// transparent corners — the indicator sits on any surface
	dc.SetBackground(*wxTRANSPARENT_BRUSH);
	dc.Clear();

	const wxColour mark = disabled ? kBorder : *wxWHITE;

	if (radio) {
		// float coordinates through the graphics context: the true centre of
		// an even-sized bitmap is BETWEEN pixels (size/2.0), and the integer
		// DrawCircle API rounded the outer ring and the inner dot differently
		// — the dot visibly drifted off-centre
		wxGraphicsContext* gc = dc.GetGraphicsContext();
		const double centre = size / 2.0;
		const double radiusOuter = centre - 1.5;

		gc->SetPen(wxPen(border));
		gc->SetBrush(wxBrush(on && !disabled ? kContent : face));
		gc->DrawEllipse(centre - radiusOuter, centre - radiusOuter,
			2.0 * radiusOuter, 2.0 * radiusOuter);

		if (on) {
			// NB: the dot gets a pen of its own colour — a brush-only
			// ellipse fills half a pixel off the pen-outlined outer circle
			// and the dot visibly drifts off-centre
			const double radiusDot = radiusOuter * 0.4;
			const wxColour colDot = disabled ? kBorder : kAccent;
			gc->SetPen(wxPen(colDot));
			gc->SetBrush(wxBrush(colDot));
			gc->DrawEllipse(centre - radiusDot, centre - radiusDot,
				2.0 * radiusDot, 2.0 * radiusDot);
		}
	}
	else {
		const wxRect rect(1, 1, size - 2, size - 2);
		dc.SetPen(wxPen(border));
		dc.SetBrush(wxBrush(face));
		dc.DrawRoundedRectangle(rect, 2);

		if (flags & wxCONTROL_CHECKED) {
			const wxCoord w = rect.width;
			wxPoint tick[3] = {
				wxPoint(rect.x + w / 4,     rect.y + w / 2),
				wxPoint(rect.x + w * 2 / 5, rect.y + w * 7 / 10),
				wxPoint(rect.x + w * 3 / 4, rect.y + w * 3 / 10)
			};
			dc.SetPen(wxPen(mark, wxMax(1, w / 7)));
			dc.DrawLines(3, tick);
		}
		else if (flags & wxCONTROL_UNDETERMINED) {
			wxRect rectMark = rect;
			rectMark.Deflate(rect.width / 3);
			dc.SetPen(*wxTRANSPARENT_PEN);
			dc.SetBrush(wxBrush(disabled ? kBorder : kAccent));
			dc.DrawRectangle(rectMark);
		}
	}

	mdc.SelectObject(wxNullBitmap);
	return bmp;
}

wxBitmap ibLunaRenderer::GetRadioBitmap(int flags) {
	return CreateIndicatorBitmap(flags, true /* radio */);
}

wxBitmap ibLunaRenderer::GetCheckBitmap(int flags) {
	return CreateIndicatorBitmap(flags, false /* checkbox */);
}

wxBitmap ibLunaRenderer::GetFrameButtonBitmap(FrameButtonType WXUNUSED(type)) {
	// top-level frames stay native for now — nothing draws these yet
	return wxNullBitmap;
}

// ----------------------------------------------------------------------------
// ibLunaTheme
// ----------------------------------------------------------------------------

class ibLunaTheme : public ibThemeEngine {
public:

	ibLunaTheme() {
		m_renderer = nullptr;
	}

	virtual ~ibLunaTheme() {
		delete m_renderer;
	}

	virtual ibRenderer* GetRenderer() override {
		if (m_renderer == nullptr)
			m_renderer = new ibLunaRenderer(&m_scheme);
		return m_renderer;
	}

	virtual wxArtProvider* GetArtProvider() override {
		// the application-wide art provider serves the theme as well
		return nullptr;
	}

	virtual ibInputHandler* GetInputHandler(const wxString& WXUNUSED(handlerType),
		ibInputConsumer* consumer) override {
		// the standard handlers are static objects owned by the controls
		return consumer->DoGetStdInputHandler(nullptr);
	}

	virtual ibColourScheme* GetColourScheme() override {
		return &m_scheme;
	}

private:
	ibLunaRenderer* m_renderer;
	ibLunaColourScheme m_scheme;

	WX_DECLARE_THEME(luna)
};

WX_IMPLEMENT_THEME(ibLunaTheme, luna, wxTRANSLATE("OES Luna theme"));
