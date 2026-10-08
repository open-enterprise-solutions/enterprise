#ifndef _FRMCLIENT_VIEW_GRID_BOX_H__
#define _FRMCLIENT_VIEW_GRID_BOX_H__

#include <memory>

#include "frmclient/visualView/ctrl/frame.h"

class ibViewCommandBar;
class ibGridEvent;

// THE SPREADSHEET CONTROL — the desktop's (frontend/visualView/ctrl/gridBox): its command bar above the sheet, which is
// a DOCUMENT and its VIEW, held as long as the box lives (ibSpreadsheetGridBoxDocument / ibSpreadsheetGridBoxView). The
// view's editor draws the sheet as the desktop's does, and while the box is the active control the form's view hands
// the view its menu, its toolbar and its commands (GetControlView).
//
// The sheet is the server's: read whole when the sheet on show is another (its State's Version) and put into the
// document; changed here, it goes back whole, as Change {Sheet}; a cell double-clicked goes back as Cell {Row, Col},
// counted from 1.
class ibValueGridBox : public ibValueFrame {
public:

	ibValueGridBox(ibVisualHostClient& host, long long controlId);
	virtual ~ibValueGridBox();

	virtual void Create(wxWindow* parent, const ibProtocolNode& node) override;
	virtual void Update(const ibProtocolNode& node) override;
	virtual wxWindow* GetWindow() const override;

	// The view of the document this box holds — the form's view is a facade over it while the box is the active
	// control (menu, toolbar, commands, printing, saving).
	virtual ibFrontendView* GetControlView() const override;

private:

	// The document changed — its sheet, as the editor left it, handed over.
	void OnSheetModified(const struct ibSpreadsheetDescription& spreadsheetDesc);
	void OnCellLeftClick(ibGridEvent& event);

	wxPanel*                          m_panel = nullptr;
	std::unique_ptr<ibViewCommandBar> m_commandBar;
	ibViewFetcher                     m_fetcher;

	long long m_version = -1;   // the sheet on show — -1: none yet

	// The document (it holds the sheet on show) and its view, both as long as the box lives — held side by side so
	// nothing is cast to find them. The box's Create creates the view, its going closes it.
	class ibSpreadsheetGridBoxDocument* m_gridDocument = nullptr;
	class ibSpreadsheetGridBoxView*     m_gridView = nullptr;
};

#endif
