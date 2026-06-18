
#include "widgets.h"
#include "backend/serialize/dataBuilder.h"   // ibDataNode (control -> node)
#include "backend/compiler/procUnit.h"


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

bool ibValueListBox::ReadData(const ibDataNode& node)
{
	return ibValueWindow::ReadData(node);
}

bool ibValueListBox::WriteData(ibDataNode& node) const
{
	return ibValueWindow::WriteData(node);
}
