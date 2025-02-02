
#include "widgets.h"
#include "backend/compiler/procUnit.h"

wxIMPLEMENT_DYNAMIC_CLASS(CValueListBox, IValueWindow)

//****************************************************************************
//*                             Listbox                                      *
//****************************************************************************

CValueListBox::CValueListBox() : IValueWindow()
{
}

wxObject* CValueListBox::Create(wxWindow* wxparent, IVisualHost* visualHost)
{
	wxListBox* m_listbox = new wxListBox(wxparent, wxID_ANY,
		wxDefaultPosition,
		wxDefaultSize,
		0,
		nullptr);

	return m_listbox;
}

void CValueListBox::OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost, bool first—reated)
{
}

void CValueListBox::Update(wxObject* wxobject, IVisualHost* visualHost)
{
	wxListBox* listbox = dynamic_cast<wxListBox*>(wxobject);

	if (listbox != nullptr) {
	}

	UpdateWindow(listbox);
}

void CValueListBox::Cleanup(wxObject* obj, IVisualHost* visualHost)
{
}

//*******************************************************************
//*								Data	                            *
//*******************************************************************

bool CValueListBox::LoadData(CMemoryReader& reader)
{
	return IValueWindow::LoadData(reader);
}

bool CValueListBox::SaveData(CMemoryWriter& writer)
{
	return IValueWindow::SaveData(writer);
}
