#include "aboutWnd.h"

#include "appData.h"

#include <wx/html/htmlwin.h>
#include <wx/mimetype.h>

#define ID_OK 1000

BEGIN_EVENT_TABLE(CAboutDialogWnd, wxDialog)
EVT_BUTTON(ID_OK, CAboutDialogWnd::OnButtonEvent)
END_EVENT_TABLE()

#include "core.h"

CAboutDialogWnd::CAboutDialogWnd(wxWindow *parent, int id) : wxDialog(parent, id, _("About..."))
{
	wxBoxSizer *sizer2 = new wxBoxSizer(wxVERTICAL);
	m_staticText2 = new wxStaticText(this, wxID_ANY, wxString::Format("Open enterprise solutions, build %i", GetBuildId()), wxDefaultPosition, wxDefaultSize, 0);
	m_staticText2->SetFont(wxFont(12, (wxFontFamily)74, (wxFontStyle)90, (wxFontWeight)92, false, wxT("Arial")));
	sizer2->Add(m_staticText2, 0, wxALL | wxALIGN_CENTER_HORIZONTAL, 5);
	m_staticText3 = new wxStaticText(this, wxID_ANY, _("a RAD tool powered by wxWidgets framework"), wxDefaultPosition, wxDefaultSize, 0);
	sizer2->Add(m_staticText3, 0, wxALL | wxALIGN_CENTER_HORIZONTAL, 5);
	m_staticText6 = new wxStaticText(this, wxID_ANY, _("(C) 2022 Maxim 'nouverbe' Kornienko"), wxDefaultPosition, wxDefaultSize, 0);
	sizer2->Add(m_staticText6, 0, wxALL | wxALIGN_CENTER_HORIZONTAL, 5);
	window1 = new wxStaticLine(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_HORIZONTAL);
	sizer2->Add(window1, 0, wxALL | wxEXPAND, 5);
	m_panel1 = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSUNKEN_BORDER | wxTAB_TRAVERSAL);
	wxBoxSizer *sizer3;
	sizer3 = new wxBoxSizer(wxVERTICAL);
	m_staticText8 = new wxStaticText(m_panel1, wxID_ANY, wxT("Thanks:"), wxDefaultPosition, wxDefaultSize, 0);
	sizer3->Add(m_staticText8, 0, wxALL, 5);
	m_staticText9 = new wxStaticText(m_panel1, wxID_ANY, wxT("- wxWidgets team, wxFormBuilder team, 2C, Tomasz Sowa etc..."), wxDefaultPosition, wxDefaultSize, 0);
	sizer3->Add(m_staticText9, 0, wxALL, 5);
	m_panel1->SetSizer(sizer3);
	m_panel1->SetAutoLayout(true);
	m_panel1->Layout();
	sizer2->Add(m_panel1, 1, wxALL | wxEXPAND, 5);
	window2 = new wxStaticLine(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_HORIZONTAL);
	sizer2->Add(window2, 0, wxALL | wxEXPAND, 5);
	m_button1 = new wxButton(this, ID_OK, wxT("&OK"), wxDefaultPosition, wxDefaultSize, 0);
	sizer2->Add(m_button1, 0, wxALL | wxALIGN_CENTER_HORIZONTAL, 5);
	this->SetSizer(sizer2);
	this->SetAutoLayout(true);
	this->Layout();

	Center();
}

void CAboutDialogWnd::OnButtonEvent(wxCommandEvent &)
{
	Close();
}
