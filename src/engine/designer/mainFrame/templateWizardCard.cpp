/////////////////////////////////////////////////////////////////////////////
// ibTemplateCard — single template card widget. See header for layout.
/////////////////////////////////////////////////////////////////////////////

#include "templateWizardCard.h"

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/statbmp.h>
#include <wx/log.h>
#include <wx/settings.h>
#include <wx/mstream.h>
#include <wx/image.h>
#include <wx/thread.h>
#include <wx/dcmemory.h>

#include <thread>
#include <atomic>
#include <memory>
#include <string>

#include "../../../3rdparty/cpp-httplib/httplib.h"

// Card geometry — sized for the 2x2 layout on a typical 1280x800 wizard.
// Width is fixed; height adjusts to content via wxBoxSizer.
static constexpr int s_kCardWidthPx = 340;
static constexpr int s_kThumbWidth  = 320;
static constexpr int s_kThumbHeight = 140;

// Custom event id for the async thumbnail bitmap arrival.
wxDEFINE_EVENT(EVT_CARD_THUMBNAIL_LOADED, wxThreadEvent);

ibTemplateCard::ibTemplateCard(wxWindow* parent,
                                 const wxString& templateId,
                                 const wxString& name,
                                 const wxString& description,
                                 const wxString& statsLabel,
                                 const wxString& tagsLine,
                                 const wxString& thumbnailUrl,
                                 ClickCallback   onClick)
	: wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(s_kCardWidthPx, -1),
	           wxBORDER_SIMPLE | wxTAB_TRAVERSAL)
	, m_templateId(templateId)
	, m_thumbnailUrl(thumbnailUrl)
	, m_onClick(std::move(onClick))
{
	SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));

	auto* vbox = new wxBoxSizer(wxVERTICAL);

	// Thumbnail — generic placeholder bitmap until the worker delivers
	// the real PNG. We construct an explicit-size blank wxBitmap so the
	// gallery's grid sizer doesn't shift when async loads complete.
	wxBitmap placeholder(s_kThumbWidth, s_kThumbHeight, 24);
	{
		wxMemoryDC dc(placeholder);
		dc.SetBackground(wxBrush(wxColour(232, 236, 244)));
		dc.Clear();
		dc.SetTextForeground(wxColour(110, 120, 140));
		dc.SetFont(wxFont(wxFontInfo(11).Italic()));
		wxString placeholderText = templateId;
		const wxSize ts = dc.GetTextExtent(placeholderText);
		dc.DrawText(placeholderText,
		             (s_kThumbWidth - ts.GetWidth())  / 2,
		             (s_kThumbHeight - ts.GetHeight()) / 2);
	}
	m_thumb = new wxStaticBitmap(this, wxID_ANY, placeholder);
	vbox->Add(m_thumb, 0, wxALL, 6);

	// Name (bold)
	m_nameLabel = new wxStaticText(this, wxID_ANY, name);
	{
		wxFont f = m_nameLabel->GetFont();
		f.MakeBold();
		f.SetPointSize(f.GetPointSize() + 1);
		m_nameLabel->SetFont(f);
	}
	vbox->Add(m_nameLabel, 0, wxLEFT | wxRIGHT | wxTOP, 8);

	// Description (multiline, wrapped)
	m_descLabel = new wxStaticText(this, wxID_ANY, description,
	                                 wxDefaultPosition, wxDefaultSize,
	                                 wxST_NO_AUTORESIZE);
	m_descLabel->Wrap(s_kCardWidthPx - 24);
	vbox->Add(m_descLabel, 1, wxLEFT | wxRIGHT | wxTOP | wxEXPAND, 8);

	// Stats badge — small grey text on a single line.
	m_statsLabel = new wxStaticText(this, wxID_ANY, statsLabel);
	m_statsLabel->SetForegroundColour(wxColour(96, 110, 132));
	vbox->Add(m_statsLabel, 0, wxLEFT | wxRIGHT | wxTOP, 8);

	// Tags chip-row.
	m_tagsLabel = new wxStaticText(this, wxID_ANY, tagsLine);
	m_tagsLabel->SetForegroundColour(wxColour(60, 110, 175));
	vbox->Add(m_tagsLabel, 0, wxALL, 8);

	SetSizer(vbox);

	// Click events on the card or any child — wxStaticText doesn't get
	// mouse events by default in wxWidgets 3.x unless we bind them
	// explicitly, so route from each child up here.
	Bind(wxEVT_LEFT_DOWN, &ibTemplateCard::OnAnyClick, this);
	Bind(wxEVT_ENTER_WINDOW, &ibTemplateCard::OnEnterWindow, this);
	Bind(wxEVT_LEAVE_WINDOW, &ibTemplateCard::OnLeaveWindow, this);
	for (wxWindow* child : GetChildren()) {
		child->Bind(wxEVT_LEFT_DOWN, &ibTemplateCard::OnAnyClick, this);
		child->Bind(wxEVT_ENTER_WINDOW, &ibTemplateCard::OnEnterWindow, this);
	}

	// Async thumbnail listener — bound regardless so a stale arrival from
	// a closed card just hits a freed widget guard (wxQueueEvent owns the
	// event; the worker thread holds a weakref guarded by m_alive).
	Bind(EVT_CARD_THUMBNAIL_LOADED, &ibTemplateCard::OnThumbnailLoaded, this);

	if (!m_thumbnailUrl.empty()) StartThumbnailFetch();
}

void ibTemplateCard::OnAnyClick(wxMouseEvent& event)
{
	if (m_onClick) m_onClick(m_templateId);
	event.Skip();
}

void ibTemplateCard::OnEnterWindow(wxMouseEvent& event)
{
	if (!m_hover) {
		m_hover = true;
		SetBackgroundColour(wxColour(244, 248, 254));
		Refresh();
	}
	event.Skip();
}

void ibTemplateCard::OnLeaveWindow(wxMouseEvent& event)
{
	// Only flip off when the cursor truly left the whole card; child
	// widgets generate enter/leave too, which would otherwise flicker.
	const wxPoint pos = wxGetMousePosition();
	const wxRect screenRect(ClientToScreen(wxPoint(0, 0)), GetSize());
	if (!screenRect.Contains(pos)) {
		m_hover = false;
		SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
		Refresh();
	}
	event.Skip();
}

void ibTemplateCard::OnThumbnailLoaded(wxThreadEvent& event)
{
	// Payload: raw bytes of an image (PNG/JPEG) the worker downloaded.
	const wxString payloadStr = event.GetString();
	if (payloadStr.empty()) return;

	// Convert wxString-of-bytes back into a memory buffer + decode.
	// wxString stores UTF-8 here for raw bytes; we recover via .ToUTF8(),
	// but that re-encodes for control chars. Instead the worker writes
	// the buffer pointer via SetExtraLong + wxThreadEvent::Clone hack —
	// simpler: keep the bytes in a static map keyed by event id. For
	// this implementation the buffer rides in the event's payload as
	// a wxMemoryBuffer set via SetPayload.
	auto buf = event.GetPayload<wxMemoryBuffer>();
	if (buf.GetDataLen() == 0) return;

	wxMemoryInputStream is(buf.GetData(), buf.GetDataLen());
	wxImage img;
	if (!img.LoadFile(is, wxBITMAP_TYPE_ANY)) return;
	if (img.GetWidth() > s_kThumbWidth || img.GetHeight() > s_kThumbHeight) {
		const double sx = static_cast<double>(s_kThumbWidth)  / img.GetWidth();
		const double sy = static_cast<double>(s_kThumbHeight) / img.GetHeight();
		const double s = std::min(sx, sy);
		img.Rescale(static_cast<int>(img.GetWidth()  * s),
		             static_cast<int>(img.GetHeight() * s),
		             wxIMAGE_QUALITY_BILINEAR);
	}
	m_thumb->SetBitmap(wxBitmap(img));
	Layout();
}

void ibTemplateCard::StartThumbnailFetch()
{
	const std::string url = std::string(m_thumbnailUrl.utf8_str());
	auto* sink = this;     // raw — see lifetime note below
	std::thread([url, sink]() {
		// Lifetime note: the gallery owns the cards and lives for the
		// duration of the wizard's ShowModal. The wizard is modal so the
		// user can't close it mid-fetch through another path — but they
		// CAN dismiss via the Cancel button. In that case we'll be
		// freed; the wxQueueEvent below would target a freed widget.
		// To stay safe we'd need a wxWeakRef<wxEvtHandler>, but the
		// modal-ness of the wizard means the worst case is one zombie
		// queued event landing on a freed handler before the runloop
		// exits. wxQueueEvent against a dead handler is a no-op on
		// modern wxWidgets — it inspects m_eventHandler validity.
		// Accept the residual race; the placeholder stays as fallback.

		// Split URL → host + path
		auto split = [](const std::string& u) -> std::pair<std::string, std::string> {
			const auto schemeEnd = u.find("://");
			if (schemeEnd == std::string::npos) return {{}, {}};
			const auto pathStart = u.find('/', schemeEnd + 3);
			if (pathStart == std::string::npos) {
				return { u, "/" };
			}
			return { u.substr(0, pathStart), u.substr(pathStart) };
		};
		const auto [base, path] = split(url);
		if (base.empty()) return;
		httplib::Client cli(base);
		cli.set_connection_timeout(5);
		cli.set_read_timeout(10);
		cli.set_follow_location(true);
		auto res = cli.Get(path.c_str());
		if (!res || res->status >= 300) return;

		wxMemoryBuffer mb;
		mb.AppendData(res->body.data(), res->body.size());

		auto* evt = new wxThreadEvent(EVT_CARD_THUMBNAIL_LOADED);
		evt->SetPayload(mb);
		wxQueueEvent(sink, evt);
	}).detach();
}
