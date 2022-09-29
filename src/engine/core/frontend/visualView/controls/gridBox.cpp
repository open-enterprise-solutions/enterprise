#include "gridbox.h"
#include "frontend/visualView/visualEditor.h"

//***********************************************************************************
//*                           IMPLEMENT_DYNAMIC_CLASS                               *
//***********************************************************************************

wxIMPLEMENT_DYNAMIC_CLASS(CValueGridBox, IValueWindow);

//***********************************************************************************
//*                                 Value Notebook                                  *
//***********************************************************************************

CValueGridBox::CValueGridBox() : IValueWindow()
{	
	//set default params
	*m_propertyMinSize = wxSize(300, 100);
}

wxObject* CValueGridBox::Create(wxObject* parent, IVisualHost *visualHost)
{
	return new CGrid((wxWindow*)parent, wxID_ANY, wxDefaultPosition, wxDefaultSize);
}

void CValueGridBox::OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost *visualHost, bool first—reated)
{
	CGrid *m_grid = dynamic_cast<CGrid *>(wxobject);
}

void CValueGridBox::OnSelected(wxObject* wxobject)
{
}

void CValueGridBox::Update(wxObject* wxobject, IVisualHost *visualHost)
{
	CGrid *m_grid = dynamic_cast<CGrid *>(wxobject);

	if (m_grid) {
	}

	UpdateWindow(m_grid);
}

void CValueGridBox::Cleanup(wxObject* wxobject, IVisualHost *visualHost)
{
}

//**********************************************************************************
//*                                   Data										   *
//**********************************************************************************

bool CValueGridBox::LoadData(CMemoryReader &reader)
{
	return IValueWindow::LoadData(reader);
}

bool CValueGridBox::SaveData(CMemoryWriter &writer)
{
	return IValueWindow::SaveData(writer);
}