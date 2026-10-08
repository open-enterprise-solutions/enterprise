////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : a spreadsheet of a view
////////////////////////////////////////////////////////////////////////////

#include "gridBox.h"

#include "frmclient/visualView/commandBar.h"
#include "frmclient/backend/serialize/dataProtocol.h"           // the sheet as the wire brought it
#include "frmclient/docView/templates/docViewSpreadsheet.h"    // the document it holds, and its view

//***********************************************************************************
//*                                    gridBox                                      *
//***********************************************************************************

ibValueGridBox::ibValueGridBox(ibVisualHostClient& host, long long controlId) : ibValueFrame(host, controlId),
m_gridDocument(new ibSpreadsheetGridBoxDocument()),
m_gridView(new ibSpreadsheetGridBoxView())                // empty until Create
{
	m_gridView->SetDocument(m_gridDocument);
	m_gridDocument->SetModifyHandler([this](const ibSpreadsheetDescription& spreadsheetDesc) { OnSheetModified(spreadsheetDesc); });
}

ibValueGridBox::~ibValueGridBox()
{
	// The view is closed, left empty — the editor went with the box's window — and goes first: it is the document's.
	m_gridView->Close(false);

	wxDELETE(m_gridView);
	wxDELETE(m_gridDocument);
}

ibFrontendView* ibValueGridBox::GetControlView() const
{
	// An empty view (closed, not created again) has nothing to hand on.
	return m_gridView->GetGridCtrl() != nullptr ? m_gridView : nullptr;
}

void ibValueGridBox::Create(wxWindow* parent, const ibProtocolNode& /*node*/)
{
	m_panel = new wxPanel(parent, wxID_ANY);
	wxBoxSizer* const sizer = new wxBoxSizer(wxVERTICAL);

	// The chrome: its command bar above it.
	m_commandBar = std::make_unique<ibViewCommandBar>(m_panel, [this](const ibProtocolNode& args) {
		Send(ibProtocolEvent::Command, args);
	});
	sizer->Add(m_commandBar->GetWindow(), 0, wxEXPAND);

	// The box's view is created the way a document's view is — its frame is the box's window, and its OnCreate makes
	// the editor.
	m_gridView->SetFrame(m_panel);
	if (!m_gridView->OnCreate(m_gridDocument, 0))
		return;

	ibGridEditor* const gridWindow = m_gridView->GetGridCtrl();

	gridWindow->EnableProperty(true);
	gridWindow->EnableGridArea(false);
	gridWindow->EnableGridLines(false);

	sizer->Add(gridWindow, 1, wxEXPAND);
	m_panel->SetSizer(sizer);
	// ⚠ A SIZE OF ITS OWN, as the table's — never the grid's best one: that is every row the grid holds, and the grid
	// fills itself with rows to the size it is given (FillVisibleArea), so a view that scrolls to fit it grew without
	// end. A form's box is given its MinimumSize by the server; a document's sheet in a tab is given none.
	m_panel->SetMinSize(m_panel->FromDIP(wxSize(150, 75)));

	m_fetcher = MakeFetcher();

	// A DOUBLE left click opens the cell's value — the editor's own event, the question asked of the server: what this
	// figure is made of is its sheet's to say.
	gridWindow->Bind(wxEVT_GRID_CELL_LEFT_DCLICK, &ibValueGridBox::OnCellLeftClick, this);
}

void ibValueGridBox::Update(const ibProtocolNode& node)
{
	const ibProtocolNode state = node.FindChild(ibProtocolName::State);

	m_commandBar->Update(state.FindChild(ibProtocolName::CommandBar));

	ibGridEditor* const gridWindow = m_gridView->GetGridCtrl();
	if (gridWindow == nullptr)
		return;

	// ANOTHER SHEET ON SHOW — composed again, replaced: read whole, into a document's sheet of its own, which the
	// editor then shows as the desktop's shows the one its model holds.
	const long long version = state.GetInt(ibProtocolName::Version);
	if (version != m_version) {
		ibProtocolNode answer;
		if (m_fetcher(ibProtocolNode(), answer) && answer.GetInt(ibProtocolName::Version) == version) {
			m_version = version;

			wxObjectDataPtr<ibBackendSpreadsheetObject> document(new ibBackendSpreadsheetObject);
			auto sheet = std::make_shared<ibDataNode>();
			ibReadProtocolNode(answer.FindChild(ibProtocolName::Sheet), *sheet);
			ibSpreadsheetDescriptionMemory::ReadNode(ibDataValue::Child(sheet), document->GetSpreadsheetDesc());
			document->EnableEditing(!answer.GetBool(ibProtocolName::ReadOnly));
			m_gridDocument->SetSpreadsheetDocument(document);
		}
	}

	// A report being built — «Composing…» over the sheet until it is delivered.
	gridWindow->ShowComposeProgress(state.GetBool(ibProtocolName::Composing));
	// The grid lines a document's sheet shows; a report's are its cells' borders.
	gridWindow->EnableGridLines(state.GetBool(ibProtocolName::GridLines));

	UpdateWindow(m_panel, node);
	// …and its command bar takes the box's look (the desktop's ApplyLook(part)).
	ApplyLook(m_commandBar->GetWindow(), node);
}

wxWindow* ibValueGridBox::GetWindow() const
{
	return m_panel;
}

//*******************************************************************
//*                             Events                              *
//*******************************************************************

// ⭐ THE SHEET CHANGED HERE IS HANDED TO THE SERVER — whole, in the form it came in: the server's sheet from then on,
// and a document's tab marked modified by the server.
void ibValueGridBox::OnSheetModified(const ibSpreadsheetDescription& spreadsheetDesc)
{
	ibDataValue sheet;
	if (!ibSpreadsheetDescriptionMemory::WriteNode(sheet, spreadsheetDesc))
		return;

	ibProtocolNode args;
	ibWriteProtocolNode(*sheet.AsChild(), args.Child(ibProtocolName::Sheet));
	Send(ibProtocolEvent::Change, args);
}

void ibValueGridBox::OnCellLeftClick(ibGridEvent& event)
{
	ibProtocolNode args;
	args.SetValue(ibProtocolName::Row, event.GetRow() + 1)
		.SetValue(ibProtocolName::Col, event.GetCol() + 1);
	Send(ibProtocolEvent::Cell, args);
}
