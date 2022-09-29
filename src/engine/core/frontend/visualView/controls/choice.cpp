
#include "widgets.h"
#include "compiler/procUnit.h"

wxIMPLEMENT_DYNAMIC_CLASS(CValueChoice, IValueWindow)

//****************************************************************************
//*                             Choice                                       *
//****************************************************************************

CValueChoice::CValueChoice() : IValueWindow()
{
}

wxObject* CValueChoice::Create(wxObject* parent, IVisualHost* visualHost)
{
	wxChoice* choice = new wxChoice((wxWindow*)parent, wxID_ANY,
		wxDefaultPosition,
		wxDefaultSize);

	return choice;
}

void CValueChoice::OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost, bool first—reated)
{
}

void CValueChoice::Update(wxObject* wxobject, IVisualHost* visualHost)
{
	wxChoice* choice = dynamic_cast<wxChoice*>(wxobject);

	if (choice != NULL) {
	}

	UpdateWindow(choice);
}

void CValueChoice::Cleanup(wxObject* obj, IVisualHost* visualHost)
{
}

//*******************************************************************
//*								Data	                            *
//*******************************************************************

bool CValueChoice::LoadData(CMemoryReader& reader)
{
	return IValueWindow::LoadData(reader);
}

bool CValueChoice::SaveData(CMemoryWriter& writer)
{
	return IValueWindow::SaveData(writer);
}