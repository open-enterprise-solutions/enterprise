
#include "widgets.h"
#include "backend/serialize/dataBuilder.h"   // ibDataNode (control -> node)
#include "backend/compiler/procUnit.h"


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

void ibValueChoice::OnCreated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost, bool firstCreated)
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

bool ibValueChoice::ReadData(const ibDataNode& node)
{
	return ibValueWindow::ReadData(node);
}

bool ibValueChoice::WriteData(ibDataNode& node) const
{
	return ibValueWindow::WriteData(node);
}