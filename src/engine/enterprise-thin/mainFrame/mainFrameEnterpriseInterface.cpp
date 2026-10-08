#include "mainFrameEnterprise.h"

#include <functional>

#include <wx/artprov.h>
#include <wx/renderer.h>
#include <wx/popupwin.h>
#include <wx/statline.h>
#include <wx/statbmp.h>   // wxStaticBitmap — a declared command group's picture beside its heading
#include <wx/tglbtn.h>
#include <wx/hyperlink.h>
#include <wx/dcbuffer.h>

// Interior-design palette (see luna_dockart.cpp). Subsystem chrome
// uses the same powder-blue + dusty-border tones as the rest of the
// frontend so the home page sits in the same visual world as the
// document panes.
#define THEME_COLOUR_MAIN    wxColour(0xB8, 0xC9, 0xD4)  // #B8C9D4 powder blue
#define THEME_COLOUR_BORDER  wxColour(0xA8, 0xBA, 0xC8)  // #A8BAC8 light dusty

// The text of the panel's pages — the forms' own ink (frontend/visualView/ctrl/frame.h).
#define wxDefaultStypeFGColour wxColour(0x3F, 0x5C, 0x77)  // #3F5C77 deep dusty blue

#include "frmclient/win/picture.h"   // the pictures, as they travel

// THE SECTION PANEL — the desktop's (enterprise/mainFrame/mainFrameEnterpriseInterface.cpp) on the thin client: the
// same buttons, the same page in its popup, drawn line for line; what it shows is the server's schema Sections
// (frmserver/schema/runtime/sections.cpp) instead of the configuration read here, and a link opens its item through
// the schema's command — the window's call (ibFrontendMainFrameEnterprise::CreateSubSystem).

// A picture of the wire as an icon — what a link draws.
static wxIcon IconOf(const wxString& base64)
{
	wxIcon icon;
	const wxBitmap picture = ibProtocolPicture(base64);
	if (picture.IsOk())
		icon.CopyFromBitmap(picture);
	return icon;
}

class ibSubSystemWindow : public wxWindow {

	// ----------------------------------------------------------------------------
	// ibSubSystemButton: search button used by search control
	// ----------------------------------------------------------------------------

	class ibSubSystemButton : public wxControl {

		wxString ChopText(wxDC& dc, const wxString& text, int max_size) {

			wxCoord x, y;

			// first check if the text fits with no problems
			dc.GetMultiLineTextExtent(text, &x, &y);
			if (x <= max_size)
				return text;
			unsigned int i, len = text.Length();
			unsigned int last_good_length = 0;
			for (i = 0; i < len; ++i) {

				wxString s = text.Left(i);
				s += wxT("...");
				dc.GetMultiLineTextExtent(s, &x, &y);
				if (x > max_size)
					break;
				last_good_length = i;
			}

			wxString ret = text.Left(last_good_length);
			ret += wxT("...");
			return ret;
		}

	public:

		void SetPopupWindow(wxPopupTransientWindow* wnd) {

			m_popupWindow = wnd;

			if (!wnd) {
				m_mainWindow->m_activeButton = nullptr;

				// A press outside the open panel closes it (the transient window does that on the way
				// down), and the release that ends the same press then arrives at whatever is under the
				// pointer. When that is this button the panel would be shut and opened again in the same
				// click, so a person could never put a section away by clicking its button.
				if (wxGetMouseState().LeftIsDown())
					m_closedByPressAt = wxGetLocalTimeMillis();
			}

			ibSubSystemButton::Refresh();
			ibSubSystemButton::Update();
		}

		void DismissPopupWindow() {

			if (m_popupWindow != nullptr)
				m_popupWindow->Dismiss();
		}

		const ibProtocolNode& GetSection() const { return m_section; }
		ibSubSystemWindow* GetMainWindow() const { return m_mainWindow; }

		ibSubSystemButton(ibSubSystemWindow* mainWindow, wxWindowID id, const ibProtocolNode& section)
			: wxControl(mainWindow, id, wxDefaultPosition, wxDefaultSize, wxNO_BORDER),
			m_section(section), m_popupWindow(nullptr),
			m_mainWindow(mainWindow), m_bitmap(ibProtocolPicture(section.GetString(ibProtocolName::Picture))), m_eventType(wxEVT_BUTTON) {

			m_baseColour = wxSystemSettings::GetColour(wxSYS_COLOUR_3DFACE);
			m_highlightColour = wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHT);

			SetLabel(section.GetString(ibProtocolName::Title));

			m_normalFont = *wxNORMAL_FONT;
			m_selectedFont = *wxNORMAL_FONT;
			m_selectedFont.SetWeight(wxFONTWEIGHT_BOLD);
			m_measuringFont = m_selectedFont;

			// ⭐ NOT GREY (an accountant's review, 2026-09-22 — "too grey"; Max: "the selected one must be white, and a
			// section is not grey by default either", "whiter"). A section at rest is all but white with dark text,
			// lighter than the panel under it so it still reads as a button; the OPEN one is white and bold. It was
			// the dusty border colour darkened to 75 %, with white text — a slab of mid-grey down the window's side.
			m_activeColour = *wxWHITE;
			m_baseColour = wxColour(0xEE, 0xF3, 0xF6);   // #EEF3F6 all but white, a breath of blue

			m_borderPen = wxPen(THEME_COLOUR_MAIN);
			m_baseColourPen = wxPen(m_activeColour);
			m_baseColourBrush = wxBrush(THEME_COLOUR_MAIN);

			SetCursor(wxCURSOR_HAND);
			SetBackgroundStyle(wxBG_STYLE_PAINT);
			EnableVisibleFocus(false);   // AcceptsFocus() alone does not stop the native view drawing its ring
		}

		virtual wxWindow* GetMainWindowOfCompositeControl() override { return m_mainWindow; }

		// The section is shown by its own painting (bold, lighter fill when open); a focus ring on top of
		// that is macOS drawing the blue ring around a button that was clicked.
		bool AcceptsFocus() const override { return false; }

	protected:

		void OnLeftUp(wxMouseEvent& event) {
			const bool endsClosingPress = m_closedByPressAt != 0 && (wxGetLocalTimeMillis() - m_closedByPressAt) < 1500;
			m_closedByPressAt = 0;
			if (endsClosingPress)
				return;

			// Clicking the open section closes it. That click used to be the one case this
			// handler skipped, and the transient window does not dismiss itself here either,
			// so a panel could not be put away at all.
			if (m_popupWindow != nullptr) {
				DismissPopupWindow();
			}
			else {
				// The open panel belongs to the button that opened it, not to this one.
				if (m_mainWindow->m_activeButton != nullptr && m_mainWindow->m_activeButton != this)
					m_mainWindow->m_activeButton->DismissPopupWindow();

				wxCommandEvent open(m_eventType, m_mainWindow->GetId());

				open.SetEventObject(this);
				open.SetString(GetLabel());
				open.SetExtraLong(true);

				GetEventHandler()->ProcessEvent(open);
			}

			ibSubSystemButton::Refresh();
			ibSubSystemButton::Update();
		}

		void OnPaint(wxPaintEvent& e) {

			wxPaintDC dc(this);

			// Clear the background in case of a user bitmap with alpha channel
			dc.SetBrush(m_mainWindow->GetBackgroundColour());
			dc.Clear();

			wxCoord normal_textx, normal_texty;
			wxCoord selected_textx, selected_texty;
			wxCoord texty;

			// if the caption is empty, measure some temporary text
			wxString caption = m_labelOrig;
			if (caption.empty())
				caption = wxT("Xj");

			dc.SetFont(m_selectedFont);
			dc.GetMultiLineTextExtent(caption, &selected_textx, &selected_texty);

			dc.SetFont(m_normalFont);
			dc.GetMultiLineTextExtent(caption, &normal_textx, &normal_texty);

			// figure out the size of the tab
			wxSize tab_size = GetSize();

			wxCoord tab_height = tab_size.y;//tab_size.y -3;
			wxCoord tab_width = tab_size.x;

			wxRect in_rect = GetSize();//GetRect();

			wxCoord tab_x = in_rect.x;
			wxCoord tab_y = in_rect.y;// +in_rect.height - tab_height;

			bool active = m_popupWindow != nullptr;

			// select pen, brush and font for the tab to be drawn
			if (active) {
				dc.SetFont(m_selectedFont);
				texty = selected_texty;
			}
			else {
				dc.SetFont(m_normalFont);
				texty = normal_texty;
			}

			// create points that will make the tab outline
			int clip_width = tab_width;
			if (tab_x + clip_width > in_rect.x + in_rect.width)
				clip_width = (in_rect.x + in_rect.width) - tab_x;

			// since the above code above doesn't play well with WXDFB or WXCOCOA,
			// we'll just use a rectangle for the clipping region for now --		
			dc.SetClippingRegion(tab_x, tab_y, clip_width, tab_height);

			wxPoint border_points[6];
			if (m_flags & wxAUI_NB_BOTTOM) {
				border_points[0] = wxPoint(tab_x, tab_y);
				border_points[1] = wxPoint(tab_x, tab_y + tab_height - 6);
				border_points[2] = wxPoint(tab_x + 2, tab_y + tab_height - 4);
				border_points[3] = wxPoint(tab_x + tab_width - 2, tab_y + tab_height - 4);
				border_points[4] = wxPoint(tab_x + tab_width, tab_y + tab_height - 6);
				border_points[5] = wxPoint(tab_x + tab_width, tab_y);
			}
			else //if (m_flags & wxAUI_NB_TOP) {}
			{
				border_points[0] = wxPoint(tab_x, tab_y + tab_height);
				border_points[1] = wxPoint(tab_x, tab_y);
				border_points[2] = wxPoint(tab_x, tab_y);
				border_points[3] = wxPoint(tab_x + tab_width, tab_y);
				border_points[4] = wxPoint(tab_x + tab_width, tab_y);
				border_points[5] = wxPoint(tab_x + tab_width, tab_y + tab_height);

			}
			// TODO: else if (m_flags &wxAUI_NB_LEFT) {}
			// TODO: else if (m_flags &wxAUI_NB_RIGHT) {}

			int drawn_tab_yoff = border_points[1].y;
			int drawn_tab_height = border_points[0].y - border_points[1].y;

			wxColor back_color = m_baseColour;
			if (active) {

				// draw active tab
				// draw base background color
				wxRect r(tab_x, tab_y, tab_width, tab_height);
				dc.SetPen(wxPen(m_activeColour));
				dc.SetBrush(wxBrush(m_activeColour));
				dc.DrawRectangle(r.x, r.y, r.width, r.height);

				// this white helps fill out the gradient at the top of the tab
				wxColor gradient = m_activeColour;

				if (m_flags & wxAUI_NB_BOTTOM) {

					dc.SetPen(wxPen(gradient));
					dc.SetBrush(wxBrush(gradient));
					dc.DrawRectangle(r.x - 2, r.y - 1, r.width + 3, r.height + 4);

					// these two points help the rounded corners appear more antisynonymed
					dc.SetPen(wxPen(m_activeColour));

					dc.DrawPoint(r.x - 2, r.y - 1);
					dc.DrawPoint(r.x - r.width + 2, r.y - 1);
				}
				else {

					dc.SetPen(wxPen(gradient));
					dc.SetBrush(wxBrush(gradient));
					dc.DrawRectangle(r.x + 2, r.y + 1, r.width - 3, r.height - 5);

					// these two points help the rounded corners appear more antisynonymed
					dc.SetPen(wxPen(m_activeColour));


					dc.DrawPoint(r.x + 2, r.y + 1);
					dc.DrawPoint(r.x + r.width - 2, r.y + 1);

					dc.DrawPoint(r.x + 2, r.y + r.height - 5);
					dc.DrawPoint(r.x + r.width - 2, r.y + r.height - 5);

				}

				// set rectangle down a bit for gradient drawing
				r.SetHeight(r.GetHeight() / 2);
				r.x += 2;
				r.width -= 3;

				r.y += r.height;
				r.y -= 6;

				// draw gradient background
				wxColor top_color = gradient;
				wxColor bottom_color = gradient;

				dc.GradientFillLinear(r, bottom_color, top_color, wxNORTH);
			}
			else {

				// draw inactive tab
				wxRect r(tab_x, tab_y, tab_width, tab_height);

				// -- draw top gradient fill for glossy look
				wxColor top_color = m_baseColour;
				wxColor bottom_color = top_color;// .ChangeLightness(160);

				dc.GradientFillLinear(r, bottom_color, top_color, wxNORTH);

				// -- draw bottom fill for glossy look
				top_color = m_baseColour;
				bottom_color = m_baseColour;
				dc.GradientFillLinear(r, top_color, bottom_color, wxSOUTH);
			}

			// draw tab outline
			dc.SetPen(m_borderPen);
			dc.SetBrush(*wxTRANSPARENT_BRUSH);

			// there are two horizontal grey lines at the bottom of the tab control,
			// this gets rid of the top one of those lines in the tab control
			if (active) {

				if (m_flags & wxAUI_NB_BOTTOM)
					dc.SetPen(wxPen(m_baseColour.ChangeLightness(170)));
				// TODO: else if (m_flags &wxAUI_NB_LEFT) {}
				// TODO: else if (m_flags &wxAUI_NB_RIGHT) {}
				else //for wxAUI_NB_TOP
					dc.SetPen(m_baseColourPen);

				dc.DrawLine(border_points[0].x + 1,
					border_points[0].y,
					border_points[5].x,
					border_points[5].y);
			}

			int text_offset;
			int bitmap_offset = 0;

			if (m_bitmap.IsOk()) {

				bitmap_offset = tab_x + wxControl::FromDIP(8);

				// draw bitmap
				dc.DrawBitmap(m_bitmap,
					bitmap_offset,
					drawn_tab_yoff + (drawn_tab_height / 2) - (m_bitmap.GetLogicalHeight() / 2),
					true);

				text_offset = bitmap_offset + m_bitmap.GetLogicalWidth();
				text_offset += wxControl::FromDIP(3); // bitmap padding
			}
			else {
				text_offset = tab_x + wxControl::FromDIP(8);
			}

			wxString draw_text = ChopText(dc,
				caption,
				tab_width - (text_offset - tab_x));

			// draw tab text — dark on both: white text was for the dark slab the section used to be
			if (!active) {
				dc.SetTextForeground(wxColour(0x34, 0x3A, 0x40));   // #343a40
			}
			else {
				dc.SetTextForeground(*wxBLACK);
			}

			dc.DrawText(draw_text,
				text_offset,
				drawn_tab_yoff + (drawn_tab_height) / 2 - (texty / 2) - 1);

			// draw focus rectangle
			if (active) {

				wxRect focusRectText(text_offset, (drawn_tab_yoff + (drawn_tab_height) / 2 - (texty / 2) - 1),
					selected_textx, selected_texty);

				wxRect focusRect;
				wxRect focusRectBitmap;

				if (m_bitmap.IsOk()) {
					focusRectBitmap = wxRect(bitmap_offset, drawn_tab_yoff + (drawn_tab_height / 2) - (m_bitmap.GetLogicalHeight() / 2),
						m_bitmap.GetLogicalWidth(), m_bitmap.GetLogicalHeight());
				}

				if (m_bitmap.IsOk() && draw_text.IsEmpty())
					focusRect = focusRectBitmap;
				else if (!m_bitmap.IsOk() && !draw_text.IsEmpty())
					focusRect = focusRectText;
				else if (m_bitmap.IsOk() && !draw_text.IsEmpty())
					focusRect = focusRectText.Union(focusRectBitmap);

				focusRect.Inflate(2, 2);
				wxRendererNative::Get().DrawFocusRect(this, dc, focusRect, 0);
			}

			dc.DestroyClippingRegion();
		}

	private:

		unsigned int m_flags = wxAUI_TB_TEXT | wxAUI_NB_LEFT;

		ibProtocolNode m_section;   // a tree of its own (Clone) — the panel outlives the answer it came in
		wxPopupTransientWindow* m_popupWindow;
		wxLongLong m_closedByPressAt = 0;

		wxAuiToolBarItem item;

		wxFont m_normalFont;
		wxFont m_selectedFont;
		wxFont m_measuringFont;
		wxColour m_baseColour;
		wxColour m_highlightColour;

		wxPen m_baseColourPen;
		wxPen m_borderPen;

		wxBrush m_baseColourBrush;
		wxColour m_activeColour;

		ibSubSystemWindow* m_mainWindow;
		wxBitmap m_bitmap;
		wxEventType   m_eventType;

		wxDECLARE_EVENT_TABLE();
	};

	// ⭐ A COMMAND ON A SECTION'S PAGE — an icon and a caption, left-aligned, with no button around it.
	//
	// It was a wxButton with wxBORDER_NONE | wxBU_LEFT and an underlined font. On macOS a native button
	// centres its caption whatever wxBU_LEFT says, paints the system's grey over the colour it was given,
	// and a white background was set on it besides — so the page read as a column of centred, underlined,
	// grey words on white strips over a cream page. This draws the row itself: icon and caption from the
	// left edge, the page's own colour behind it, and the underline only while the pointer is over the
	// words (the underline that says "this is a link" is shown at the moment it is useful).
	//
	// It answers a click the way the button did — a wxEVT_BUTTON from this window, carrying its id — so
	// the handlers that were bound to the buttons are bound to this unchanged.
	class ibCommandLink : public wxControl {
	public:

		ibCommandLink(wxWindow* parent, wxWindowID id, const wxString& caption, const wxIcon& icon)
			: m_caption(caption), m_icon(icon)
		{
			wxControl::Create(parent, id, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);

			SetBackgroundStyle(wxBG_STYLE_PAINT);
			SetForegroundColour(wxDefaultStypeFGColour);
			EnableVisibleFocus(false);

			// The caption is four points larger than the system's normal text: it is what a person came to
			// read, and it sits under a heading that is bold.
			wxFont font = *wxNORMAL_FONT;
			font.SetPointSize(font.GetPointSize() + 4);
			font.SetUnderlined(false);
			SetFont(font);

			Bind(wxEVT_PAINT,        &ibCommandLink::OnPaint,        this);
			Bind(wxEVT_MOTION,       &ibCommandLink::OnMotion,       this);
			Bind(wxEVT_LEAVE_WINDOW, &ibCommandLink::OnLeave,        this);
			Bind(wxEVT_LEFT_UP,      &ibCommandLink::OnLeftUp,       this);
			Bind(wxEVT_ERASE_BACKGROUND, [](wxEraseEvent&) {});   // painted in full in OnPaint

			// The section page lives in a wxPopupTransientWindow, which holds the mouse while it is open;
			// on macOS that leaves the rows without motion and leave events. So the pointer is also looked
			// at from a timer, and the underline follows it even when nothing is delivered to the row.
			m_hoverTimer.SetOwner(this);
			Bind(wxEVT_TIMER, &ibCommandLink::OnHoverTick, this);
			m_hoverTimer.Start(50);

			SetInitialSize(DoGetBestSize());
		}

		// A row does not take the focus. It lives in a wxPopupTransientWindow, which closes itself when the
		// focus leaves it; a press on a row that took the focus closed the page before the release could
		// activate the row, and the release then went to the button that had the focus before.
		bool AcceptsFocus() const override { return false; }

	protected:

		wxSize DoGetBestSize() const override
		{
			wxClientDC dc(const_cast<ibCommandLink*>(this));
			dc.SetFont(GetFont());
			const wxSize text = dc.GetTextExtent(m_caption);
			const int iconW = m_icon.IsOk() ? m_icon.GetWidth() : 0;
			const int iconH = m_icon.IsOk() ? m_icon.GetHeight() : 0;
			const int gap = iconW > 0 ? Gap() : 0;
			return wxSize(iconW + gap + text.x + 2 * Pad(), wxMax(iconH, text.y) + 2 * Pad());
		}

	private:

		int Gap() const { return FromDIP(8); }
		int Pad() const { return FromDIP(2); }

		// The part of the row that is the command: the icon and the words, not the empty stretch to the
		// right of them that the sizer gave the control.
		wxRect ContentRect() const
		{
			const wxSize best = DoGetBestSize();
			return wxRect(0, 0, wxMin(best.x, GetClientSize().x), GetClientSize().y);
		}

		void SetHovered(bool hovered)
		{
			if (m_hovered == hovered)
				return;
			m_hovered = hovered;

			wxFont font = GetFont();
			font.SetUnderlined(hovered);
			SetFont(font);
			ApplyCursor();
			Refresh();
		}

		// The pointer shape comes from the window that holds the mouse, which in the section page is the
		// popup and not this row, so it is set on both.
		void ApplyCursor()
		{
			const wxCursor cursor = m_hovered ? wxCursor(wxCURSOR_HAND) : wxNullCursor;
			SetCursor(cursor);
			for (wxWindow* parent = GetParent(); parent != nullptr; parent = parent->GetParent()) {
				parent->SetCursor(cursor);
				if (dynamic_cast<wxPopupTransientWindow*>(parent) != nullptr)
					break;
			}
		}

		void OnMotion(wxMouseEvent& event)
		{
			SetHovered(ContentRect().Contains(event.GetPosition()));
			event.Skip();
		}

		void OnLeave(wxMouseEvent& event)
		{
			SetHovered(false);
			event.Skip();
		}

		void OnHoverTick(wxTimerEvent&)
		{
			if (!IsShownOnScreen()) {
				SetHovered(false);
				return;
			}
			const wxPoint pointer = ScreenToClient(wxGetMousePosition());
			SetHovered(ContentRect().Contains(pointer));
			if (m_hovered)
				ApplyCursor();   // the system puts the arrow back whenever the pointer moves
		}

		void Activate()
		{
			wxCommandEvent click(wxEVT_BUTTON, GetId());
			click.SetEventObject(this);
			ProcessWindowEvent(click);
		}

		void OnLeftUp(wxMouseEvent& event)
		{
			if (ContentRect().Contains(event.GetPosition()))
				Activate();
			event.Skip();
		}

		void OnPaint(wxPaintEvent&)
		{
			wxAutoBufferedPaintDC dc(this);

			// Behind the row is whatever the page behind it is — no colour of its own.
			const wxWindow* page = GetParent();
			dc.SetBackground(wxBrush(page != nullptr ? page->GetBackgroundColour() : GetBackgroundColour()));
			dc.Clear();

			dc.SetFont(GetFont());
			dc.SetTextForeground(GetForegroundColour());

			const wxSize client = GetClientSize();
			int x = 0;

			if (m_icon.IsOk()) {
				dc.DrawIcon(m_icon, x, (client.y - m_icon.GetHeight()) / 2);
				x += m_icon.GetWidth() + Gap();
			}

			const wxSize text = dc.GetTextExtent(m_caption);
			dc.DrawText(m_caption, x, (client.y - text.y) / 2);
		}

		wxString m_caption;
		wxIcon   m_icon;
		bool     m_hovered = false;
		wxTimer  m_hoverTimer;
	};

	class ibPopupSubWindow : public wxPopupTransientWindow {

		class ibScrolledSubWindow : public wxScrolledWindow {

			// What a link opens — the schema's Item, and how (its Type: a new item in Create, the default anywhere else).
			class ibScrolledSubWindowItemRefData : public wxClientData {
			public:
				ibScrolledSubWindowItemRefData(long long item, int type) : m_item(item), m_type(type) {}
				long long GetItem() const { return m_item; }
				int       GetType() const { return m_type; }
			private:
				long long m_item;
				int       m_type;
			};

		public:

			ibScrolledSubWindow() : wxScrolledWindow() {}
			ibScrolledSubWindow(ibPopupSubWindow* parent,
				wxWindowID winid = wxID_ANY) : wxScrolledWindow(parent, winid, wxDefaultPosition, wxDefaultSize, wxBORDER_SUNKEN | wxHSCROLL | wxVSCROLL), m_popupWindow(parent)
			{
				wxBoxSizer* sizerMain = new wxBoxSizer(wxHORIZONTAL);
				wxBoxSizer* sizerLeft = new wxBoxSizer(wxVERTICAL);
				wxBoxSizer* sizerRight = new wxBoxSizer(wxVERTICAL);

				// THE PAGE — the schema's blocks, in the panel's order (frmserver/schema/runtime/sections.cpp, PageOf). The
				// left column: the section's own items at the top under no heading (Important first), the navigation
				// groups the configuration declares, each subsection with what it and its own hold. The right column —
				// THE ACTIONS PANEL: the platform's groups (Create, Reports, Service), then the declared ones.
				for (const ibProtocolNode& block : m_popupWindow->GetSection().FindChild(ibProtocolName::Blocks).Children()) {
					const wxString title = block.GetString(ibProtocolName::Title);
					if (block.GetInt(ibProtocolName::Column, 1) == 2) {
						sizerRight->Add(CreateCaptionedBlock(title, block, ibProtocolPicture(block.GetString(ibProtocolName::Picture)),
							block.GetString(ibProtocolName::Tooltip)), 0, wxEXPAND, FromDIP(5));
						continue;
					}
					if (title.IsEmpty()) {
						wxBoxSizer* sizerSubCommonSpacer = new wxBoxSizer(wxHORIZONTAL);
						sizerSubCommonSpacer->Add(20, 0, 0, wxEXPAND, FromDIP(5));
						wxBoxSizer* sizerSubCommonItem = new wxBoxSizer(wxVERTICAL);

						AppendLinks(sizerSubCommonItem, block);

						sizerSubCommonSpacer->Add(sizerSubCommonItem, 1, wxEXPAND, FromDIP(5));
						sizerLeft->Add(sizerSubCommonSpacer, 0, wxEXPAND, FromDIP(5));
						continue;
					}
					sizerLeft->Add(CreateCaptionedBlock(title, block, ibProtocolPicture(block.GetString(ibProtocolName::Picture)),
						block.GetString(ibProtocolName::Tooltip)), 0, wxEXPAND, FromDIP(5));
				}

				sizerMain->Add(sizerLeft, 0, 0, 0);
				sizerMain->Add(50, 0, 0, wxEXPAND, FromDIP(5));
				sizerMain->Add(sizerRight, 0, 0, FromDIP(5));

				SetSizer(sizerMain);
				Layout();

				SetBackgroundColour(*wxWHITE);
				SetForegroundColour(wxDefaultStypeFGColour);
			}

		private:

			// The links of a block, one per item — each remembers what it opens.
			void AppendLinks(wxBoxSizer* sizerItem, const ibProtocolNode& block) {

				for (const ibProtocolNode& item : block.Children()) {

					ibCommandLink* df = new ibCommandLink(this, wxID_ANY, item.GetString(ibProtocolName::Title), IconOf(item.GetString(ibProtocolName::Icon)));
					df->SetClientObject(new ibScrolledSubWindowItemRefData(item.GetInt(ibProtocolName::Item), static_cast<int>(item.GetInt(ibProtocolName::Type))));

					df->Bind(wxEVT_BUTTON, &ibScrolledSubWindow::OnMenuItemClicked, this);

					sizerItem->Add(df, 0, wxEXPAND, FromDIP(5));
				}
			}

			// ONE CAPTIONED BLOCK of the page — a heading and the items under it. A declared group brings its picture
			// (beside the heading) and its tooltip (on it); the platform's have neither.
			wxBoxSizer* CreateCaptionedBlock(const wxString& caption, const ibProtocolNode& block,
				const wxBitmap& picture = wxNullBitmap, const wxString& tooltip = wxEmptyString) {

				wxBoxSizer* sizerBlock = new wxBoxSizer(wxVERTICAL);
				wxStaticText* st = new wxStaticText(this, wxID_ANY, caption, wxDefaultPosition, wxDefaultSize, 0);

				st->SetForegroundColour(wxDefaultStypeFGColour);
				st->Wrap(-1);
				st->SetFont([]{ wxFont f = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT); f.SetPointSize(f.GetPointSize() + 3); f.MakeBold(); return f; }());
				if (!tooltip.IsEmpty())
					st->SetToolTip(tooltip);

				if (picture.IsOk()) {
					wxBoxSizer* sizerHeading = new wxBoxSizer(wxHORIZONTAL);
					wxStaticBitmap* sb = new wxStaticBitmap(this, wxID_ANY, picture);
					if (!tooltip.IsEmpty())
						sb->SetToolTip(tooltip);
					sizerHeading->Add(sb, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(5));
					sizerHeading->Add(st, 0, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(5));
					sizerBlock->Add(sizerHeading, 0, wxEXPAND, 0);
				}
				else
					sizerBlock->Add(st, 0, wxALL | wxEXPAND, FromDIP(5));

				wxBoxSizer* sizerSpacer = new wxBoxSizer(wxHORIZONTAL);
				sizerSpacer->Add(20, 0, 0, wxEXPAND, FromDIP(5));

				wxBoxSizer* sizerItem = new wxBoxSizer(wxVERTICAL);

				AppendLinks(sizerItem, block);

				sizerSpacer->Add(sizerItem, 1, wxEXPAND, FromDIP(5));
				sizerBlock->Add(sizerSpacer, 1, wxEXPAND, FromDIP(5));
				return sizerBlock;
			}

			void OnMenuItemClicked(wxCommandEvent& event) {

				wxWindow* btn = dynamic_cast<wxWindow*>(event.GetEventObject());
				ibScrolledSubWindowItemRefData* refData = btn != nullptr ?
					dynamic_cast<ibScrolledSubWindowItemRefData*>(btn->GetClientObject()) : nullptr;
				if (refData == nullptr)
					return;

				// The schema's own command — the server opens the item, as the panel's link opened it.
				const bool executed = m_popupWindow->Open(refData->GetItem(), refData->GetType());
				if (executed) {
					// A click inside wxPopupTransientWindow doesn't dismiss it on
					// every platform. Close it explicitly once the command has
					// successfully opened its form.
					m_popupWindow->Dismiss();
				}
			}

			ibPopupSubWindow* m_popupWindow;
		};

	public:

		const ibProtocolNode& GetSection() const { return m_currentButton->GetSection(); }

		// An item of the page opened — through the window (ibSubSystemWindow's Open).
		bool Open(long long item, int type) const { return m_currentButton->GetMainWindow()->Open(item, type); }

		// ctors
		ibPopupSubWindow() : m_currentButton(nullptr) {}
		ibPopupSubWindow(wxWindow* parent, ibSubSystemButton* btn, const wxPoint& point, const wxSize& size, int style = wxBORDER_NONE | wxPU_CONTAINS_CONTROLS)
			: wxPopupTransientWindow(parent, style), m_currentButton(btn) {

			wxPopupTransientWindow::SetPosition(point);
			wxPopupTransientWindow::SetSize(size);
			wxPopupTransientWindow::SetBackgroundColour(THEME_COLOUR_MAIN);

			wxBoxSizer* bSizer1 = new wxBoxSizer(wxHORIZONTAL);

			m_staticLine = new wxWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize);
			m_staticLine->SetBackgroundColour(THEME_COLOUR_MAIN);
			m_staticLine->SetMinSize(wxSize(1, -1));
			bSizer1->Add(m_staticLine, 0, wxEXPAND | wxALL, 0);

			m_mainWindow = new ibScrolledSubWindow(this, wxID_ANY);
			m_mainWindow->SetScrollRate(5, FromDIP(5));

			// Cream content surface inside the popup — matches editor /
			// tree / lists. Was system HIGHLIGHTTEXT (pure white).
			m_mainWindow->SetBackgroundColour(wxColour(0xFA, 0xF7, 0xF0));  // #FAF7F0 cream

			bSizer1->Add(m_mainWindow, 1, wxEXPAND | wxALL, 0);

			SetSizer(bSizer1);
			Layout();

			// Fade animation. Start fully transparent — the timer ramps
			// alpha up to 255 on Popup and back to 0 on Dismiss, with
			// the actual base-class Dismiss deferred until the fade-out
			// finishes. Switching subsystem tabs cross-fades the popups
			// because the framework dismisses the previous one (its
			// fade-out runs in parallel with the new one's fade-in).
			m_fadeTimer.SetOwner(this, m_fadeTimerId);
			SetTransparent(0);
			Bind(wxEVT_TIMER, &ibPopupSubWindow::OnFadeTick, this, m_fadeTimerId);
		}

		// Implement base class pure virtuals.
		virtual void Popup(wxWindow* focus = nullptr) override {
			m_dismissPending = false;
			m_currentButton->SetPopupWindow(this);
			wxPopupTransientWindow::Popup(focus);
			StartFade(255);
		}

		virtual void Dismiss() override {
			if (m_dismissPending)
				return;   // already fading out; a second ask would restart the ramp
			m_currentButton->SetPopupWindow(nullptr);
			m_dismissPending = true;
			StartFade(0);
		}

	private:

		void StartFade(int target) {
			m_targetAlpha = target;
			if (!m_fadeTimer.IsRunning())
				m_fadeTimer.Start(15);  // ~60 FPS
		}

		void OnFadeTick(wxTimerEvent&) {
			constexpr int step = 48;  // ~5 ticks for full 0..255 transition
			if (m_currentAlpha < m_targetAlpha)
				m_currentAlpha = std::min(m_targetAlpha, m_currentAlpha + step);
			else
				m_currentAlpha = std::max(m_targetAlpha, m_currentAlpha - step);

			SetTransparent(m_currentAlpha);

			if (m_currentAlpha == m_targetAlpha) {
				m_fadeTimer.Stop();
				if (m_dismissPending && m_currentAlpha == 0) {
					wxPopupTransientWindow::Dismiss();
					Destroy();   // one window per opening, and nothing deleted them
				}
			}
		}

		static constexpr int m_fadeTimerId = wxID_HIGHEST + 4001;

		wxWindow*          m_staticLine    = nullptr;
		ibSubSystemButton* m_currentButton = nullptr;
		wxScrolledWindow*  m_mainWindow    = nullptr;

		wxTimer m_fadeTimer;
		int     m_currentAlpha   = 0;
		int     m_targetAlpha    = 255;
		bool    m_dismissPending = false;
	};

	ibSubSystemButton* CreateSubMenu(const ibProtocolNode& section) {
		return m_arrayPageButton.emplace_back(
			new ibSubSystemButton(this, wxID_ANY, section.Clone()));
	}

public:

	// An item of a page opened — the window's call; true when it was.
	using Opener = std::function<bool(long long item, int type)>;

	ibSubSystemWindow() : wxWindow(), m_activeButton(nullptr) {}
	ibSubSystemWindow(wxWindow* parent,
		wxWindowID id,
		const ibProtocolNode& shown,
		Opener open,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize,
		long style = 0,
		const wxString& name = wxASCII_STR(wxPanelNameStr)) : wxWindow(parent, id, pos, size, style, name), m_activeButton(nullptr),
		m_open(std::move(open))
	{
		// The sections the server shows this person — only those they may use come.
		for (const ibProtocolNode& section : shown.FindChild(ibProtocolName::Sections).Children())
			CreateSubMenu(section);

		// Interior palette — light dusty (one tier between powder chrome
		// and the cream popup interior). Was wxAUI_DEFAULT_COLOUR
		// (dark navy) lightened — read as a muddy mid-grey.
		wxWindow::SetBackgroundColour(wxColour(0xC8, 0xD6, 0xDF));  // #C8D6DF light dusty
	}

protected:

	void OnEventSize(wxSizeEvent& event) {
		unsigned int text_height_total = 0;
		for (auto& staticText : m_arrayPageButton) {
			staticText->SetSize(0, text_height_total, event.m_size.x, ms_text_height);
			text_height_total += ms_text_height;
		}
		event.Skip();
	}

	void OnEventButton(wxCommandEvent& event) {

		auto iterator = std::find(
			m_arrayPageButton.begin(), m_arrayPageButton.end(), event.GetEventObject());

		if (iterator != m_arrayPageButton.end())
			m_activeButton = *iterator;
		else
			m_activeButton = nullptr;

		if (event.GetExtraLong()) {

			const wxPoint& pos = GetScreenPosition();
			const wxSize& size = GetSize();

			const wxSize& main_size = wxGetTopLevelParent(this)->GetSize();

			ibPopupSubWindow* subWindow = new ibPopupSubWindow(this,
				m_activeButton,
				{ pos.x + size.x - 5, pos.y + 1 },
				{ main_size.x - size.x - 20, size.y - 5 }
			);

			subWindow->Popup();
		}

		event.Skip();
	}

public:

	bool Open(long long item, int type) const { return m_open && m_open(item, type); }

private:

	ibSubSystemButton* m_activeButton;
	Opener             m_open;

	static const int ms_text_height = 35;
	static const int ms_text_width = 200;

	std::vector<ibSubSystemButton*> m_arrayPageButton;

	wxDECLARE_EVENT_TABLE();
};

wxBEGIN_EVENT_TABLE(ibSubSystemWindow::ibSubSystemButton, wxControl)
EVT_LEFT_UP(ibSubSystemWindow::ibSubSystemButton::OnLeftUp)
EVT_PAINT(ibSubSystemWindow::ibSubSystemButton::OnPaint)
wxEND_EVENT_TABLE()

wxBEGIN_EVENT_TABLE(ibSubSystemWindow, wxWindow)
EVT_BUTTON(wxID_ANY, ibSubSystemWindow::OnEventButton)
EVT_SIZE(ibSubSystemWindow::OnEventSize)
wxEND_EVENT_TABLE()

//////////////////////////////////////////////////////////////////////////////

void ibFrontendMainFrameEnterprise::CreateSubSystem()
{
	// The sections are the server's: asked of the schema when the application offers it (the frame's Schemas) — a
	// server that does not is not asked, and the window goes without the panel.
	bool offered = false;
	for (const ibProtocolNode& schema : GetCommunicator().GetFrame().GetList(ibProtocolName::Schemas)) {
		if (schema.AsInt() == static_cast<long long>(ibProtocolSchema::Sections))
			offered = true;
	}
	if (!offered)
		return;

	ibProtocolNode params, shown;
	params.SetValue(ibProtocolName::Schema, static_cast<int>(ibProtocolSchema::Sections));
	if (!Call(ibProtocolMethod::Schema, params, shown))
		return;

	const bool hasInterface = !shown.FindChild(ibProtocolName::Sections).Children().empty();

	if (hasInterface) {

		wxAuiPaneInfo m_infoSection;
		m_infoSection.Name(wxT("section"));
		m_infoSection.Left();
		m_infoSection.CaptionVisible(false);
		m_infoSection.MinSize(200, 25);
		m_infoSection.Movable(false);
		m_infoSection.Dockable(false);
		m_infoSection.Fixed();

		// A link opens its item through the schema's own command — Open {Item, Type}.
		m_mgr.AddPane(
			new ibSubSystemWindow(this, wxID_ANY, shown, [this](long long item, int type) {
				ibProtocolNode open, answer;
				open.SetValue(ibProtocolName::Schema, static_cast<int>(ibProtocolSchema::Sections))
					.SetValue(ibProtocolName::Command, static_cast<int>(ibProtocolSchemaCommand::Open));
				open.Child(ibProtocolName::Args).SetValue(ibProtocolName::Item, item).SetValue(ibProtocolName::Type, type);
				return Call(ibProtocolMethod::Schema, open, answer);
			}), m_infoSection);
		m_mgr.Update();
	}
}

//////////////////////////////////////////////////////////////////////////////
