#include "menuBar.h"

ibPanelMenuBar::ibPanelMenuBar()
{
	m_sizer = nullptr;
}

ibPanelMenuBar::ibPanelMenuBar(wxWindow* parent, int id, const wxPoint& pos, const wxSize& size, long style, const wxString& name) :
	wxPanel(parent, id, pos, size, style, name)
{
	wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
	m_sizer = new wxBoxSizer(wxHORIZONTAL);
	m_sizer->Add(new wxStaticText(this, wxID_ANY, wxT(" ")), 0, wxRIGHT | wxLEFT, 0);
	mainSizer->Add(m_sizer, 1, wxTOP | wxBOTTOM, 3);
	SetSizer(mainSizer);
	SetBackgroundColour(wxColour(0xB8, 0xC9, 0xD4));  // #B8C9D4 powder blue (was system BTNFACE)
}

ibPanelMenuBar::~ibPanelMenuBar()
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

void ibPanelMenuBar::AppendMenu(wxMenu* menu, const wxString& name)
{
	wxStaticText* st = new wxStaticText(this, wxID_ANY, name);
	st->PushEventHandler(new ibMenuEvtHandler(st, menu));
	m_sizer->Add(st, 0, wxALIGN_LEFT | wxRIGHT | wxLEFT, FromDIP(5));
	Layout();
	m_menus.push_back(menu);
}

wxMenu* ibPanelMenuBar::Remove(int /*i*/)
{
	return nullptr;  // TODO: Implementar ibPanelMenuBar::Remove
}

wxBEGIN_EVENT_TABLE(ibMenuEvtHandler, wxEvtHandler)
EVT_LEFT_DOWN(ibMenuEvtHandler::OnMouseEvent)
wxEND_EVENT_TABLE()

ibMenuEvtHandler::ibMenuEvtHandler(wxStaticText* st, wxMenu* menu)
{
	wxASSERT(menu != nullptr && st != nullptr);
	m_label = st;
	m_menu = menu;
}

void ibMenuEvtHandler::OnMouseEvent(wxMouseEvent&)
{
	m_label->PopupMenu(m_menu, 0, m_label->GetSize().y + 3);
}

