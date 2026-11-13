// ----------------------------------------------------------------------------
// Luna theme — the default deskgui theme, built the way every univ theme is:
// an ibThemeRenderer subclass + an ibColourScheme carrying the interior-design
// palette from docs/private/ui-palette.md (cool dusty-blue chrome, white
// content, terracotta accent). Registered as the default theme "luna".
// ----------------------------------------------------------------------------

#include <wx/dc.h>
#include <wx/dcmemory.h>
#include <wx/dcgraph.h>
#include <wx/dcscreen.h>   // wxScreenDC - the standard text measured for a row's air (GetListboxItemHeight)
#include <wx/settings.h>
#include <wx/window.h>
#include <wx/control.h>   // wxControl::Ellipsize - a pane caption's title cut to its room (DrawPaneCaption)
#include <wx/artprov.h>
#include <wx/bmpbndl.h>
#include <wx/image.h>
#include <wx/mstream.h>
#include <wx/toolbar.h>   // wxTOOL_STYLE_* - a tool bar's button or separator (DrawToolBarButton)

#include "core/utils/base64.h"   // ibBase64 - a picture's PNG written as text

#include "deskgui/artProvider.h"   // the pictures' ids, the product's own
#include "deskgui/univ/theme.h"
#include "deskgui/univ/renderer.h"
#include "deskgui/univ/colourScheme.h"
#include "deskgui/univ/inputConsumer.h"
#include "deskgui/univ/window.h"
#include "deskgui/univ/aui/framemanager.h"   // ibAUI_BUTTON_* - the AUI buttons (DrawStripButton, DrawPaneButton)
#include "deskgui/univ/theme/themeRenderer.h"
#include "deskgui/univ/theme/private/picturePredefined.h"   // the pictures, a PNG in Base64 each

namespace {

// palette tiers (docs/private/ui-palette.md — reuse a tier, don't invent a shade). The chrome's tiers - chrome,
// chrome-light, the inactive caption's two - were lifted a third of the way to white, their blue a quarter less (Max,
// 2026-11-12: "the theme is greyish, it presses"; the variant he picked of three); the powder and the border stayed -
// a table's header, rules and scroll bars are theirs, and the tables were liked as they were. The content is white, as a
// table's rows: it was cream #FAF7F0 - "the cream gives a feeling of dirt" (Max, the same day)
const wxColour kChrome(0xD3, 0xDB, 0xE1);        // chrome        #D3DBE1
const wxColour kChromeLight(0xDD, 0xE4, 0xE8);   // chrome-light  #DDE4E8
const wxColour kPowder(0xE6, 0xEE, 0xF5);        // powder        #E6EEF5
const wxColour kContent(0xFF, 0xFF, 0xFF);       // content       #FFFFFF
const wxColour kBorder(0xA8, 0xBA, 0xC8);        // border        #A8BAC8
const wxColour kDisabledText(0x94, 0xA6, 0xB4);  // disabled-text #94A6B4
const wxColour kCaptionTop(0x5A, 0x7B, 0x95);    // caption-active-top
const wxColour kCaptionBottom(0x3F, 0x5C, 0x77); // caption-active-bottom
const wxColour kCaptionInactiveTop(0xE7, 0xEC, 0xF0);    // caption-inactive-top    #E7ECF0
const wxColour kCaptionInactiveBottom(0xDB, 0xE2, 0xE6); // caption-inactive-bottom #DBE2E6
const wxColour kAccent(0xD9, 0x77, 0x57);        // accent        #D97757
const wxColour kAccentDeep(0xB8, 0x5A, 0x38);    // accent-deep   #B85A38

// a title bar's buttons, the Mac's colours (DrawFrameButton): close, minimize, maximize or restore
const wxColour kFrameClose(0xFF, 0x5F, 0x57);    // #FF5F57
const wxColour kFrameIconize(0xFE, 0xBC, 0x2E);  // #FEBC2E
const wxColour kFrameMaximize(0x28, 0xC8, 0x40); // #28C840

// the shelf under an AUI book's tabs (DrawTabStrip): a neutral warm taupe between the cool strip and the pages - a
// sandstone #E8D8BE read as too yellow
const wxColour kShelf(0xE0, 0xD8, 0xCC);

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

// the glyphs of the AUI buttons, 16 x 16 bits each - an AUI book's (DrawStripButton): close, left, right, the list of the
// tabs; a pane caption's (DrawPaneButton): close, maximize, restore, pin. One close glyph each on every platform - the
// arts drew the one each platform's own close looked like.
const int AUI_GLYPH_SIZE = 16;

const unsigned char kStripCloseBits[] = {
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xe7, 0xf3, 0xcf, 0xf9,
	0x9f, 0xfc, 0x3f, 0xfe, 0x3f, 0xfe, 0x9f, 0xfc, 0xcf, 0xf9, 0xe7, 0xf3,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

const unsigned char kStripLeftBits[] = {
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe, 0x7f, 0xfe, 0x3f, 0xfe,
	0x1f, 0xfe, 0x0f, 0xfe, 0x1f, 0xfe, 0x3f, 0xfe, 0x7f, 0xfe, 0xff, 0xfe,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

const unsigned char kStripRightBits[] = {
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xdf, 0xff, 0x9f, 0xff, 0x1f, 0xff,
	0x1f, 0xfe, 0x1f, 0xfc, 0x1f, 0xfe, 0x1f, 0xff, 0x9f, 0xff, 0xdf, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

const unsigned char kStripListBits[] = {
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0x0f, 0xf8, 0xff, 0xff, 0x0f, 0xf8, 0x1f, 0xfc, 0x3f, 0xfe, 0x7f, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

const unsigned char kPaneCloseBits[] = {
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xcf, 0xf3, 0x9f, 0xf9,
	0x3f, 0xfc, 0x7f, 0xfe, 0x3f, 0xfc, 0x9f, 0xf9, 0xcf, 0xf3, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

const unsigned char kPaneMaximizeBits[] = {
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x07, 0xf0, 0xf7, 0xf7, 0x07, 0xf0,
	0xf7, 0xf7, 0xf7, 0xf7, 0xf7, 0xf7, 0xf7, 0xf7, 0xf7, 0xf7, 0x07, 0xf0,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

const unsigned char kPaneRestoreBits[] = {
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x1f, 0xf0, 0x1f, 0xf0, 0xdf, 0xf7,
	0x07, 0xf4, 0x07, 0xf4, 0xf7, 0xf5, 0xf7, 0xf1, 0xf7, 0xfd, 0xf7, 0xfd,
	0x07, 0xfc, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

const unsigned char kPanePinBits[] = {
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x1f, 0xfc, 0xdf, 0xfc, 0xdf, 0xfc,
	0xdf, 0xfc, 0xdf, 0xfc, 0xdf, 0xfc, 0x0f, 0xf8, 0x7f, 0xff, 0x7f, 0xff,
	0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

// A glyph's bits (XBM) in a colour, the rest masked out.
wxBitmap ibAuiBitmapFromBits(const unsigned char bits[], int w, int h, const wxColour& color)
{
	wxImage img = wxBitmap((const char*)bits, w, h).ConvertToImage();
	img.Replace(0, 0, 0, 123, 123, 123);
	img.Replace(255, 255, 255, color.Red(), color.Green(), color.Blue());
	img.SetMaskColour(123, 123, 123);
	return wxBitmap(img);
}

} // anonymous namespace

// ----------------------------------------------------------------------------
// ibLunaColourScheme
// ----------------------------------------------------------------------------

class ibLunaColourScheme : public ibColourScheme {
public:
	virtual wxColour Get(StdColour col) const override;
	virtual wxColour GetBackground(ibWindow* win) const override;
};

// the object inspector's palette for the whole theme (Max, 2026-11-10: "too greyish a theme"): a control is the
// powder its margin is and a scroll bar's track a lighter one (2026-11-13), a dialog and a panel the light caption tier under them (a button is white
// on it since 2026-11-12, powder under the mouse - GetBackground); the chrome-light tier stays a caption's. The text is
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
		case SCROLLBAR:                   return kPowder.ChangeLightness(150);
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
		case DESKTOP:                     return kChrome;   // the docking's, round its panes (ibAuiManager)
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

	// highlightable controls (buttons and friends) are white, as a field, their border holding them on the surface, and
	// take the control colour under the mouse - the powder they were sat too close to a dialog's chrome after the palette
	// was lifted, a plaque held by its border alone (2026-11-12, Max: "white"). Pressed must be UNMISTAKABLE (a toggle
	// button stays pressed, the old chrome-8% shade was nearly invisible); one whose face is not its background (a check
	// box: the box) takes the surface. On GTK3/Cocoa the same - the buttons are the engine's there too
	if (win->CanBeHighlighted() && !win->HasTransparentBackground()) {
		const int flags = win->GetStateFlags();
		if (flags & wxCONTROL_PRESSED)
			return kAccent.ChangeLightness(172);
		if (flags & wxCONTROL_CURRENT)
			return Get(CONTROL);
		return Get(WINDOW);
	}

	// label-like statics and the containers they sit on take the surface under them - the colour set on the nearest
	// window up the chain the background shows through (wxMSW's MSWGetBgBrush: on through a transparent window, to one
	// that is not), else the theme's chrome. A parent's colour that is only its default is not that surface: an MDI
	// child is a frame, its default the workspace grey, and the home page's panes (ibPanel, transparent under the MDI
	// client's colour) drew it in their header gaps while their labels drew the panel's own. Nor is the window's own
	// default, the platform's: black on macOS, grey beside a dialog's chrome on Windows - the chrome a dialog takes
	// (ibTopLevelWindow::Create). Content (a tree, a list, a grid) paints its own background, not through here. A parent
	// the background does not show through, with no colour of its own, is the surface it paints - this same answer for
	// it: a panel over a report's sheet painted the sheet's colour, and its label stopped at it and drew the chrome round
	// «Composing...» (2026-11-12, Max)
	for (wxWindow* parent = win->GetParent(); parent != nullptr; parent = parent->GetParent()) {
		if (parent->UseBgCol())
			return parent->GetBackgroundColour();
		if (parent->IsTopLevel())
			break;
		if (!parent->HasTransparentBackground()) {
			if (ibWindow* const surface = dynamic_cast<ibWindow*>(parent))
				return GetBackground(surface);
			break;
		}
	}
	return Get(FRAME);
}

// ----------------------------------------------------------------------------
// ibLunaRenderer: the standard renderer with Luna indicator bitmaps
// ----------------------------------------------------------------------------

class ibLunaRenderer : public ibThemeRenderer {
public:
	// a pane caption is written in the system's font, as a title bar is (ibThemeRenderer): the default family's 10
	// points stood here until 2026-11-12 - Arial on Windows, beside an interface in Segoe UI. On GTK3/Cocoa it is the
	// system's font there, the one their controls are written in
	ibLunaRenderer(const ibColourScheme* scheme) : ibThemeRenderer(scheme),
		m_paneCaptionFont(wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT)) {}

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
		// for this label alone (a changer): set on the dc, the white stayed for whatever was drawn after it
		wxDCTextColourChanger colChanger(dc);
		if ((flags & wxCONTROL_ISDEFAULT) && !(flags & wxCONTROL_DISABLED))
			colChanger.Set(*wxWHITE);

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
	// the former, layout through the latter). Two above and below, one until
	// then: a field pressed round its line was the look of an older desktop
	// (2026-11-13, Max: "modernise the interface", four "a little smaller");
	// a button is as tall (ibButton::GetDefaultSize). On GTK3/Cocoa the same -
	// the fields are the engine's there too
	virtual wxRect GetTextTotalArea(const wxRect& rect) const override {
		wxRect total = rect;
		total.Inflate(GetTextBorderWidth() + 3, GetTextBorderWidth() + 2);
		return total;
	}

	virtual wxRect GetTextClientArea(const wxRect& rect, wxCoord* extraSpaceBeyond) const override {
		wxRect client = rect;
		client.Deflate(GetTextBorderWidth() + 3, GetTextBorderWidth() + 2);

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

	// a field's frame: the palette border, the accent's focused (DrawBorder); one that cannot be typed into - disabled, or
	// read-only (ibTextCtrl::DrawBorder) - has no well, the surface under it shows (ibTextCtrl::HasContentBackground), and
	// its frame is paler: the border most of the way to white (2026-11-13, Max: variant E3 - a field shut to typing could
	// not be told from one open to it). On GTK3/Cocoa the same - the field is the engine's there too
	virtual void DrawTextBorder(wxDC& dc, wxBorder border, const wxRect& rectTotal,
		int flags = 0, wxRect* rectIn = nullptr) override {
		if (border != wxBORDER_NONE && border != wxBORDER_DEFAULT
				&& (flags & wxCONTROL_DISABLED) && !(flags & wxCONTROL_FOCUSED)) {
			wxRect rect = rectTotal;
			DrawRect(dc, &rect, wxPen(kBorder.ChangeLightness(140)));
			if (rectIn != nullptr)
				*rectIn = rect;
			return;
		}
		DrawBorder(dc, border, rectTotal, flags, rectIn);
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

	// the scroll bar is variant D of the four drawn on Max's screenshot (2026-11-13): a near-white track, grey chevrons
	// for the arrows, a rounded thumb without a border, narrower than the bar - the black triangles on white buttons
	// and the bordered thumb were the darkest thing in a pane. The arrows stay: a line at a time by the mouse. The same
	// on every port: the bar is ibScrollBar's, drawn by the theme
	virtual void DrawScrollbarArrow(wxDC& dc, wxDirection dir, const wxRect& rect,
		int flags = 0) override {
		DrawSolidRect(dc, m_scheme->Get(ibColourScheme::SCROLLBAR), rect);
		const wxColour col = (flags & wxCONTROL_DISABLED) ? kBorder : kBorder.ChangeLightness(75);

		// a chevron: 0.22 of the button each way across, half of that each way along, the stroke a tenth of it
		const double size = wxMin(rect.width, rect.height);
		const double cx = rect.x + rect.width / 2.0, cy = rect.y + rect.height / 2.0;
		const double across = size * 0.22, along = across / 2;
		wxPoint2DDouble p[3];
		switch (dir) {
			case wxUP:
				p[0] = wxPoint2DDouble(cx - across, cy + along);
				p[1] = wxPoint2DDouble(cx, cy - along);
				p[2] = wxPoint2DDouble(cx + across, cy + along);
				break;
			case wxDOWN:
				p[0] = wxPoint2DDouble(cx - across, cy - along);
				p[1] = wxPoint2DDouble(cx, cy + along);
				p[2] = wxPoint2DDouble(cx + across, cy - along);
				break;
			case wxLEFT:
				p[0] = wxPoint2DDouble(cx + along, cy - across);
				p[1] = wxPoint2DDouble(cx - along, cy);
				p[2] = wxPoint2DDouble(cx + along, cy + across);
				break;
			default: // wxRIGHT
				p[0] = wxPoint2DDouble(cx - along, cy - across);
				p[1] = wxPoint2DDouble(cx + along, cy);
				p[2] = wxPoint2DDouble(cx - along, cy + across);
				break;
		}

		wxGraphicsContext* gc = wxGraphicsContext::CreateFromUnknownDC(dc);
		if (gc != nullptr) {
			gc->SetPen(gc->CreatePen(wxGraphicsPenInfo(col).Width(size / 10)));
			gc->StrokeLines(3, p);
			delete gc;
			return;
		}

		// fallback: a plain integer polyline
		wxPoint pi[3];
		for (int i = 0; i < 3; i++)
			pi[i] = wxPoint(wxRound(p[i].m_x), wxRound(p[i].m_y));
		dc.SetPen(wxPen(col));
		dc.DrawLines(3, pi);
	}

	// the thumb on the track: inset 3 of the bar's 16 across and 2 along, the ends round; darker under the mouse, the
	// border's colour while dragged
	virtual void DrawScrollbarThumb(wxDC& dc, wxOrientation orient,
		const wxRect& rect, int flags = 0) override {
		wxColour face = kBorder.ChangeLightness(130);
		if (flags & wxCONTROL_PRESSED)
			face = m_scheme->Get(ibColourScheme::SCROLLBAR_PRESSED);
		else if (flags & wxCONTROL_CURRENT)
			face = kBorder.ChangeLightness(115);
		DrawSolidRect(dc, m_scheme->Get(ibColourScheme::SCROLLBAR), rect);

		const wxCoord across = (orient == wxVERTICAL ? rect.width : rect.height) * 3 / 16, along = across * 2 / 3;
		const wxRect thumb = (orient == wxVERTICAL) ? rect.Deflate(across, along) : rect.Deflate(along, across);
		const double radius = wxMin(thumb.width, thumb.height) / 2.0;
		wxGraphicsContext* gc = wxGraphicsContext::CreateFromUnknownDC(dc);
		if (gc != nullptr) {
			gc->SetPen(*wxTRANSPARENT_PEN);
			gc->SetBrush(wxBrush(face));
			gc->DrawRoundedRectangle(thumb.x, thumb.y, thumb.width, thumb.height, radius);
			delete gc;
			return;
		}

		dc.SetPen(*wxTRANSPARENT_PEN);
		dc.SetBrush(wxBrush(face));
		dc.DrawRoundedRectangle(thumb, radius);
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

	// the corner between a window's two bars is their track
	virtual void DrawScrollCorner(wxDC& dc, const wxRect& rect) override {
		DrawSolidRect(dc, m_scheme->Get(ibColourScheme::SCROLLBAR), rect);
	}

	// a bar lit from the top: lighter than the surface it stands on at the top, darker at the bottom, a pale palette
	// border under it. Gone with the Luna tool bar art (wxAuiLunaToolBarArt::DrawBackground, lightness 150 to 90), the
	// bars melted into the dock (2026-11-12, Max: "the toolbar should stand out a little, with a gradient, as on our
	// site" - the web client's command bar is lit the same way, form.css); on a form's ground, a step below white, the
	// art's gradient melted too, and a form's bar went flat - the control colour, the border under it. Two bars, one
	// unlike the usual: met halfway, every bar - 125 to 95, the border at 125 (2026-11-13, Max: "meet in the middle,
	// subtler", variant B). Lit from the chrome, not from the surface under it: from the surface every bar came out its
	// own - the one under the menu the chrome's, a form's cream, a table's white (2026-11-13, Max: "one for all"). On
	// GTK3/Cocoa the same - the bars are the engine's there too
	virtual void DrawToolBarBackground(wxDC& dc, const wxColour& WXUNUSED(col), const wxRect& rect,
		int WXUNUSED(flags) = 0) override {
		const wxColour chrome = m_scheme->Get(ibColourScheme::FRAME);
		dc.GradientFillLinear(rect, chrome.ChangeLightness(125), chrome.ChangeLightness(95), wxSOUTH);
		dc.SetPen(wxPen(kBorder.ChangeLightness(125)));
		dc.DrawLine(rect.GetLeft(), rect.GetBottom(), rect.GetRight() + 1, rect.GetBottom());
	}

	virtual void DrawToolBarButton(wxDC& dc, const wxString& label,
		const wxBitmap& bitmap, const wxRect& rect, int flags = 0,
		long style = 0, int WXUNUSED(tbarStyle) = 0) override {
		// a separator is a line of the palette border across the bar, three quarters of it (the canon's dispatch on the
		// style: the line along the longer side)
		if (style == wxTOOL_STYLE_SEPARATOR) {
			dc.SetPen(wxPen(kBorder));
			if (rect.height > rect.width) {
				const wxCoord length = rect.height * 3 / 4;
				const wxCoord x = rect.x + rect.width / 2, y = rect.y + (rect.height - length) / 2;
				dc.DrawLine(x, y, x, y + length);
			}
			else {
				const wxCoord length = rect.width * 3 / 4;
				const wxCoord x = rect.x + (rect.width - length) / 2, y = rect.y + rect.height / 2;
				dc.DrawLine(x, y, x + length, y);
			}
			return;
		}

		// the bar's default command is a dialog's default button: the accent's face and border, the label white
		// (DrawButtonSurface, DrawButtonBorder, DrawButtonLabel) - a form's «… and close» stands out (2026-11-12, Max);
		// a disabled one is an ordinary disabled tool
		if ((flags & wxCONTROL_ISDEFAULT) && !(flags & wxCONTROL_DISABLED)) {
			DrawButtonSurface(dc, m_scheme->Get(ibColourScheme::CONTROL), rect, flags);
			DrawButtonBorder(dc, rect, flags);
		}
		// a checked tool sits on a light accent pad, hover/press on a
		// chrome pad; both get the 1px palette border
		else if (flags & (wxCONTROL_PRESSED | wxCONTROL_CURRENT | wxCONTROL_CHECKED)) {
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
		// the bitmap and the label on it, the label of a disabled tool greyed (DrawButtonLabel) - dc.DrawLabel drew it
		// in the text colour whatever the state
		DrawButtonLabel(dc, label, bitmap, rect, flags, wxALIGN_CENTRE);
	}

	// a bar's handle: the palette border's dots in a line along it, down a horizontal bar's start, across a vertical's
	virtual void DrawGripper(wxDC& dc, const wxRect& rect, wxOrientation orient) override {
		const wxCoord dot = dc.FromDIP(2), step = dc.FromDIP(4);
		dc.SetPen(*wxTRANSPARENT_PEN);
		dc.SetBrush(wxBrush(kBorder));
		if (orient == wxHORIZONTAL) {
			const wxCoord x = rect.x + (rect.width - dot) / 2;
			for (wxCoord y = rect.y + step; y + dot + step <= rect.y + rect.height; y += step)
				dc.DrawRectangle(x, y, dot, dot);
		}
		else {
			const wxCoord y = rect.y + (rect.height - dot) / 2;
			for (wxCoord x = rect.x + step; x + dot + step <= rect.x + rect.width; x += step)
				dc.DrawRectangle(x, y, dot, dot);
		}
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
		// turned the other way first, so it stands upright. The canon's road (DrawTab of wx's src/univ/themes/win32.cpp); drawn across,
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

	// AN AUI BOOK'S ROW (ibAuiTabContainer) is the palette's own, not a book's tab above (the art's, theme/luna_tabart,
	// until 2026-11-11): the light dusty strip, and along the pages a warm shelf the selected tab stands on - its ends
	// past the row's, so only its long sides show
	virtual void DrawTabStrip(wxDC& dc, const wxRect& rect, wxDirection dir) override {
		const bool bottom = dir == wxBOTTOM;
		dc.SetPen(*wxTRANSPARENT_PEN);
		dc.SetBrush(wxBrush(kChromeLight));
		dc.DrawRectangle(rect.x, rect.y, rect.width, bottom ? rect.height : rect.height - 3);

		dc.SetPen(wxPen(kBorder));
		dc.SetBrush(wxBrush(kShelf));
		dc.DrawRectangle(rect.x - 1, bottom ? rect.y : rect.y + rect.height - 4, rect.width + 2, 4);
	}

	// the selected tab is the pages' colour, its corners rounded off by a dot of the strip and its foot one with the shelf
	// (a tab of a row under the pages: its head, a lighter line); the strip shows through the others
	virtual void DrawStripTab(wxDC& dc, const wxRect& rect, wxDirection dir, int flags = 0) override {
		if (!(flags & wxCONTROL_SELECTED))
			return;

		dc.SetPen(wxPen(kContent));
		dc.SetBrush(wxBrush(kContent));
		if (dir == wxBOTTOM) {
			dc.DrawRectangle(rect.x - 2, rect.y - 1, rect.width + 3, rect.height + 4);
			dc.SetPen(wxPen(kChromeLight.ChangeLightness(170)));
			dc.DrawLine(rect.x + 1, rect.y, rect.x + rect.width, rect.y);
			return;
		}

		dc.DrawRectangle(rect.x + 2, rect.y + 1, rect.width - 3, rect.height - 4);
		dc.SetPen(wxPen(kChromeLight));
		dc.DrawPoint(rect.x + 2, rect.y + 1);
		dc.DrawPoint(rect.x + rect.width - 2, rect.y + 1);
		dc.SetPen(wxPen(kShelf));
		dc.DrawLine(rect.x + 1, rect.y + rect.height - 4, rect.x + rect.width, rect.y + rect.height - 4);
	}

	// the glyph black, grey while it cannot be pressed - and a tab's (flat) until the mouse is over it
	virtual void DrawStripButton(wxDC& dc, const wxRect& rect, int button, int flags = 0) override {
		const unsigned char* bits = nullptr;
		switch (button) {
			case ibAUI_BUTTON_CLOSE:      bits = kStripCloseBits; break;
			case ibAUI_BUTTON_LEFT:       bits = kStripLeftBits;  break;
			case ibAUI_BUTTON_RIGHT:      bits = kStripRightBits; break;
			case ibAUI_BUTTON_WINDOWLIST: bits = kStripListBits;  break;
			default:                      return;
		}

		const bool faint = (flags & wxCONTROL_DISABLED) != 0
			|| ((flags & wxCONTROL_FLAT) && !(flags & (wxCONTROL_CURRENT | wxCONTROL_PRESSED)));
		const wxRect glyph = wxRect(wxSize(AUI_GLYPH_SIZE, AUI_GLYPH_SIZE)).CentreIn(rect);
		dc.DrawBitmap(ibAuiBitmapFromBits(bits, AUI_GLYPH_SIZE, AUI_GLYPH_SIZE,
			faint ? wxColour(128, 128, 128) : *wxBLACK), glyph.GetPosition(), true);
	}

	// A PANE'S CAPTION IN THE DOCKING (ibAuiManager) is the palette's, as the frame's title bar is (the art's,
	// theme/luna_dockart, until 2026-11-11): light under its blue title, the active pane's dusty blue under its white one
	virtual void DrawPaneCaption(wxDC& dc, const wxRect& rect, const wxString& title, const wxBitmap& icon,
		int flags = 0) override {
		const bool active = (flags & wxCONTROL_FOCUSED) != 0;
		dc.GradientFillLinear(rect, active ? kCaptionBottom : kCaptionInactiveTop,
			active ? kCaptionTop : kCaptionInactiveBottom, wxSOUTH);

		wxCoord x = rect.x + 3;
		if (icon.IsOk()) {
			const wxSize size = icon.GetLogicalSize();
			dc.DrawBitmap(icon, rect.x + 2, rect.y + (rect.height - size.y) / 2, true);
			x += size.x + 3;
		}

		// the title cut with an ellipsis two pixels short of the piece's end - the buttons stand past it
		const wxCoord room = rect.x + rect.width - 2 - x;
		if (title.empty() || room <= 0)
			return;
		dc.SetFont(m_paneCaptionFont);
		dc.SetTextForeground(active ? *wxWHITE : kCaptionBottom);
		const wxCoord height = dc.GetTextExtent(wxT("ABCDEFHXfgkj")).y;
		dc.DrawText(wxControl::Ellipsize(title, dc, wxELLIPSIZE_END, room, wxELLIPSIZE_FLAGS_NONE),
			x, rect.y + rect.height / 2 - height / 2 - 1);
	}

	// a caption's button: the glyph in the title's colour, on a lighter pad of the caption while the mouse is over it or
	// presses it - pressed, a pixel down and right
	virtual void DrawPaneButton(wxDC& dc, const wxRect& rect, int button, int flags = 0) override {
		const unsigned char* bits = nullptr;
		switch (button) {
			case ibAUI_BUTTON_CLOSE:            bits = kPaneCloseBits; break;
			case ibAUI_BUTTON_PIN:              bits = kPanePinBits; break;
			case ibAUI_BUTTON_MAXIMIZE_RESTORE: bits = (flags & wxCONTROL_CHECKED) ? kPaneRestoreBits : kPaneMaximizeBits; break;
			default:                            return;
		}

		const bool active = (flags & wxCONTROL_FOCUSED) != 0;
		wxPoint glyph(rect.x, rect.y + rect.height / 2 - AUI_GLYPH_SIZE / 2);
		if (flags & wxCONTROL_PRESSED)
			glyph += wxPoint(1, 1);
		if (flags & (wxCONTROL_CURRENT | wxCONTROL_PRESSED)) {
			const wxColour caption = active ? kCaptionTop : kCaptionInactiveTop;
			dc.SetPen(wxPen(caption.ChangeLightness(70)));
			dc.SetBrush(wxBrush(caption.ChangeLightness(120)));
			dc.DrawRectangle(glyph.x, glyph.y, AUI_GLYPH_SIZE - 1, AUI_GLYPH_SIZE - 1);
		}
		dc.DrawBitmap(ibAuiBitmapFromBits(bits, AUI_GLYPH_SIZE, AUI_GLYPH_SIZE, active ? *wxWHITE : kCaptionBottom),
			glyph, true);
	}

	// the line round a pane: the palette border, its corners rounded
	virtual void DrawPaneBorder(wxDC& dc, const wxRect& rect) override {
		dc.SetPen(wxPen(kBorder));
		dc.SetBrush(*wxTRANSPARENT_BRUSH);
		dc.DrawRoundedRectangle(rect, 3);
	}

	// the bar between docks or panes: the docking's ground round the panes, their frames standing apart, and the grip
	// along its middle - a splitter's (ibSplitterWindow::DrawSash), a row of dots in the shadow. A bar of the border's
	// colour stood here, beside the panes' frames and their controls' of the same colour: one band of five to eight
	// pixels (2026-11-12, Max: "these lines look most wretched", variant B). On GTK3/Cocoa the same - the docking is the
	// engine's there too
	virtual void DrawSash(wxDC& dc, const wxRect& rect, wxOrientation orient) override {
		dc.SetPen(*wxTRANSPARENT_PEN);
		dc.SetBrush(wxBrush(m_scheme->Get(ibColourScheme::DESKTOP)));
		dc.DrawRectangle(rect);

		const int dot = dc.FromDIP(2), step = dc.FromDIP(4), dots = 5;
		const wxPoint centre(rect.x + rect.width / 2, rect.y + rect.height / 2);
		dc.SetBrush(wxBrush(m_scheme->Get(ibColourScheme::SHADOW_DARK)));
		for (int i = 0; i < dots; ++i) {
			const int along = (i - dots / 2) * step - dot / 2;
			if (orient == wxVERTICAL)
				dc.DrawRectangle(centre.x - dot / 2, centre.y + along, dot, dot);
			else
				dc.DrawRectangle(centre.x + along, centre.y - dot / 2, dot, dot);
		}
	}

	// the free part of a row of tool bars: the palette's inactive caption pair, the menu's colour going a shade darker
	// down the row, both a little darker still. The docking's ground stood there, and the row melted into the ground under
	// it (2026-11-13, Max: "the toolbar's free part, colour it on its own", "closer to the menu", variant M2, then "a bit
	// darker"). On GTK3/Cocoa the same - the docking is the engine's there too
	virtual void DrawToolBarDockBackground(wxDC& dc, const wxRect& rect) override {
		dc.GradientFillLinear(rect, kCaptionInactiveTop.ChangeLightness(95), kCaptionInactiveBottom.ChangeLightness(95),
			wxSOUTH);
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
	// win32-private GetIndicator). A title of the bar under the mouse or open sits on the pad a tool under the mouse has
	// (DrawToolBarButton): the chrome's light face and the palette border, its corners rounded, the text as it is; an
	// item of the menu on a rounded pad of the accent, the text white - a selection, as a tree's and the inspector's
	// (2026-11-13, Max: "the menu wants combing", then "terracotta inside, when selecting" - the light pad there too
	// for one build; the accent's square bar stood before). On GTK3/Cocoa the same - the menus are the engine's there too
	virtual void DrawMenuBarItem(wxDC& dc, const wxRect& rectOrig,
		const wxString& label, int flags = 0, int indexAccel = -1) override {
		wxRect rect = rectOrig;
		rect.height--;

		if (flags & wxCONTROL_SELECTED) {
			dc.SetPen(wxPen(kBorder));
			dc.SetBrush(wxBrush(m_scheme->Get(ibColourScheme::CONTROL_CURRENT)));
			dc.DrawRoundedRectangle(wxRect(rect).Deflate(0, dc.FromDIP(2)), dc.FromDIP(3));
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

			const wxColour accent = m_scheme->Get(ibColourScheme::HIGHLIGHT);
			dc.SetPen(wxPen(accent));
			dc.SetBrush(wxBrush(accent));
			dc.DrawRoundedRectangle(rect, dc.FromDIP(3));
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

		// NB: no accel index here. Quieter than the label, as the welcome's keys are (ibWelcomeWindow): the alternate
		// text lightened - it stood in the label's black; on the selection white, as the label
		wxDCTextColourChanger accelColour(dc);
		if (!(flags & wxCONTROL_SELECTED))
			accelColour.Set(m_scheme->Get(ibColourScheme::CONTROL_TEXT_ALTERNATE).ChangeLightness(115));
		DrawLabel(dc, accel, rect, flags, wxALIGN_CENTRE_VERTICAL);

		// draw the submenu indicator
		if (flags & wxCONTROL_ISSUBMENU) {
			// a small arrow in the middle of the margin, about 4 by 7 at an item 22 high: DrawArrow sizes the triangle by
			// its box, and the margin's whole box made it a wedge as high as the text (2026-11-12, Max: "neater arrows").
			// The box half the item's height, even - the tip and the base on whole pixels
			const wxCoord side = rect.height / 4 * 2;
			const wxRect box(geometryInfo.GetSize().x - MENU_SIDE_MARGIN - MENU_RIGHT_MARGIN + (MENU_RIGHT_MARGIN - side) / 2,
				rect.y + (rect.height - side) / 2, side, side);

			DrawArrow(dc, wxRIGHT, box, flags);
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

	// a row of a list, a tree or the property grid: its text and the standard text's air, half the height of the
	// window's own font - one rule for every list, so a list in a larger font is no freer than one in the window's
	// (2026-11-13, Max chose "the text + the air": the launcher's list two points over would have had rows of 30
	// pixels). Until then half the row's own text again, variant C of four on his screenshot - 22 pixels for the
	// window's font, as now. The standard text is measured on the screen: its height carries the platform's font and
	// the screen's DPI with it; on GTK3/Cocoa the same
	virtual wxCoord GetListboxItemHeight(wxCoord fontHeight) override {
		wxScreenDC dc;
		dc.SetFont(wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT));
		return fontHeight + dc.GetCharHeight() / 2;
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

	wxFont m_paneCaptionFont;   // a pane caption's title (DrawPaneCaption)
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

// The corner of a resizable window's client area that sizes it - the square the native size grip takes, a scroll bar's
// corner box (SBS_SIZEGRIP): the window paints that corner again where it stood as it is sized (ibTopLevelWindow::OnSize)
static wxRect GetFrameGripRect(const wxRect& client, const wxSize& side) {
	return wxRect(client.GetRight() + 1 - side.x, client.GetBottom() + 1 - side.y, side.x, side.y);
}

int ibLunaRenderer::HitTestFrame(const wxRect& rect, const wxPoint& pt, int flags) const {
	const int hit = ibThemeRenderer::HitTestFrame(rect, pt, flags);
	if (hit == wxHT_TOPLEVEL_CLIENT_AREA && (flags & wxTOPLEVEL_RESIZEABLE) && !(flags & wxTOPLEVEL_MAXIMIZED)
			&& GetFrameGripRect(GetFrameClientArea(rect, flags), GetScrollbarArrowSize()).Contains(pt))
		return wxHT_TOPLEVEL_BORDER_SE;
	return hit;
}

void ibLunaRenderer::DrawFrameBorder(wxDC& dc, const wxRect& rect, int flags) {
	if (flags & wxTOPLEVEL_BORDER)
		DrawBorder(dc, wxBORDER_RAISED, rect);

	// the corner's mark: three strokes across it, as the native grip has
	if ((flags & wxTOPLEVEL_RESIZEABLE) && !(flags & wxTOPLEVEL_MAXIMIZED)) {
		const wxRect grip = GetFrameGripRect(GetFrameClientArea(rect, flags), GetScrollbarArrowSize());
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

	// the palette's captions, the panes' (DrawPaneCaption): the active one dusty blue under its white title, the inactive
	// one light under its blue title. The system's caption colour lightening to the right was a pale blue the white
	// title hardly showed on (Max, 2026-11-07), and the platform's, not the theme's
	const bool active = (flags & wxTOPLEVEL_ACTIVE) != 0;
	dc.GradientFillLinear(band, active ? kCaptionTop : kCaptionInactiveTop,
		active ? kCaptionBottom : kCaptionInactiveBottom, wxSOUTH);
}

void ibLunaRenderer::DrawFrameButton(wxDC& dc, wxCoord x, wxCoord y, int button, int flags) {
	const wxRect face(x, y, FRAME_BUTTON_WIDTH, FRAME_BUTTON_HEIGHT);

	// the help button: a face only while pressed, its glyph in the title's colour - DrawFrameTitle sets it just before
	// the buttons: white on the active caption, blue on the inactive one
	if (button == wxTOPLEVEL_BUTTON_HELP) {
		if (flags & wxCONTROL_PRESSED) {
			dc.SetPen(*wxTRANSPARENT_PEN);
			dc.SetBrush(wxBrush(kBorder));
			dc.DrawRectangle(face);
		}
		dc.DrawLabel(wxT("?"), face, wxALIGN_CENTRE);
		return;
	}

	// the others the Mac's, on every platform: a disc of 12 pixels - red, yellow, green - darker while pressed, its glyph
	// a deep shade of it (2026-11-13, Max: "the Mac's colours, only with our theme"; the side is the base's,
	// ibTHEME_FRAME_BUTTONS_LEFT)
	wxColour colour = button == wxTOPLEVEL_BUTTON_CLOSE ? kFrameClose
		: button == wxTOPLEVEL_BUTTON_ICONIZE ? kFrameIconize : kFrameMaximize;
	if (flags & wxCONTROL_PRESSED)
		colour = colour.ChangeLightness(80);
	const int diameter = 12;
	const wxRect disc(face.x + (face.width - diameter) / 2, face.y + (face.height - diameter) / 2, diameter, diameter);
	wxGraphicsContext* gc = wxGraphicsContext::CreateFromUnknownDC(dc);
	if (gc != nullptr) {
		gc->SetPen(*wxTRANSPARENT_PEN);
		gc->SetBrush(wxBrush(colour));
		gc->DrawEllipse(disc.x, disc.y, disc.width, disc.height);
		delete gc;
	}
	else {
		dc.SetPen(*wxTRANSPARENT_PEN);
		dc.SetBrush(wxBrush(colour));
		dc.DrawEllipse(disc);
	}

	// the glyph in a square of 6 pixels in the middle
	const int side = 6;
	const wxRect glyph(face.x + (face.width - side) / 2, face.y + (face.height - side) / 2, side, side);
	dc.SetPen(wxPen(colour.ChangeLightness(40)));
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
		// across the middle, as the Mac's
		dc.DrawLine(glyph.GetLeft(), glyph.y + side / 2, glyph.GetRight() + 1, glyph.y + side / 2);
		break;
	}
}

int ibLunaRenderer::GetFrameBorderWidth(int flags) const {
	// the border's line, and inside it a resizable window's grip
	const int line = GetBorderDimensions(wxBORDER_RAISED).x;
	return (flags & wxTOPLEVEL_RESIZEABLE) ? line + 4 : line;
}

// ----------------------------------------------------------------------------
// ibLunaArtProvider
// ----------------------------------------------------------------------------

// EVERY PICTURE HERE IS A PNG IN BASE64, one string each in private/picturePredefined.h: a master four times the size of
// its drawing, scaled down here to the size asked. Its source is an SVG in tools/pictures/render.js, which draws the PNG
// and writes the string; the groups are the debugger, the code editor and its autocomplete, the form designer, the
// metadata tree, the service panes and the output window, the spreadsheet, the list commands and the query constructor,
// and wx's own stock ids.

// A master's PNG read from its Base64 - none when the text is no PNG.
static wxImage ImageOfBase64(const wxString& png)
{
	ibMemoryBuffer data;
	if (!ibBase64::Read(png, data))
		return wxImage();
	if (wxImage::FindHandler(wxBITMAP_TYPE_PNG) == nullptr)
		wxImage::AddHandler(new wxPNGHandler());
	wxMemoryInputStream stream(data.GetData(), data.GetDataLen());
	return wxImage(stream, wxBITMAP_TYPE_PNG);
}

// THE BUNDLE CARRIES THE MASTER AT THE SIZE ASKED AND AT TWICE AND FOUR TIMES IT, so a display scaled to 150 or
// 200 % is handed a picture drawn for it rather than the small one stretched - never larger than the master,
// which would only be stretched in its turn.
static wxBitmapBundle BundleOf(const wxString& png, const wxSize& size)
{
	const wxImage master = ImageOfBase64(png);
	if (!master.IsOk())
		return wxBitmapBundle();

	wxVector<wxBitmap> bitmaps;
	for (const int times : { 1, 2, 4 }) {
		const wxSize at(size.x * times, size.y * times);
		if (times > 1 && (at.x > master.GetWidth() || at.y > master.GetHeight()))
			break;
		bitmaps.push_back(wxBitmap(at == master.GetSize() ? master : master.Scale(at.x, at.y, wxIMAGE_QUALITY_HIGH)));
	}
	return wxBitmapBundle::FromBitmaps(bitmaps);
}

// THE THEME'S PICTURES (the canon's wxWin32ArtProvider): the product's own by their client and id (deskgui/artProvider.h)
// and wx's stock ids for every client - pushed over wx's own as the theme is set (ibThemeEngine::Set).
class ibLunaArtProvider : public wxArtProvider {
protected:

	virtual wxBitmapBundle CreateBitmapBundle(
		const wxArtID& id,
		const wxArtClient& client,
		const wxSize& size) override {

		// The pictures: a PNG master in Base64, scaled down to the size asked for (16x16 when none is).
		static const struct { wxArtClient client; wxArtID id; const wxString& png; } s_allPictures[] =
		{
			// ******* wxART_VISUALHOST *******
			{ wxART_VISUALHOST, wxART_NO_PICTURE, s_null_png },

			// ******* wxART_FRONTEND ******* - moving a row is the stock arrow's act, so it is the stock arrow
			// (wxART_EDIT and wxART_DELETE are stock ids themselves and are answered by the stock table below)
			{ wxART_FRONTEND, wxART_ADD, s_add_png },
			{ wxART_FRONTEND, wxART_UP, s_goUp_png },
			{ wxART_FRONTEND, wxART_DOWN, s_goDown_png },
			{ wxART_FRONTEND, wxART_SORT, s_sort_png },
			{ wxART_FRONTEND, wxART_DATABASE, s_db_png },
			{ wxART_FRONTEND, wxART_DATABASE_APPLY, s_dbApply_png },
			{ wxART_FRONTEND, wxART_DATABASE_ROOLBACK, s_dbRollback_png },
			{ wxART_FRONTEND, wxART_QUERY_CONSTRUCTOR, s_queryConstructor_png },
			{ wxART_FRONTEND, wxART_TEMP_TABLE, s_tempTable_png },
			{ wxART_FRONTEND, wxART_NESTED_QUERY, s_nestedQuery_png },
			{ wxART_FRONTEND, wxART_ADD_QUERY, s_addQuery_png },
			{ wxART_FRONTEND, wxART_ADD_NESTED, s_addNested_png },
			{ wxART_FRONTEND, wxART_EDIT_NESTED, s_editNested_png },
			{ wxART_FRONTEND, wxART_REPLACE_TABLE, s_replaceTable_png },
			{ wxART_FRONTEND, wxART_ADD_EXPRESSION, s_addExpression_png },
			{ wxART_FRONTEND, wxART_ADD_LINK, s_addLink_png },
			{ wxART_FRONTEND, wxART_LINK_CONDITION, s_linkCondition_png },
			{ wxART_FRONTEND, wxART_ADD_CONDITION, s_addCondition_png },
			{ wxART_FRONTEND, wxART_DIRECTION, s_direction_png },
			{ wxART_FRONTEND, wxART_ADD_SPLIT, s_addSplit_png },
			{ wxART_FRONTEND, wxART_ADD_BRANCH, s_addBranch_png },
			{ wxART_FRONTEND, wxART_ADD_JOIN, s_addJoin_png },
			{ wxART_FRONTEND, wxART_COUNT_ROWS, s_countRows_png },
			{ wxART_FRONTEND, wxART_TOTALS, s_totals_png },
			{ wxART_FRONTEND, wxART_PARAMETERS, s_parameters_png },
			{ wxART_FRONTEND, wxART_OUTPUT, s_output_png },
			{ wxART_FRONTEND, wxART_SELECTED_FIELDS, s_selectedFields_png },
			{ wxART_FRONTEND, wxART_FILTER, s_filter_png },
			{ wxART_FRONTEND, wxART_GROUPING, s_grouping_png },
			{ wxART_FRONTEND, wxART_TABLE, s_table_png },
			{ wxART_FRONTEND, wxART_LINKS, s_links_png },
			{ wxART_FRONTEND, wxART_SELECTION_LINKS, s_selectionLinks_png },
			{ wxART_FRONTEND, wxART_ADVANCED, s_advanced_png },
			{ wxART_FRONTEND, wxART_UNIONS, s_unions_png },
			{ wxART_FRONTEND, wxART_INDEX, s_index_png },
			{ wxART_FRONTEND, wxART_QUERY_BATCH, s_queryBatch_png },
			{ wxART_FRONTEND, wxART_TRANSLATION_CONSTRUCTOR, s_translationConstructor_png },
			{ wxART_FRONTEND, wxART_FORMAT_CONSTRUCTOR, s_formatConstructor_png },
			{ wxART_FRONTEND, wxART_LINQ_CONSTRUCTOR, s_linqConstructor_png },
			{ wxART_DEBUG, wxART_BREAKPOINT_CONDITION, s_breakpointCondition_png },
			{ wxART_DOC_MODULE, wxART_SELECT_ALL, s_selectAll_png },

			// ******* wxART_DOC_FORM *******
			{ wxART_DOC_FORM, wxART_DESIGNER_PAGE, s_designerPage_png },
			{ wxART_DOC_FORM, wxART_CODE_PAGE, s_codePage_png },

			// ******* wxART_DOC_TEMPLATE *******
			{ wxART_DOC_TEMPLATE, wxART_MERGE_CELL, s_mergeCells_png },
			{ wxART_DOC_TEMPLATE, wxART_ADD_SECTION, s_addSection_png },
			{ wxART_DOC_TEMPLATE, wxART_REMOVE_SECTION, s_removeSection_png },
			{ wxART_DOC_TEMPLATE, wxART_SHOW_CELL, s_showCells_png },
			{ wxART_DOC_TEMPLATE, wxART_SHOW_HEADER, s_showHeaders_png },
			{ wxART_DOC_TEMPLATE, wxART_SHOW_SECTION, s_showSections_png },
			{ wxART_DOC_TEMPLATE, wxART_BORDER, s_borders_png },

			// ******* wxART_SERVICE *******
			{ wxART_SERVICE, wxART_MESSAGE, s_message_png },
			{ wxART_SERVICE, wxART_LOCAL_VARIABLE, s_variables_png },
			{ wxART_SERVICE, wxART_STACK, s_stack_png },
			{ wxART_SERVICE, wxART_WATCH, s_watch_png },
			{ wxART_SERVICE, wxART_PROPERTY, s_property_png },

			// ******* wxART_METATREE *******
			{ wxART_METATREE, wxART_COMMON_FOLDER, s_commonFolder_png },
			{ wxART_METATREE, wxART_SAVE_METADATA, s_saveMetadata_png },

			// ******* wxART_DEBUG *******
			{ wxART_DEBUG, wxART_DEBUG_START, s_start_png },
			{ wxART_DEBUG, wxART_DEBUG_START_WITHOUT_DEBUGGING, s_startWithoutDebugging_png },
			{ wxART_DEBUG, wxART_DEBUG_ATTACH, s_attach_png },
			{ wxART_DEBUG, wxART_DEBUG_CONTINUE, s_continue_png },
			{ wxART_DEBUG, wxART_DEBUG_PAUSE, s_pause_png },
			{ wxART_DEBUG, wxART_DEBUG_STEP_INTO, s_stepInto_png },
			{ wxART_DEBUG, wxART_DEBUG_STEP_OVER, s_stepOver_png },
			{ wxART_DEBUG, wxART_DEBUG_STEP_OUT, s_stepOut_png },
			{ wxART_DEBUG, wxART_DEBUG_STOP_DEBUGGING, s_stopDebugging_png },
			{ wxART_DEBUG, wxART_DEBUG_STOP_PROGRAM, s_stopProgram_png },
			{ wxART_DEBUG, wxART_DEBUG_REMOVE_ALL_BREAKPOINTS, s_removeAllBreakpoints_png },

			// ******* wxART_DOC_MODULE *******
			{ wxART_DOC_MODULE, wxART_ADD_COMMENT, s_addComment_png },
			{ wxART_DOC_MODULE, wxART_REMOVE_COMMENT, s_removeComment_png },
			{ wxART_DOC_MODULE, wxART_SYNTAX_CONTROL, s_syntaxControl_png },
			{ wxART_DOC_MODULE, wxART_GOTO_LINE, s_gotoLine_png },
			{ wxART_DOC_MODULE, wxART_PROC_AND_FUNC, s_proceduresFunctions_png },
			{ wxART_DOC_MODULE, wxART_FORMAT_CODE, s_formatCode_png },
			{ wxART_DOC_MODULE, wxART_INCREASE_INDENT, s_increaseIndent_png },
			{ wxART_DOC_MODULE, wxART_DECREASE_INDENT, s_decreaseIndent_png },

			// ******* wxART_AUTOCOMPLETE *******
			{ wxART_AUTOCOMPLETE, wxART_FUNCTION, s_function_png },
			{ wxART_AUTOCOMPLETE, wxART_PROCEDURE, s_procedure_png },
			{ wxART_AUTOCOMPLETE, wxART_VARIABLE, s_variable_png },
			{ wxART_AUTOCOMPLETE, wxART_VARIABLE_ALTERNATIVE, s_variableAlt_png },

			// ******* wxART_SERVICE *******
			{ wxART_SERVICE, wxART_OUTPUT_INFORMATION, s_outputInformation_png },
			{ wxART_SERVICE, wxART_OUTPUT_WARNING, s_outputWarning_png },
			{ wxART_SERVICE, wxART_OUTPUT_ERROR, s_outputError_png }
		};

		for (const auto& entry : s_allPictures) {
			if (entry.client == client && entry.id == id)
				return BundleOf(entry.png, size.IsFullySpecified() ? size : wxSize(16, 16));
		}

		// ⭐ WX'S OWN PICTURES, FOR EVERY CLIENT. This provider stands first (pushed over wx's), so a stock id asked
		// anywhere - a menu, a toolbar, a button, a list, a message window - gets this drawing instead of Tango's
		// or an XPM of wx's, and not one call site changes.
		static const struct { wxArtID id; const wxString& png; } s_stockPictures[] =
		{
			{ wxART_DELETE, s_delete_png },
			{ wxART_NEW, s_new_png },
			{ wxART_EDIT, s_edit_png },
			{ wxART_GO_UP, s_goUp_png },
			{ wxART_GO_DOWN, s_goDown_png },
			{ wxART_GO_BACK, s_goBack_png },
			{ wxART_GO_FORWARD, s_goForward_png },
			{ wxART_GO_DIR_UP, s_goDirUp_png },
			{ wxART_COPY, s_copy_png },
			{ wxART_CUT, s_cut_png },
			{ wxART_PASTE, s_paste_png },
			{ wxART_UNDO, s_undo_png },
			{ wxART_REDO, s_redo_png },
			{ wxART_FIND, s_find_png },
			{ wxART_PLUS, s_plus_png },
			{ wxART_MINUS, s_minus_png },
			{ wxART_FILE_OPEN, s_fileOpen_png },
			{ wxART_FILE_SAVE, s_fileSave_png },
			{ wxART_FILE_SAVE_AS, s_fileSaveAs_png },
			{ wxART_NORMAL_FILE, s_normalFile_png },
			{ wxART_FOLDER, s_folder_png },
			{ wxART_LIST_VIEW, s_listView_png },
			{ wxART_REPORT_VIEW, s_reportView_png },
			{ wxART_FULL_SCREEN, s_fullScreen_png },
			{ wxART_HELP_BOOK, s_helpBook_png },
			{ wxART_HELP_SETTINGS, s_helpSettings_png },
			{ wxART_TICK_MARK, s_tickMark_png },
			{ wxART_STOP, s_stop_png },
			{ wxART_QUIT, s_quit_png },
			{ wxART_MISSING_IMAGE, s_missingImage_png },
			{ wxART_WARNING, s_warning_png },
			{ wxART_ERROR, s_error_png },
			{ wxART_INFORMATION, s_information_png },
			{ wxART_QUESTION, s_question_png },
			{ wxART_TIP, s_tip_png }
		};

		for (const auto& entry : s_stockPictures) {
			if (entry.id == id)
				return BundleOf(entry.png, size.IsFullySpecified() ? size : wxSize(16, 16));
		}

		return wxNullBitmap;
	}
};

// ----------------------------------------------------------------------------
// ibLunaTheme
// ----------------------------------------------------------------------------

class ibLunaTheme : public ibThemeEngine {
public:

	ibLunaTheme() {
		m_renderer = nullptr;
		m_artProvider = nullptr;
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
		if (m_artProvider == nullptr)
			m_artProvider = new ibLunaArtProvider;
		return m_artProvider;
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
	// pushed as the theme is set (ibThemeEngine::Set), wx's from then: deleted with the others as wx exits
	// (wxArtProvider::CleanUpProviders), not here
	ibLunaArtProvider* m_artProvider;

	WX_DECLARE_THEME(luna)
};

WX_IMPLEMENT_THEME(ibLunaTheme, luna, ibTRANSLATE("OES Luna theme"));
