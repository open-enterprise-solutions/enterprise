// ----------------------------------------------------------------------------
// Luna theme — the default deskgui theme, built the way every univ theme is:
// an ibThemeRenderer subclass + an ibColourScheme carrying the interior-design
// palette from docs/private/ui-palette.md (cool dusty-blue chrome, warm-cream
// content, terracotta accent). Registered as the default theme "luna".
// ----------------------------------------------------------------------------

#include <wx/dc.h>
#include <wx/dcmemory.h>
#include <wx/dcgraph.h>
#include <wx/settings.h>
#include <wx/window.h>
#include <wx/artprov.h>

#include "deskgui/univ/theme.h"
#include "deskgui/univ/renderer.h"
#include "deskgui/univ/colourScheme.h"
#include "deskgui/univ/inputConsumer.h"
#include "deskgui/univ/window.h"
#include "deskgui/univ/theme/themeRenderer.h"

namespace {

// palette tiers (docs/private/ui-palette.md — reuse a tier, don't invent a shade)
const wxColour kChromeLight(0xC8, 0xD6, 0xDF);   // chrome-light  #C8D6DF
const wxColour kPowder(0xE6, 0xEE, 0xF5);        // powder        #E6EEF5
const wxColour kContent(0xFA, 0xF7, 0xF0);       // content       #FAF7F0
const wxColour kBorder(0xA8, 0xBA, 0xC8);        // border        #A8BAC8
const wxColour kDisabledText(0x94, 0xA6, 0xB4);  // disabled-text #94A6B4
const wxColour kCaptionTop(0x5A, 0x7B, 0x95);    // caption-active-top
const wxColour kCaptionBottom(0x3F, 0x5C, 0x77); // caption-active-bottom
const wxColour kCaptionInactiveTop(0xD8, 0xE2, 0xEB);    // caption-inactive-top
const wxColour kCaptionInactiveBottom(0xC5, 0xD2, 0xDC); // caption-inactive-bottom
const wxColour kAccent(0xD9, 0x77, 0x57);        // accent        #D97757
const wxColour kAccentDeep(0xB8, 0x5A, 0x38);    // accent-deep   #B85A38

// menu metrics (FORKED from the wxWin32Renderer menu block — the only canon
// implementation of the univ menu geometry contract; margins widened from
// the Win95 values 9/18/3 — modern menus breathe)
const wxCoord MENU_LEFT_MARGIN = 22;
const wxCoord MENU_RIGHT_MARGIN = 24;
const wxCoord MENU_VERT_MARGIN = 5;

// the empty strip at each of the menu's four sides: the items, their highlight and the separators stand inside it -
// drawn from the border, the highlight ran edge to edge and touched the border above the first item and below the
// last, and the pictures stood 5 px off it (2026-11-07)
const wxCoord MENU_SIDE_MARGIN = 4;

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
// ibLunaColourScheme
// ----------------------------------------------------------------------------

class ibLunaColourScheme : public ibColourScheme {
public:
	virtual wxColour Get(StdColour col) const override;
	virtual wxColour GetBackground(ibWindow* win) const override;
};

// the object inspector's palette for the whole theme (Max, 2026-11-10: "too greyish a theme"): a control and a
// scroll bar are the powder its margin is, a dialog and a panel the light caption tier under them (a button stays
// lighter than the surface it sits on, so it still reads as one); the chrome-light tier stays a caption's. The text is
// black, the alternate text the deep dusty blue the forms' tables had - a table's values and columns, the start page
// (Max, 2026-11-10: "by default black, an alternate display beside it") - the desktop painted it in by hand
wxColour ibLunaColourScheme::Get(ibLunaColourScheme::StdColour col) const {
	switch (col) {
		case WINDOW:                      return kContent;
		case CONTROL:                     return kPowder;
		case CONTROL_PRESSED:             return kPowder.ChangeLightness(92);
		case CONTROL_CURRENT:             return kPowder.ChangeLightness(106);
		case CONTROL_TEXT:                return *wxBLACK;
		case CONTROL_TEXT_DISABLED:       return kDisabledText;
		case CONTROL_TEXT_DISABLED_SHADOW:return kChromeLight;
		case CONTROL_TEXT_ALTERNATE:      return kCaptionBottom;
		case SCROLLBAR:                   return kPowder;
		case SCROLLBAR_PRESSED:           return kBorder;
		case HIGHLIGHT:                   return kAccent;
		case HIGHLIGHT_TEXT:              return *wxWHITE;
		case HYPERLINK:                   return kAccentDeep;   // an action, as the default button; not the text's blue
		case SHADOW_DARK:                return kCaptionBottom;
		case SHADOW_HIGHLIGHT:            return *wxWHITE;
		case SHADOW_IN:                   return kBorder;
		case SHADOW_OUT:                  return kChromeLight.ChangeLightness(90);
		case TITLEBAR:                    return kChromeLight;
		case TITLEBAR_ACTIVE:             return kCaptionTop;
		case TITLEBAR_TEXT:               return kCaptionBottom;
		case TITLEBAR_ACTIVE_TEXT:        return *wxWHITE;
		case GAUGE:                       return kAccent;
		case DESKTOP:                     return kCaptionInactiveTop;
		case FRAME:                       return kCaptionInactiveTop;
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
		// so a branded control hovers/presses like a themed one - one that
		// does not highlight has no face to press either (a hyperlink)
		wxColour col = win->GetBackgroundColour();
		const int flags = win->GetStateFlags();
		if ((flags & wxCONTROL_PRESSED) && win->CanBeHighlighted())
			return col.ChangeLightness(92);
		if ((flags & wxCONTROL_CURRENT) && win->CanBeHighlighted())
			return col.ChangeLightness(106);
		return col;
	}

	// a button without a border (a field's "...", "×") is flat: the surface under it, its parent's own colour or the
	// theme's, until the mouse is over it — hovered or pressed it is a button below, with the button's chrome
	ibWindow* const parent = dynamic_cast<ibWindow*>(win->GetParent());
	if (parent != nullptr && win->CanBeHighlighted() && !win->HasTransparentBackground()
			&& win->GetBorder() == wxBORDER_NONE
			&& (win->GetStateFlags() & (wxCONTROL_CURRENT | wxCONTROL_PRESSED)) == 0)
		return GetBackground(parent);

	// input surfaces (text fields, list boxes — the win32 canon classified
	// the same way) live on the content colour, not on the chrome
	if (win->HasContentBackground())
		return Get(WINDOW);

	// highlightable controls (buttons and friends) take the control colour
	// modulated by state — pressed must be UNMISTAKABLE (a toggle button
	// stays pressed, the old chrome-8% shade was nearly invisible); one whose
	// face is not its background (a check box: the box) takes the surface
	if (win->CanBeHighlighted() && !win->HasTransparentBackground()) {
		const int flags = win->GetStateFlags();
		if (flags & wxCONTROL_PRESSED)
			return kAccent.ChangeLightness(172);
		if (flags & wxCONTROL_CURRENT)
			return Get(CONTROL_CURRENT);
		return Get(CONTROL);
	}

	// label-like statics and the containers they sit on take the surface under them - the colour set on the nearest
	// window up the chain the background shows through (wxMSW's MSWGetBgBrush: on through a transparent window, to one
	// that is not), else the theme's chrome. A parent's colour that is only its default is not that surface: an MDI
	// child is a frame, its default the workspace grey, and the home page's panes (ibPanel, transparent under the MDI
	// client's colour) drew it in their header gaps while their labels drew the panel's own. Nor is the window's own
	// default, the platform's: black on macOS, grey beside a dialog's chrome on Windows - the chrome a dialog takes
	// (ibTopLevelWindow::Create). Content (a tree, a list, a grid) paints its own background, not through here.
	for (wxWindow* parent = win->GetParent(); parent != nullptr; parent = parent->GetParent()) {
		if (parent->UseBgCol())
			return parent->GetBackgroundColour();
		if (!parent->HasTransparentBackground() || parent->IsTopLevel())
			break;
	}
	return Get(FRAME);
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

	// a top-level window's decorations (ibTopLevelWindow) as the designer's form card has them (ibInnerFrame): the
	// border every raised window has, the system's caption colour lightening to the right, the buttons flat glyphs; a
	// resizable window's corner marked, and held there it is sized
	virtual int HitTestFrame(const wxRect& rect, const wxPoint& pt, int flags = 0) const override;
	virtual void DrawFrameBorder(wxDC& dc, const wxRect& rect, int flags) override;
	virtual void DrawFrameBackground(wxDC& dc, const wxRect& rect, int flags) override;
	virtual void DrawFrameButton(wxDC& dc, wxCoord x, wxCoord y, int button, int flags = 0) override;
	virtual int GetFrameBorderWidth(int flags) const override;

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
	virtual wxRect GetTextTotalArea(const wxRect& rect) const override {
		wxRect total = rect;
		total.Inflate(GetTextBorderWidth() + 3, GetTextBorderWidth() + 1);
		return total;
	}

	virtual wxRect GetTextClientArea(const wxRect& rect, wxCoord* extraSpaceBeyond) const override {
		wxRect client = rect;
		client.Deflate(GetTextBorderWidth() + 3, GetTextBorderWidth() + 1);

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

	// …and reserves what it draws: the base keeps 2px for the 3-D borders, and the inner pixel, painted by nobody,
	// let the parent show through round every field, list and tree (2026-11-06)
	virtual wxRect GetBorderDimensions(wxBorder border) const override {
		const wxCoord width = (border == wxBORDER_NONE || border == wxBORDER_DEFAULT) ? 0 : 1;
		return wxRect(width, width, width, width);
	}

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

	virtual void DrawTab(wxDC& dc, const wxRect& rect, wxDirection dir,
		const wxString& label, const wxBitmap& bitmap = wxNullBitmap,
		int flags = 0, int indexAccel = -1) override {
		const bool selected = (flags & wxCONTROL_SELECTED) != 0;
		const wxColour face = selected ? m_scheme->Get(ibColourScheme::WINDOW)
		                               : m_scheme->Get(ibColourScheme::CONTROL);
		dc.SetPen(wxPen(m_scheme->Get(ibColourScheme::SHADOW_IN)));
		dc.SetBrush(wxBrush(face));
		dc.DrawRectangle(rect);
		// the label and the tab's picture beside it - DrawLabel's road (DrawButtonLabel), of the picture too: dropped
		// here, no tab showed the one its book was given
		if (dir != wxLEFT && dir != wxRIGHT) {
			DrawButtonLabel(dc, label, bitmap, rect, flags, wxALIGN_CENTRE, indexAccel);
			return;
		}

		// A TAB DOWN A SIDE has its label along it (the book turns its geometry, GetTabSize): drawn across into a
		// bitmap of the tab turned, then turned back - read top down on the right, bottom up on the left; the picture
		// turned the other way first, so it stands upright. The canon's road (theme_win32's DrawTab); drawn across,
		// as it was here, the label was cut to the tab's width.
		const wxRect inside = rect.Deflate(1, 1);   // inside the tab's line
		const double scale = dc.GetContentScaleFactor();
		wxBitmap across;
		across.CreateWithLogicalSize(inside.height, inside.width, scale);
		{
			wxMemoryDC dcMem(across);
			dcMem.SetBackground(wxBrush(face));
			dcMem.Clear();
			dcMem.SetFont(dc.GetFont());
			dcMem.SetTextForeground(dc.GetTextForeground());
			const wxBitmap picture = bitmap.IsOk()
				? wxBitmap(bitmap.ConvertToImage().Rotate90(dir == wxLEFT), -1, bitmap.GetScaleFactor())
				: wxNullBitmap;
			DrawButtonLabel(dcMem, label, picture, wxRect(0, 0, inside.height, inside.width), flags, wxALIGN_CENTRE,
				indexAccel);
		}
		dc.DrawBitmap(wxBitmap(across.ConvertToImage().Rotate90(dir == wxRIGHT), -1, scale), inside.GetPosition());
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
		const ibMenuGeometryInfo& geometryInfo, const wxString& label,
		const wxString& accel, const wxBitmap& bitmap = wxNullBitmap,
		int flags = 0, int indexAccel = -1) override {
		wxRect rect;
		rect.x = MENU_SIDE_MARGIN;
		rect.y = y;
		rect.width = geometryInfo.GetSize().x - 2 * MENU_SIDE_MARGIN;
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
			ibRenderer::DrawBitmap(dc, bmp, rect);
		}

		// draw the label
		rect.x = geometryInfo.GetLabelOffset();
		rect.SetRight(geometryInfo.GetAccelOffset());

		DrawLabel(dc, label, rect, flags, wxALIGN_CENTRE_VERTICAL, indexAccel);

		// draw the accel string
		rect.x = geometryInfo.GetAccelOffset();
		rect.SetRight(geometryInfo.GetSize().x - MENU_SIDE_MARGIN);

		// NB: no accel index here
		DrawLabel(dc, accel, rect, flags, wxALIGN_CENTRE_VERTICAL);

		// draw the submenu indicator
		if (flags & wxCONTROL_ISSUBMENU) {
			rect.x = geometryInfo.GetSize().x - MENU_SIDE_MARGIN - MENU_RIGHT_MARGIN;
			rect.width = MENU_RIGHT_MARGIN;

			DrawArrow(dc, wxRIGHT, rect, flags);
		}
	}

	virtual void DrawMenuSeparator(wxDC& dc, wxCoord y,
		const ibMenuGeometryInfo& geomInfo) override {
		dc.SetPen(wxPen(m_scheme->Get(ibColourScheme::SHADOW_IN)));
		dc.DrawLine(MENU_SIDE_MARGIN, y + MENU_VERT_MARGIN, geomInfo.GetSize().x - MENU_SIDE_MARGIN,
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

	// the menu's metrics (FORKED from wxWin32Renderer::GetMenuGeometry, which laid the menu out with them itself; the
	// menu does now, ibMenu::GetGeometryInfo)
	virtual wxSize GetMenuMargin() const override {
		return wxSize(MENU_SIDE_MARGIN, MENU_SIDE_MARGIN);
	}
	virtual wxCoord GetMenuItemHeight(wxCoord heightText) const override {
		return heightText + 2 * MENU_VERT_MARGIN;
	}
	virtual wxCoord GetMenuSeparatorHeight() const override {
		return MENU_SEPARATOR_HEIGHT + 2 * MENU_VERT_MARGIN;
	}
	// never less than MENU_LEFT_MARGIN: Windows always leaves enough space for a check mark indicator
	virtual wxCoord GetMenuBitmapWidth(wxCoord widthBitmap) const override {
		return wxMax(widthBitmap, MENU_LEFT_MARGIN) + 2 * MENU_BMP_MARGIN;
	}
	virtual wxCoord GetMenuAccelMargin() const override { return MENU_ACCEL_MARGIN; }
	virtual wxCoord GetMenuSubmenuWidth() const override { return MENU_RIGHT_MARGIN; }

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
	// where Tab stands: the box and the circle have no frame of their own to show the focus, as a field's or a button's
	// does (DrawBorder, DrawButtonBorder) - the accent's ring round the indicator, in the margin it leaves
	const bool focused = (flags & wxCONTROL_FOCUSED) != 0 && !disabled;

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

		if (focused) {
			const double radiusFocus = centre - 0.5;
			gc->SetPen(wxPen(kAccentDeep));
			gc->SetBrush(*wxTRANSPARENT_BRUSH);
			gc->DrawEllipse(centre - radiusFocus, centre - radiusFocus,
				2.0 * radiusFocus, 2.0 * radiusFocus);
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

		if (focused) {
			dc.SetPen(wxPen(kAccentDeep));
			dc.SetBrush(*wxTRANSPARENT_BRUSH);
			dc.DrawRoundedRectangle(wxRect(0, 0, size, size), 3);
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
	// the frame's buttons are glyphs (DrawFrameButton), not bitmaps
	return wxNullBitmap;
}

// The corner of a resizable window's client area that sizes it - the square the native size grip takes.
static wxRect GetFrameGripRect(const wxRect& client) {
	const int side = 12;
	return wxRect(client.GetRight() + 1 - side, client.GetBottom() + 1 - side, side, side);
}

int ibLunaRenderer::HitTestFrame(const wxRect& rect, const wxPoint& pt, int flags) const {
	const int hit = ibThemeRenderer::HitTestFrame(rect, pt, flags);
	if (hit == wxHT_TOPLEVEL_CLIENT_AREA && (flags & wxTOPLEVEL_RESIZEABLE) && !(flags & wxTOPLEVEL_MAXIMIZED)
			&& GetFrameGripRect(GetFrameClientArea(rect, flags)).Contains(pt))
		return wxHT_TOPLEVEL_BORDER_SE;
	return hit;
}

void ibLunaRenderer::DrawFrameBorder(wxDC& dc, const wxRect& rect, int flags) {
	if (flags & wxTOPLEVEL_BORDER)
		DrawBorder(dc, wxBORDER_RAISED, rect);

	// the corner's mark: three strokes across it, as the native grip has
	if ((flags & wxTOPLEVEL_RESIZEABLE) && !(flags & wxTOPLEVEL_MAXIMIZED)) {
		const wxRect grip = GetFrameGripRect(GetFrameClientArea(rect, flags));
		dc.SetPen(wxPen(kCaptionBottom));
		for (int stroke = 1; stroke <= 3; ++stroke) {
			const int d = 4 * stroke - 1;
			dc.DrawLine(grip.GetRight() - d, grip.GetBottom(), grip.GetRight() + 1, grip.GetBottom() - d - 1);
		}
	}
}

void ibLunaRenderer::DrawFrameBackground(wxDC& dc, const wxRect& rect, int flags) {
	if (!(flags & wxTOPLEVEL_TITLEBAR))
		return;

	// the band from the border's line to the client: a resizable window's grip above the title is the band too
	const bool bordered = (flags & wxTOPLEVEL_BORDER) && !(flags & wxTOPLEVEL_MAXIMIZED);
	const int line = bordered ? GetBorderDimensions(wxBORDER_RAISED).y : 0;
	const wxRect client = GetFrameClientArea(rect, flags);
	const wxRect band(rect.x + line, rect.y + line, rect.width - 2 * line, client.y - rect.y - line);

	// the palette's captions, the panes' (luna_dockart): the active one dusty blue under its white title, the inactive
	// one light under its blue title. The system's caption colour lightening to the right was a pale blue the white
	// title hardly showed on (Max, 2026-11-07), and the platform's, not the theme's
	const bool active = (flags & wxTOPLEVEL_ACTIVE) != 0;
	dc.GradientFillLinear(band, active ? kCaptionTop : kCaptionInactiveTop,
		active ? kCaptionBottom : kCaptionInactiveBottom, wxSOUTH);
}

void ibLunaRenderer::DrawFrameButton(wxDC& dc, wxCoord x, wxCoord y, int button, int flags) {
	// a face only while pressed
	const wxRect face(x, y, FRAME_BUTTON_WIDTH, FRAME_BUTTON_HEIGHT);
	if (flags & wxCONTROL_PRESSED) {
		dc.SetPen(*wxTRANSPARENT_PEN);
		dc.SetBrush(wxBrush(kBorder));
		dc.DrawRectangle(face);
	}

	// the glyph in a square of 8 pixels in the middle, in the title's colour - DrawFrameTitle sets it just before the
	// buttons: white on the active caption, blue on the inactive one
	const int side = 8;
	const wxRect glyph(face.x + (face.width - side) / 2, face.y + (face.height - side) / 2, side, side);
	dc.SetPen(wxPen(dc.GetTextForeground()));
	dc.SetBrush(*wxTRANSPARENT_BRUSH);
	switch (button) {
	case wxTOPLEVEL_BUTTON_CLOSE:
		dc.DrawLine(glyph.GetLeft(), glyph.GetTop(), glyph.GetRight() + 1, glyph.GetBottom() + 1);
		dc.DrawLine(glyph.GetRight(), glyph.GetTop(), glyph.GetLeft() - 1, glyph.GetBottom() + 1);
		break;
	case wxTOPLEVEL_BUTTON_MAXIMIZE:
		dc.DrawRectangle(glyph);
		break;
	case wxTOPLEVEL_BUTTON_RESTORE:
		// the window in front, the corner of the one behind it
		dc.DrawRectangle(glyph.x, glyph.y + 2, side - 2, side - 2);
		dc.DrawLine(glyph.x + 2, glyph.y, glyph.GetRight() + 1, glyph.y);
		dc.DrawLine(glyph.GetRight(), glyph.y, glyph.GetRight(), glyph.y + side - 2);
		break;
	case wxTOPLEVEL_BUTTON_ICONIZE:
		dc.DrawLine(glyph.GetLeft(), glyph.GetBottom(), glyph.GetRight() + 1, glyph.GetBottom());
		break;
	case wxTOPLEVEL_BUTTON_HELP:
		dc.DrawLabel(wxT("?"), face, wxALIGN_CENTRE);
		break;
	}
}

int ibLunaRenderer::GetFrameBorderWidth(int flags) const {
	// the border's line, and inside it a resizable window's grip
	const int line = GetBorderDimensions(wxBORDER_RAISED).x;
	return (flags & wxTOPLEVEL_RESIZEABLE) ? line + 4 : line;
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

WX_IMPLEMENT_THEME(ibLunaTheme, luna, ibTRANSLATE("OES Luna theme"));
