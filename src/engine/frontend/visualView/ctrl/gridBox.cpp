#include "gridbox.h"

//***********************************************************************************
//*                           IMPLEMENT_DYNAMIC_CLASS                               *
//***********************************************************************************

wxIMPLEMENT_DYNAMIC_CLASS(ibValueGridBox, ibValueWindow);

//***********************************************************************************
//*                                 Value Notebook                                  *
//***********************************************************************************

ibValueGridBox::ibValueGridBox() : ibValueWindow(), 
m_valueSpreadsheet(ibValue::CreateAndPrepareValueRef<ibValueSpreadsheetDocument>())
{
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

bool ibValueGridBox::LoadData(ibReaderMemory& reader)
{
	ibSpreadsheetDescriptionMemory::LoadData(reader, m_valueSpreadsheet->GetSpreadsheetDesc());
	return ibValueWindow::LoadData(reader);
}

bool ibValueGridBox::SaveData(ibWriterMemory writer)
{
	ibSpreadsheetDescriptionMemory::SaveData(writer, m_valueSpreadsheet->GetSpreadsheetDesc());
	return ibValueWindow::SaveData(writer);
}

//***********************************************************************************

enum prop {
	eGridValue,
};

void ibValueGridBox::PrepareNames() const
{
	ibValueFrame::PrepareNames();

	m_methodHelper->AppendProp(wxT("Value"), eGridValue, eControl);
}

bool ibValueGridBox::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	const long lPropAlias = m_methodHelper->GetPropAlias(lPropNum); bool refreshColumn = false;
	if (lPropAlias == eControl) {
		const long lPropData = m_methodHelper->GetPropData(lPropNum);
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
	const long lPropAlias = m_methodHelper->GetPropAlias(lPropNum);
	if (lPropAlias == eControl) {
		const long lPropData = m_methodHelper->GetPropData(lPropNum);
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

CONTROL_TYPE_REGISTER(ibValueGridBox, "Gridbox", "Container", string_to_clsid("CT_GRID"));