#include "menuBar.h"

CPanelMenuBar::CPanelMenuBar()
{
	m_sizer = nullptr;
}

CPanelMenuBar::CPanelMenuBar(wxWindow* parent, int id, const wxPoint& pos, const wxSize& size, long style, const wxString& name) :
	wxPanel(parent, id, pos, size, style, name)
{
	wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
	m_sizer = new wxBoxSizer(wxHORIZONTAL);
	m_sizer->Add(new wxStaticText(this, wxID_ANY, wxT(" ")), 0, wxRIGHT | wxLEFT, 0);
	mainSizer->Add(m_sizer, 1, wxTOP | wxBOTTOM, 3);
	SetSizer(mainSizer);
	SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE));
}

CPanelMenuBar::~CPanelMenuBar()
{
	while (!m_menus.empty()) {
		wxMenu* menu = m_menus[0];
		delete menu;
		m_menus.erase(m_menus.begin());
	}

	// Delete spawned event handlers
	wxSizerItemList& labels = m_sizer->GetChildren();
	for (wxSizerItemList::iterator label = labels.begin(); label != labels.end(); ++label) {
		wxStaticText* text = dynamic_cast<wxStaticText*>((*label)->GetWindow());
		if (text != 0) {
			if (text->GetEventHandler() != text) {
				text->PopEventHandler(true);
			}
		}
	}
}

void CPanelMenuBar::AppendMenu(wxMenu* menu, const wxString& name)
{
	wxStaticText* st = new wxStaticText(this, wxID_ANY, name);
	st->PushEventHandler(new CMenuEvtHandler(st, menu));
	m_sizer->Add(st, 0, wxALIGN_LEFT | wxRIGHT | wxLEFT, 5);
	Layout();
	m_menus.push_back(menu);
}

wxMenu* CPanelMenuBar::Remove(int /*i*/)
{
	return nullptr;  // TODO: Implementar CPanelMenuBar::Remove
}

wxBEGIN_EVENT_TABLE(CMenuEvtHandler, wxEvtHandler)
EVT_LEFT_DOWN(CMenuEvtHandler::OnMouseEvent)
wxEND_EVENT_TABLE()

CMenuEvtHandler::CMenuEvtHandler(wxStaticText* st, wxMenu* menu)
{
	wxASSERT(menu != nullptr && st != nullptr);
	m_label = st;
	m_menu = menu;
}

void CMenuEvtHandler::OnMouseEvent(wxMouseEvent&)
{
	m_label->PopupMenu(m_menu, 0, m_label->GetSize().y + 3);
}

