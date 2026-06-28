#include "gridbox.h"
#include "backend/serialize/dataBuilder.h"   // ibDataNode (control -> node)

//***********************************************************************************
//*                           IMPLEMENT_DYNAMIC_CLASS                               *
//***********************************************************************************


//***********************************************************************************
//*                                 Value Notebook                                  *
//***********************************************************************************

ibValueGridBox::ibValueGridBox() : ibValueWindow(),
m_valueSpreadsheet(new ibValueSpreadsheetDocument())
{
	m_members.Bind(this, &ibValueGridBox::FillControlMembers);
	//set default params
	m_propertyMinSize->SetValue(wxSize(150, 50));
}

#include "frontend/visualView/ctrl/form.h"

wxObject* ibValueGridBox::Create(wxWindow* wxparent, ibVisualHost* visualHost)
{
	ibGridEditor* gridWindow = new ibGridEditor(nullptr, wxparent, wxID_ANY, wxDefaultPosition, wxDefaultSize);

	gridWindow->EnableProperty(!visualHost->IsDesignerHost());
	gridWindow->EnableGridArea(false);
	gridWindow->EnableGridLines(false);

	gridWindow->LoadDocument(m_valueSpreadsheet->GetSpreadsheetDocument());

	return gridWindow;
}

void ibValueGridBox::OnCreated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost, bool firstСreated)
{
	ibGridEditor* gridWindow = dynamic_cast<ibGridEditor*>(wxobject);
}

void ibValueGridBox::OnSelected(wxObject* wxobject)
{
}

void ibValueGridBox::Update(wxObject* wxobject, ibVisualHost* visualHost)
{
	ibGridEditor* gridWindow = dynamic_cast<ibGridEditor*>(wxobject);

	if (gridWindow) {
	}

	UpdateWindow(gridWindow);
}

void ibValueGridBox::Cleanup(wxObject* wxobject, ibVisualHost* visualHost)
{
}

//**********************************************************************************

#include "frontend/win/editor/gridEditor/gridPrintout.h"

wxPrintout* ibValueGridBox::CreatePrintout() const
{
	ibGridEditor* gridWindow = dynamic_cast<ibGridEditor*>(GetWxObject());
	if (gridWindow != nullptr)
		return gridWindow->CreatePrintout();

	return nullptr;
}

//**********************************************************************************
//*                                   Data										   *
//**********************************************************************************

bool ibValueGridBox::ReadData(const ibDataNode& node)
{
	ibSpreadsheetDescriptionMemory::ReadNode(node.GetProperty(wxT("Spreadsheet")), m_valueSpreadsheet->GetSpreadsheetDesc());	
	return ibValueWindow::ReadData(node);
}

bool ibValueGridBox::WriteData(ibDataNode& node) const
{
	ibDataValue spreadsheet;
	ibSpreadsheetDescriptionMemory::WriteNode(spreadsheet, m_valueSpreadsheet->GetSpreadsheetDesc());
	node.SetProperty(wxT("Spreadsheet"), spreadsheet);
	return ibValueWindow::WriteData(node);
}

//***********************************************************************************

enum prop {
	eGridValue,
};

void ibValueGridBox::FillControlMembers(ibMemberTable& helper) const
{
	helper.AppendProp(wxT("Value"), eGridValue, eControl);
}

bool ibValueGridBox::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	const long lPropAlias = m_members.GetPropAlias(lPropNum); bool refreshColumn = false;
	if (lPropAlias == eControl) {
		const long lPropData = m_members.GetPropData(lPropNum);
		if (lPropData == eGridValue) {
			ibGridEditor* gridWindow = dynamic_cast<ibGridEditor*>(GetWxObject());
			m_valueSpreadsheet = varPropVal.ConvertToType<ibValueSpreadsheetDocument>();
			if (gridWindow != nullptr) gridWindow->LoadDocument(m_valueSpreadsheet->GetSpreadsheetDesc());
		}
	}

	return ibValueFrame::SetPropVal(lPropNum, varPropVal);
}

bool ibValueGridBox::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	const long lPropAlias = m_members.GetPropAlias(lPropNum);
	if (lPropAlias == eControl) {
		const long lPropData = m_members.GetPropData(lPropNum);
		if (lPropData == eGridValue) {
			pvarPropVal = m_valueSpreadsheet;
			return true;
		}
	}
	return ibValueFrame::GetPropVal(lPropNum, pvarPropVal);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_TYPE_REGISTER(ibValueGridBox, "Gridbox", "Container", control_to_clsid("CT_GRID"));
