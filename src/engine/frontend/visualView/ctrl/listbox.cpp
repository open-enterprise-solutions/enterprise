
#include "widgets.h"
#include "backend/compiler/procUnit.h"

wxIMPLEMENT_DYNAMIC_CLASS(ibValueListBox, ibValueWindow)

//****************************************************************************
//*                             Listbox                                      *
//****************************************************************************

ibValueListBox::ibValueListBox() : ibValueWindow()
{
}

wxObject* ibValueListBox::Create(wxWindow* wxparent, ibVisualHost* visualHost)
{
	wxListBox* m_listbox = new wxListBox(wxparent, wxID_ANY,
		wxDefaultPosition,
		wxDefaultSize,
		0,
		nullptr);

	return m_listbox;
}

void ibValueListBox::OnCreated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost, bool firstСreated)
{
}

void ibValueListBox::Update(wxObject* wxobject, ibVisualHost* visualHost)
{
	wxListBox* listbox = dynamic_cast<wxListBox*>(wxobject);

	if (listbox != nullptr) {
	}

	UpdateWindow(listbox);
}

void ibValueListBox::Cleanup(wxObject* obj, ibVisualHost* visualHost)
{
}

//*******************************************************************
//*								Data	                            *
//*******************************************************************

bool ibValueListBox::LoadData(ibReaderMemory& reader)
{
	return ibValueWindow::LoadData(reader);
}

bool ibValueListBox::SaveData(ibWriterMemory writer)
{
	return ibValueWindow::SaveData(writer);
}
