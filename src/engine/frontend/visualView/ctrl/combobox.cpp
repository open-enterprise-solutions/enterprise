
#include "widgets.h"
#include "backend/serialize/dataBuilder.h"   // ibDataNode (control -> node)
#include "backend/compiler/procUnit.h"


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

bool ibValueComboBox::ReadData(const ibDataNode& node)
{
	return ibValueWindow::ReadData(node);
}

bool ibValueComboBox::WriteData(ibDataNode& node) const
{
	return ibValueWindow::WriteData(node);
}