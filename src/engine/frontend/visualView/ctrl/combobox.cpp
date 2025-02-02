
#include "widgets.h"
#include "backend/compiler/procUnit.h"

wxIMPLEMENT_DYNAMIC_CLASS(CValueComboBox, IValueWindow)

//****************************************************************************
//*                             ComboBox                                     *
//****************************************************************************

CValueComboBox::CValueComboBox() : IValueWindow()
{
}

wxObject* CValueComboBox::Create(wxWindow* wxparent, IVisualHost* visualHost)
{
	wxComboBox* combobox = new wxComboBox(wxparent, wxID_ANY,
		wxEmptyString, 
		wxDefaultPosition,
		wxDefaultSize);

	return combobox;
}

void CValueComboBox::OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost, bool first—reated)
{
}

void CValueComboBox::Update(wxObject* wxobject, IVisualHost* visualHost)
{
	wxComboBox* combobox = dynamic_cast<wxComboBox*>(wxobject);

	if (combobox != nullptr) {
	}

	UpdateWindow(combobox);
}

void CValueComboBox::Cleanup(wxObject* obj, IVisualHost* visualHost)
{
}

//*******************************************************************
//*								 Data		                        *
//*******************************************************************

bool CValueComboBox::LoadData(CMemoryReader& reader)
{
	return IValueWindow::LoadData(reader);
}

bool CValueComboBox::SaveData(CMemoryWriter& writer)
{
	return IValueWindow::SaveData(writer);
}