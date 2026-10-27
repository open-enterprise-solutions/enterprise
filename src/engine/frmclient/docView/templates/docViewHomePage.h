#ifndef _FRMCLIENT_DOCVIEW_HOME_PAGE_H__
#define _FRMCLIENT_DOCVIEW_HOME_PAGE_H__

// Home page — the old desktop's composite doc/view, in a tab the server
// opened (the tab's Kind HomePage). The forms are the server's, each opened there as a child of its page: the client
// lays the columns out as the desktop did and draws each cell's form in a host of its own — what the person does in a
// cell goes to that form by its key (ibVisualHostClient), so the page's forms are told apart.

#include "frmclient/docView/tabDocument.h"
#include "frmclient/visualView/visualHostClient.h"   // ibVisualHostView — a cell's form the person is in, its facade

#include <vector>

class wxSizer;
class wxStaticText;

class FRMCLIENT_API ibHomePageView : public ibVisualHostView {
public:

	// The cells paint themselves (each is a form host) — the view has nothing to draw.
	virtual void OnDraw(wxDC* WXUNUSED(dc)) override {}

	// The host a window of the page is in — its cell's; none: a cell's header, a sash.
	virtual ibVisualHostClient* FindVisualHost(wxWindow* window) const override;

	// The tab's View — its columns, and in each the cells — drawn: laid out the first time, each cell's form drawn by
	// what the patch changed of it (none: whole).
	void Draw(const ibProtocolNode& view, const ibProtocolNode& patch);

private:

	// One cell — its header's caption, and the host its form is drawn in (none: the form is not available).
	struct ibHomePageCell {
		wxStaticText*       m_caption = nullptr;
		ibVisualHostClient* m_host = nullptr;
	};

	// Build a column: its cells stacked one under another with a draggable sash between each pair, weighted by their
	// heights. Returns the window that fills the slot — a cell's pane when one is left, a splitter when there are more.
	wxWindow* BuildColumn(wxWindow* parent, const std::vector<ibProtocolNode>& cells, size_t from);
	// One cell's pane in `parent` (the splitter): its header, and its form's host filling the rest — or what says the
	// form is not available.
	wxWindow* OpenCell(wxWindow* parent, const ibProtocolNode& cell);
	// The strip above a cell — its icon and its title, as the server sent them; returns the title's text.
	wxStaticText* BuildCellHeader(wxWindow* pane, wxSizer* paneSizer, const ibProtocolNode& cell);

	wxWindow* m_workspace = nullptr;
	std::vector<ibHomePageCell> m_cells;   // column by column, top to bottom — as the view lists them
};

// The tab of the start page — its view, the forms laid out.
class FRMCLIENT_API ibHomePageDocument : public ibTabDocument {
public:

	explicit ibHomePageDocument(long long tabId);

	// The page drawn — the frame's View, as the patch changed it.
	virtual void DrawView(const ibProtocolNode& view, const ibProtocolNode& patch) override;

	// While the active control of the form the person is in is a document of its own, the undo is that document's.
	virtual wxCommandProcessor* GetCommandProcessor() const override;

	ibHomePageView* GetFirstView() const;

protected:

	virtual ibFrontendView* DoCreateView() override;
};

#endif
