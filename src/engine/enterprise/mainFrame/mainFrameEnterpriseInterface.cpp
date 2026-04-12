#include "mainFrameEnterprise.h"

#include <wx/artprov.h>
#include <wx/renderer.h>
#include <wx/popupwin.h>
#include <wx/statline.h>
#include <wx/tglbtn.h>
#include <wx/hyperlink.h>

#define THEME_COLOUR_MAIN wxColour(255, 232, 166) 
#define THEME_COLOUR_BORDER wxColour(41, 57, 85) 

#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/metaInterfaceObject.h"

#include "frontend/visualView/ctrl/frame.h"

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

			if (!wnd)
				m_mainWindow->m_activeButton = nullptr;

			ibSubSystemButton::Refresh();
			ibSubSystemButton::Update();
		}

		void DismissPopupWindow() {

			if (m_popupWindow != nullptr)
				m_popupWindow->Dismiss();
		}

		const ibValueMetaObjectInterface* GetMetaObject() const { return m_metaObject; }

		ibSubSystemButton(ibSubSystemWindow* mainWindow, wxWindowID id, const ibValueMetaObjectInterface* object)
			: wxControl(mainWindow, id, wxDefaultPosition, wxDefaultSize, wxNO_BORDER),
			m_mainWindow(mainWindow), m_bitmap(object->GetPictureAsBitmap()), m_metaObject(object), m_popupWindow(nullptr),
			m_eventType(wxEVT_BUTTON) {

			m_baseColour = wxSystemSettings::GetColour(wxSYS_COLOUR_3DFACE);
			m_highlightColour = wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHT);

			SetLabel(object->GetSynonym());

			m_normalFont = *wxNORMAL_FONT;
			m_selectedFont = *wxNORMAL_FONT;
			m_selectedFont.SetWeight(wxFONTWEIGHT_BOLD);
			m_measuringFont = m_selectedFont;

			wxColor baseColour = THEME_COLOUR_BORDER;

			m_activeColour = baseColour;
			m_baseColour = baseColour.ChangeLightness(75);

			m_borderPen = wxPen(THEME_COLOUR_MAIN);
			m_baseColourPen = wxPen(THEME_COLOUR_MAIN);
			m_baseColourBrush = wxBrush(THEME_COLOUR_MAIN);

			SetCursor(wxCURSOR_HAND);
			SetBackgroundStyle(wxBG_STYLE_PAINT);
		}

		virtual wxWindow* GetMainWindowOfCompositeControl() override { return m_mainWindow; }

	protected:

		void OnLeftUp(wxMouseEvent& event) {

			if (m_mainWindow->m_activeButton != this) {

				DismissPopupWindow();

				wxCommandEvent event(m_eventType, m_mainWindow->GetId());

				event.SetEventObject(this);
				event.SetString(GetLabel());
				event.SetExtraLong(true);

				GetEventHandler()->ProcessEvent(event);
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
				wxColor gradient = THEME_COLOUR_MAIN;

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

			// draw tab text
			if (!active) {
				dc.SetTextForeground(*wxWHITE);
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

		const ibValueMetaObjectInterface* m_metaObject;
		wxPopupTransientWindow* m_popupWindow;

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

	class ibPopupSubWindow : public wxPopupTransientWindow {

		class ibScrolledSubWindow : public wxScrolledWindow {

			class ibScrolledSubWindowSectionRefData : public wxClientData {
			public:
				ibInterfaceCommandSection GetArea() const { return m_section; }
				ibScrolledSubWindowSectionRefData(ibInterfaceCommandSection s) : m_section(s) {}
			private:
				ibInterfaceCommandSection m_section;
			};

		public:

			ibScrolledSubWindow() : wxScrolledWindow() {}
			ibScrolledSubWindow(ibPopupSubWindow* parent,
				wxWindowID winid = wxID_ANY) : wxScrolledWindow(parent, winid, wxDefaultPosition, wxDefaultSize, wxBORDER_SUNKEN | wxHSCROLL | wxVSCROLL), m_popupWindow(parent)
			{
				wxBoxSizer* sizerMain = new wxBoxSizer(wxHORIZONTAL);
				wxBoxSizer* sizerLeft = new wxBoxSizer(wxVERTICAL);

				const ibValueMetaObjectInterface* metaObject = m_popupWindow->GetMetaObject();
				if (metaObject != nullptr) {
					std::vector<ibValueMetaObject*> array;
					if (metaObject->GetInterfaceItemArrayObject(ibInterfaceCommandSection_Default, array)) {

						wxBoxSizer* sizerSubCommonSpacer = new wxBoxSizer(wxHORIZONTAL);
						sizerSubCommonSpacer->Add(20, 0, 0, wxEXPAND, 5);
						wxBoxSizer* sizerSubCommonItem = new wxBoxSizer(wxVERTICAL);

						for (const auto object : array) {

							wxButton* df = new wxButton(this, object->GetMetaID(), object->GetSynonym(),
								wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxBU_LEFT);

							df->SetBitmap(object->GetIcon());
							df->SetFont(wxFont(wxNORMAL_FONT->GetPointSize(), wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, true, wxEmptyString));
							df->SetBackgroundColour(*wxWHITE);
							df->SetForegroundColour(wxDefaultStypeFGColour);

							df->SetCursor(wxCURSOR_HAND);
							df->SetClientObject(new ibScrolledSubWindowSectionRefData(ibInterfaceCommandSection_Default));
							df->Bind(wxEVT_BUTTON, &ibScrolledSubWindow::OnMenuItemClicked, this);

							sizerSubCommonItem->Add(df, 0, wxEXPAND, 5);
						}

						sizerSubCommonSpacer->Add(sizerSubCommonItem, 1, wxEXPAND, 5);
						sizerLeft->Add(sizerSubCommonSpacer, 0, wxEXPAND, 5);
					}
				}

				for (const auto child : metaObject->GetInterfaceArrayObject()) {

					std::vector<ibValueMetaObject*> array;

					child->GetInterfaceItemArrayObject(ibInterfaceCommandSection_Default, array);

					child->GetInterfaceItemArrayObject(ibInterfaceCommandSection_Create, array);
					child->GetInterfaceItemArrayObject(ibInterfaceCommandSection_Report, array);
					child->GetInterfaceItemArrayObject(ibInterfaceCommandSection_Service, array);

					if (array.size() > 0) {

						wxBoxSizer* sizerSubsystem = new wxBoxSizer(wxVERTICAL);
						wxStaticText* st = new wxStaticText(this, wxID_ANY, child->GetSynonym());

						st->SetBackgroundColour(*wxWHITE);
						st->SetForegroundColour(wxDefaultStypeFGColour);
						st->Wrap(-1);
						st->SetFont(wxFont(12, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, wxT("Arial")));

						sizerSubsystem->Add(st, 0, wxALL, 5);

						wxBoxSizer* sizerSubsystemSpacer = new wxBoxSizer(wxHORIZONTAL);
						sizerSubsystemSpacer->Add(20, 0, 0, wxEXPAND, 5);
						wxBoxSizer* sizerSubSystemItem = new wxBoxSizer(wxVERTICAL);

						struct CSubWindowConstructor {

							static void NextChildConstruct(wxBoxSizer* sizerSubSystemItem, const ibValueMetaObjectInterface* parent, ibScrolledSubWindow* wnd) {

								for (const auto child : parent->GetInterfaceArrayObject()) {

									std::vector<ibValueMetaObject*> subArray;

									child->GetInterfaceItemArrayObject(ibInterfaceCommandSection_Default, subArray);
									child->GetInterfaceItemArrayObject(ibInterfaceCommandSection_Create, subArray);
									child->GetInterfaceItemArrayObject(ibInterfaceCommandSection_Report, subArray);
									child->GetInterfaceItemArrayObject(ibInterfaceCommandSection_Service, subArray);

									if (subArray.size() > 0) {

										for (const auto object : subArray) {

											wxButton* df = new wxButton(wnd, object->GetMetaID(), object->GetSynonym(),
												wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxBU_LEFT);

											df->SetBitmap(object->GetIcon());
											df->SetFont(wxFont(wxNORMAL_FONT->GetPointSize(), wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, true, wxEmptyString));

											df->SetBackgroundColour(*wxWHITE);
											df->SetForegroundColour(wxDefaultStypeFGColour);
											df->SetCursor(wxCURSOR_HAND);
											df->SetClientObject(new ibScrolledSubWindowSectionRefData(ibInterfaceCommandSection_Default));
											df->Bind(wxEVT_BUTTON, &ibScrolledSubWindow::OnMenuItemClicked, wnd);

											sizerSubSystemItem->Add(df, 0, wxEXPAND, 5);

											NextChildConstruct(sizerSubSystemItem, child, wnd);
										}
									}
								}
							}
						};

						for (const auto object : array) {

							wxButton* df = new wxButton(this, object->GetMetaID(), object->GetSynonym(),
								wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxBU_LEFT);

							df->SetBitmap(object->GetIcon());
							df->SetFont(wxFont(wxNORMAL_FONT->GetPointSize(), wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, true, wxEmptyString));

							df->SetBackgroundColour(*wxWHITE);
							df->SetForegroundColour(wxDefaultStypeFGColour);
							df->SetCursor(wxCURSOR_HAND);
							df->SetClientObject(new ibScrolledSubWindowSectionRefData(ibInterfaceCommandSection_Default));
							df->Bind(wxEVT_BUTTON, &ibScrolledSubWindow::OnMenuItemClicked, this);

							sizerSubSystemItem->Add(df, 0, wxEXPAND, 5);
						}

						CSubWindowConstructor::NextChildConstruct(sizerSubSystemItem, child, this);

						sizerSubsystemSpacer->Add(sizerSubSystemItem, 1, wxEXPAND, 5);
						sizerSubsystem->Add(sizerSubsystemSpacer, 1, wxEXPAND, 5);

						sizerLeft->Add(sizerSubsystem, 1, wxEXPAND, 0);
					}
				}

				sizerMain->Add(sizerLeft, 0, 0, 0);
				sizerMain->Add(50, 0, 0, wxEXPAND, 5);

				wxBoxSizer* sizerRight = new wxBoxSizer(wxVERTICAL);

				if (metaObject != nullptr) {

					std::vector<ibValueMetaObject*> array;
					if (metaObject->GetInterfaceItemArrayObject(ibInterfaceCommandSection_Create, array)) {

						wxBoxSizer* sizerCreate = new wxBoxSizer(wxVERTICAL);
						wxStaticText* st_create = new wxStaticText(this, wxID_ANY, _("Create"), wxDefaultPosition, wxDefaultSize, 0);

						st_create->SetBackgroundColour(*wxWHITE);
						st_create->SetForegroundColour(wxDefaultStypeFGColour);
						st_create->Wrap(-1);
						st_create->SetFont(wxFont(12, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, wxT("Arial")));

						sizerCreate->Add(st_create, 0, wxALL | wxEXPAND, 5);

						wxBoxSizer* sizerCreateSpacer = new wxBoxSizer(wxHORIZONTAL);
						sizerCreateSpacer->Add(20, 0, 0, wxEXPAND, 5);

						wxBoxSizer* sizerCreateItem = new wxBoxSizer(wxVERTICAL);

						for (const auto object : array) {

							wxButton* df = new wxButton(this, object->GetMetaID(), object->GetSynonym(),
								wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxBU_LEFT);

							df->SetBitmap(object->GetIcon());
							df->SetFont(wxFont(wxNORMAL_FONT->GetPointSize(), wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, true, wxEmptyString));

							df->SetBackgroundColour(*wxWHITE);
							df->SetForegroundColour(wxDefaultStypeFGColour);
							df->SetCursor(wxCURSOR_HAND);
							df->SetClientObject(new ibScrolledSubWindowSectionRefData(ibInterfaceCommandSection_Create));

							df->Bind(wxEVT_BUTTON, &ibScrolledSubWindow::OnMenuItemClicked, this);

							sizerCreateItem->Add(df, 0, wxEXPAND, 5);
						}

						sizerCreateSpacer->Add(sizerCreateItem, 1, wxEXPAND, 5);
						sizerCreate->Add(sizerCreateSpacer, 1, wxEXPAND, 5);
						sizerRight->Add(sizerCreate, 0, wxEXPAND, 5);
					}
				}

				if (metaObject != nullptr) {

					std::vector<ibValueMetaObject*> array;
					if (metaObject->GetInterfaceItemArrayObject(ibInterfaceCommandSection_Report, array)) {

						wxBoxSizer* sizerReport = new wxBoxSizer(wxVERTICAL);
						wxStaticText* st_report = new wxStaticText(this, wxID_ANY, _("Report"), wxDefaultPosition, wxDefaultSize, 0);

						st_report->SetBackgroundColour(*wxWHITE);
						st_report->SetForegroundColour(wxDefaultStypeFGColour);
						st_report->Wrap(-1);
						st_report->SetFont(wxFont(12, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, wxT("Arial")));

						sizerReport->Add(st_report, 0, wxALL | wxEXPAND, 5);

						wxBoxSizer* sizerReportSpacer = new wxBoxSizer(wxHORIZONTAL);
						sizerReportSpacer->Add(20, 0, 0, wxEXPAND, 5);

						wxBoxSizer* sizerReportItem = new wxBoxSizer(wxVERTICAL);

						for (const auto object : array) {

							wxButton* df = new wxButton(this, object->GetMetaID(), object->GetSynonym(),
								wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxBU_LEFT);

							df->SetBitmap(object->GetIcon());
							df->SetFont(wxFont(wxNORMAL_FONT->GetPointSize(), wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, true, wxEmptyString));

							df->SetBackgroundColour(*wxWHITE);
							df->SetForegroundColour(wxDefaultStypeFGColour);
							df->SetCursor(wxCURSOR_HAND);
							df->SetClientObject(new ibScrolledSubWindowSectionRefData(ibInterfaceCommandSection_Report));

							df->Bind(wxEVT_BUTTON, &ibScrolledSubWindow::OnMenuItemClicked, this);

							sizerReportItem->Add(df, 0, wxEXPAND, 5);
						}

						sizerReportSpacer->Add(sizerReportItem, 1, wxEXPAND, 5);
						sizerReport->Add(sizerReportSpacer, 1, wxEXPAND, 5);

						sizerRight->Add(sizerReport, 0, wxEXPAND, 5);
					}
				}

				if (metaObject != nullptr) {
					std::vector<ibValueMetaObject*> array;
					if (metaObject->GetInterfaceItemArrayObject(ibInterfaceCommandSection_Service, array)) {

						wxBoxSizer* sizerService = new wxBoxSizer(wxVERTICAL);
						wxStaticText* st_service = new wxStaticText(this, wxID_ANY, _("Service"), wxDefaultPosition, wxDefaultSize, 0);

						st_service->SetBackgroundColour(*wxWHITE);
						st_service->SetForegroundColour(wxDefaultStypeFGColour);
						st_service->Wrap(-1);
						st_service->SetFont(wxFont(12, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, wxT("Arial")));

						sizerService->Add(st_service, 0, wxALL | wxEXPAND, 5);

						wxBoxSizer* sizerServiceSpacer = new wxBoxSizer(wxHORIZONTAL);
						sizerServiceSpacer->Add(20, 0, 0, wxEXPAND, 1);
						wxBoxSizer* sizerServiceItem = new wxBoxSizer(wxVERTICAL);

						for (const auto object : array) {

							wxButton* df = new wxButton(this, object->GetMetaID(), object->GetSynonym(),
								wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxBU_LEFT);

							df->SetBitmap(object->GetIcon());
							df->SetFont(wxFont(wxNORMAL_FONT->GetPointSize(), wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, true, wxEmptyString));

							df->SetBackgroundColour(*wxWHITE);
							df->SetForegroundColour(wxDefaultStypeFGColour);
							df->SetCursor(wxCURSOR_HAND);
							df->SetClientObject(new ibScrolledSubWindowSectionRefData(ibInterfaceCommandSection_Service));

							df->Bind(wxEVT_BUTTON, &ibScrolledSubWindow::OnMenuItemClicked, this);

							sizerServiceItem->Add(df, 0, wxEXPAND, 5);
						}

						sizerServiceSpacer->Add(sizerServiceItem, 1, wxEXPAND, 5);
						sizerService->Add(sizerServiceSpacer, 1, wxEXPAND, 5);
						sizerRight->Add(sizerService, 0, wxEXPAND, 5);
					}
				}

				sizerMain->Add(sizerRight, 0, 0, 5);

				SetSizer(sizerMain);
				Layout();

				SetBackgroundColour(*wxWHITE);
				SetForegroundColour(wxDefaultStypeFGColour);
			}

		private:

			ibInterfaceCommandType GetCommandType(const ibInterfaceCommandSection section) const {

				if (section == ibInterfaceCommandSection::ibInterfaceCommandSection_Create)
					return ibInterfaceCommandType::ibInterfaceCommandType_Create;
				else if (section == ibInterfaceCommandSection::ibInterfaceCommandSection_Service)
					return ibInterfaceCommandType::ibInterfaceCommandType_Default;
				else if (section == ibInterfaceCommandSection::ibInterfaceCommandSection_Report)
					return ibInterfaceCommandType::ibInterfaceCommandType_Default;

				return ibInterfaceCommandType::ibInterfaceCommandType_Default;
			}

			void OnMenuItemClicked(wxCommandEvent& event) {

				wxWindow* btn = dynamic_cast<wxWindow*>(event.GetEventObject());
				ibScrolledSubWindowSectionRefData* refData = btn != nullptr ?
					dynamic_cast<ibScrolledSubWindowSectionRefData*>(btn->GetClientObject()) : nullptr;

				const ibInterfaceCommandSection section = refData != nullptr ?
					refData->GetArea() : ibInterfaceCommandSection::ibInterfaceCommandSection_Default;

				ibBackendCommandItem* cmdItem =
					dynamic_cast<ibBackendCommandItem*>(activeMetaData->FindAnyObjectByFilter(event.GetId()));

				if (cmdItem != nullptr &&
					cmdItem->ShowFormByCommandType(GetCommandType(section)))
					event.Skip();
			}

			ibPopupSubWindow* m_popupWindow;
		};

	public:

		const ibValueMetaObjectInterface* GetMetaObject() const { return m_currentButton->GetMetaObject(); }

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
			m_mainWindow->SetScrollRate(5, 5);

			m_mainWindow->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHTTEXT));
			//m_mainWindow->SetBackgroundColour(THEME_COLOUR_BORDER);

			bSizer1->Add(m_mainWindow, 1, wxEXPAND | wxALL, 0);

			SetSizer(bSizer1);
			Layout();
		}

		// Implement base class pure virtuals.
		virtual void Popup(wxWindow* focus = nullptr) override {
			m_currentButton->SetPopupWindow(this);
			wxPopupTransientWindow::Popup(focus);
			wxLogDebug(wxT("wxPopupTransientWindow::Popup"));
		}

		virtual void Dismiss() override {
			m_currentButton->SetPopupWindow(nullptr);
			wxPopupTransientWindow::Dismiss();
			wxLogDebug(wxT("wxPopupTransientWindow::Dismiss"));
		}

	private:

		wxWindow* m_staticLine;
		ibSubSystemButton* m_currentButton;
		wxScrolledWindow* m_mainWindow;
	};

	ibSubSystemButton* CreateSubMenu(const ibValueMetaObjectInterface* object) {
		return m_arrayPageButton.emplace_back(
			new ibSubSystemButton(this, wxID_ANY, object));
	}

public:

	ibSubSystemWindow() : wxWindow(), m_activeButton(nullptr) {}
	ibSubSystemWindow(wxWindow* parent,
		wxWindowID id,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize,
		long style = 0,
		const wxString& name = wxASCII_STR(wxPanelNameStr)) : wxWindow(parent, id, pos, size, style, name), m_activeButton(nullptr)
	{
		for (const auto object : activeMetaData->GetAnyArrayObject<ibValueMetaObjectInterface>(g_metaInterfaceCLSID)) {
			if (object->AccessRight_Use()) CreateSubMenu(object);
		}

		wxWindow::SetBackgroundColour(wxAUI_DEFAULT_COLOUR.ChangeLightness(85));
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

			const wxSize& main_size = mainFrame->GetSize();

			ibPopupSubWindow* subWindow = new ibPopupSubWindow(this,
				m_activeButton,
				{ pos.x + size.x - 5, pos.y + 1 },
				{ main_size.x - size.x - 20, size.y - 5 }
			);

			subWindow->Popup();
		}

		event.Skip();
	}

private:

	ibSubSystemButton* m_activeButton;

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

void ibFrontendDocMDIFrameEnterprise::CreateSubSystem()
{
	bool hasInterface = false;

	for (const auto object : activeMetaData->GetAnyArrayObject<ibValueMetaObjectInterface>(g_metaInterfaceCLSID)) {
		if (object->AccessRight_Use()) {
			hasInterface = true;
			break;
		}
	}

	if (hasInterface) {

		wxAuiPaneInfo m_infoSection;
		m_infoSection.Name(wxT("section"));
		m_infoSection.Left();
		m_infoSection.CaptionVisible(false);
		m_infoSection.MinSize(200, 25);
		m_infoSection.Movable(false);
		m_infoSection.Dockable(false);
		m_infoSection.Fixed();

		m_mgr.AddPane(
			new ibSubSystemWindow(this, wxID_ANY), m_infoSection);
	}
}

//////////////////////////////////////////////////////////////////////////////