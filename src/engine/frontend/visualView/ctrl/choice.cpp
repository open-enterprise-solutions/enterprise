
#include "widgets.h"
#include "backend/compiler/procUnit.h"

wxIMPLEMENT_DYNAMIC_CLASS(ibValueChoice, ibValueWindow)

//****************************************************************************
//*                             Choice                                       *
//****************************************************************************

ibValueChoice::ibValueChoice() : ibValueWindow()
{
}

wxObject* ibValueChoice::Create(wxWindow* wxparent, ibVisualHost* visualHost)
{
	wxChoice* choice = new wxChoice(wxparent, wxID_ANY,
		wxDefaultPosition,
		wxDefaultSize);

	return choice;
}

void ibValueChoice::OnCreated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost, bool firstСreated)
{
}

void ibValueChoice::Update(wxObject* wxobject, ibVisualHost* visualHost)
{
	wxChoice* choice = dynamic_cast<wxChoice*>(wxobject);

	if (choice != nullptr) {
	}

	UpdateWindow(choice);
}

void ibValueChoice::Cleanup(wxObject* obj, ibVisualHost* visualHost)
{
}

//*******************************************************************
//*								Data	                            *
//*******************************************************************

bool ibValueChoice::LoadData(ibReaderMemory& reader)
{
	return ibValueWindow::LoadData(reader);
}

bool ibValueChoice::SaveData(ibWriterMemory writer)
{
	return ibValueWindow::SaveData(writer);
}