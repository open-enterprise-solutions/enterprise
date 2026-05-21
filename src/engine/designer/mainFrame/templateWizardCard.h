/////////////////////////////////////////////////////////////////////////////
// ibTemplateCard — single template card for the Wizard's gallery page.
//
// Layout:
//   [ thumbnail bitmap or placeholder ]   ← 200x120
//   [ Template name (bold)            ]
//   [ Description (3-4 line ellipsis) ]
//   [ N obj · M rows · v1.0           ]   ← stats badge
//   [ #tag1 #tag2 #tag3               ]
//
// Click fires a wxEVT_BUTTON with the card's wxID; the wizard's
// gallery-page sizer routes that through OnGalleryCardClicked which
// records the templateId and advances to Page 2.
//
// Thumbnail fetch is fire-and-forget on a worker thread (cpp-httplib
// against the thumbnailUrl); on success we wxQueueEvent the decoded
// bitmap back to the card's wxThreadEvent handler. Failure falls back
// to a generic placeholder so the card never blocks the gallery render.
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_TEMPLATE_WIZARD_CARD_H_
#define _IB_TEMPLATE_WIZARD_CARD_H_

#include <wx/panel.h>
#include <wx/string.h>
#include <wx/bitmap.h>
#include <wx/event.h>
#include <functional>

class wxStaticText;
class wxStaticBitmap;

class ibTemplateCard : public wxPanel {
public:
	using ClickCallback = std::function<void(const wxString& templateId)>;

	ibTemplateCard(wxWindow* parent,
	                const wxString& templateId,
	                const wxString& name,
	                const wxString& description,
	                const wxString& statsLabel,
	                const wxString& tagsLine,
	                const wxString& thumbnailUrl,
	                ClickCallback   onClick);

	~ibTemplateCard() override = default;

	const wxString& TemplateId() const { return m_templateId; }

private:
	// Click on anything inside the card fires the click callback.
	void OnAnyClick(wxMouseEvent& event);

	// Hover highlighting — draws a subtle border via SetBackgroundColour
	// on the outer panel. The colour pair is intentionally subtle so the
	// gallery doesn't flicker as the cursor sweeps across cards.
	void OnEnterWindow(wxMouseEvent& event);
	void OnLeaveWindow(wxMouseEvent& event);

	// Thumbnail loaded asynchronously — wxThreadEvent carries the bitmap.
	void OnThumbnailLoaded(wxThreadEvent& event);

	// Kicks off the worker that downloads m_thumbnailUrl. Skipped when
	// the URL is empty (placeholder stays).
	void StartThumbnailFetch();

private:
	wxString        m_templateId;
	wxString        m_thumbnailUrl;
	ClickCallback   m_onClick;

	wxStaticBitmap* m_thumb     = nullptr;
	wxStaticText*   m_nameLabel = nullptr;
	wxStaticText*   m_descLabel = nullptr;
	wxStaticText*   m_statsLabel= nullptr;
	wxStaticText*   m_tagsLabel = nullptr;

	bool m_hover = false;
};

#endif // _IB_TEMPLATE_WIZARD_CARD_H_
