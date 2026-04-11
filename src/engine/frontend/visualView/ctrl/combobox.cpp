
#include "widgets.h"
#include "backend/compiler/procUnit.h"

wxIMPLEMENT_DYNAMIC_CLASS(ibValueComboBox, ibValueWindow)

//****************************************************************************
//*                             ComboBox                                     *
//****************************************************************************

ibValueComboBox::ibValueComboBox() : ibValueWindow()
{
}

wxObject* ibValueComboBox::Create(wxWindow* wxparent, ibVisualHost* visualHost)
{
	wxComboBox* combobox = new wxComboBox(wxparent, wxID_ANY,
		wxEmptyString, 
		wxDefaultPosition,
		wxDefaultSize);

	return combobox;
}

void ibValueComboBox::OnCreated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost, bool firstСreated)
{
}

void ibValueComboBox::Update(wxObject* wxobject, ibVisualHost* visualHost)
{
	wxComboBox* combobox = dynamic_cast<wxComboBox*>(wxobject);

	if (combobox != nullptr) {
	}

	UpdateWindow(combobox);
}

void ibValueComboBox::Cleanup(wxObject* obj, ibVisualHost* visualHost)
{
}

//*******************************************************************
//*								 Data		                        *
//*******************************************************************

bool ibValueComboBox::LoadData(ibReaderMemory& reader)
{
	return ibValueWindow::LoadData(reader);
}

bool ibValueComboBox::SaveData(ibWriterMemory writer)
{
	return ibValueWindow::SaveData(writer);
}