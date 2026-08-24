#include "gridBox.h"
#include "backend/serialize/dataBuilder.h"   // ibDataNode (control -> node)
#include "backend/system/value/valueDataComposition.h"   // the source that turns this box into a report

//***********************************************************************************
//*                           IMPLEMENT_DYNAMIC_CLASS                               *
//***********************************************************************************

// ⭐⭐ THE MODEL INSTALLS ITSELF HERE. Anything that IS a spreadsheet model — a hand-filled document,
// a composer — is handed in and the box shows the sheet THAT model holds. Nothing else is accepted,
// and the box keeps what it had rather than half-taking a value it cannot show.
//
// 🛑 THE MODEL, never a bare document description: the drill-down parameters and the edit mode live
// on the OBJECT, so installing a description alone leaves every cell bound to a name nothing answers
// to — the sheet looks right and stops opening anything.
bool ibValueGridBox::SetControlValue(const ibValue& varControlVal)
{
	ibValueSpreadsheetModel* model = varControlVal.ConvertToType<ibValueSpreadsheetModel>();
	if (model == nullptr)
		return false;

	m_spreadsheetModel = model;

	// The window follows the model it was given — the sheet is the model's, and a run filling it
	// reaches the screen through the notifiers the window subscribes to here.
	if (ibGridEditor* gridWindow = dynamic_cast<ibGridEditor*>(GetInnerWx()))
		gridWindow->LoadDocument(model->GetSpreadsheetDocument());

	return true;
}

bool ibValueGridBox::GetControlValue(ibValue& pvarControlVal) const
{
	pvarControlVal = m_spreadsheetModel;
	return true;
}

//***********************************************************************************
//*                                 Value Notebook                                  *
//***********************************************************************************

ibValueGridBox::ibValueGridBox() : ibValueWindowComposite(),
m_spreadsheetModel(new ibValueSpreadsheetDocument())   // nothing bound yet — a sheet of its own IS a model
{
	m_members.Bind(this, &ibValueGridBox::FillControlMembers);
	//set default params
	m_propertyMinSize->SetValue(wxSize(150, 50));
	// ⭐ THE SOURCE TAKES EXACTLY TWO TYPES (Max, 2026-08-19): a **spreadsheet document** — the
	// hand-filled sheet, a printable form — and a **composition** — the report, which builds its own
	// document and brings the two verbs with it. Declared here so the picker offers those and
	// nothing else; the control then behaves by what it was actually given (see ResolveComposition).
	ibTypeDescription allowedSources;
	allowedSources.AppendMetaType(g_valueSpreadsheetCLSID);
	allowedSources.AppendMetaType(g_valueDataCompositionCLSID);
	m_propertySource->SetValue(allowedSources);
}

#include "frontend/visualView/ctrl/form.h"

wxObject* ibValueGridBox::Create(wxWindow* wxparent, ibVisualHost* visualHost)
{
	ibGridEditor* gridWindow = new ibGridEditor(nullptr, wxparent, wxID_ANY, wxDefaultPosition, wxDefaultSize);

	gridWindow->EnableProperty(!visualHost->IsDesignerHost());
	gridWindow->EnableGridArea(false);
	gridWindow->EnableGridLines(false);

	// THE BINDING IS HONOURED HERE TOO — the same moment every other source control takes its value
	// (a checkbox, a textbox, a table all read the bound attribute as their window appears). The form
	// may already have handed it over at InitializeControl; taking it again is idempotent, and it is
	// what makes a window rebuilt on its own — a re-render, a designer preview — come back bound.
	RefreshModel();

	// WHAT IT SHOWS: the sheet the MODEL holds. The model is what fills it — a composer on Compose,
	// a hand-filled document simply by being one — and the window subscribes to that very object, so
	// data arriving there reaches the screen without anybody re-pointing the control.
	if (m_spreadsheetModel)
		gridWindow->LoadDocument(m_spreadsheetModel->GetSpreadsheetDocument());

	return gridWindow;
}

void ibValueGridBox::OnCreated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost, bool firstCreated)
{
	// A JUST-DROPPED BOX PROVISIONS ITS OWN ATTRIBUTE — the same door every source control uses. Its
	// TYPE is the first type this box DECLARES it accepts (spreadsheet document), so there is nothing
	// to state twice.
	if (firstCreated)
		AutoBindNewSource(this);
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
	// ⭐ THE WINDOW IS GOING — TELL THE READ TO STOP. A report composes on a rented background run,
	// and a form closing is not the same moment as the composition dying: the run holds references
	// of its own, so without this it keeps reading against a session nobody is watching, on a
	// connection somebody else is waiting for (Max, 2026-08-19: "sorry, break off, we changed our
	// mind"). CancelFetch raises the same cooperative flag the interpreter obeys and waits the run
	// out — the walk polls it per row, so it stops at the next one.
	//
	// ASKED OF THE MODEL, not of a composition: whatever is bound answers, and a sheet that reads
	// nothing has nothing to stop.
	if (m_spreadsheetModel)
		m_spreadsheetModel->CancelFetch();
}

//**********************************************************************************

#include "frontend/win/editor/gridEditor/gridPrintout.h"

wxPrintout* ibValueGridBox::CreatePrintout() const
{
	// GetInnerWx, not GetWxObject — this box is a COMPOSITE, so the outer object is the layer
	// canvas that carries the command bar, and the grid sits inside it. The cast to the editor was
	// therefore never anything but null, and printing a report quietly did nothing.
	ibGridEditor* gridWindow = dynamic_cast<ibGridEditor*>(GetInnerWx());
	if (gridWindow != nullptr)
		return gridWindow->CreatePrintout();

	return nullptr;
}

//**********************************************************************************
//*                                   Data										   *
//**********************************************************************************

bool ibValueGridBox::ReadData(const ibDataNode& node)
{
	// ⭐ THE SOURCE IS PART OF THE CONTROL (Max, 2026-08-19: "you forgot the serialisation that
	// stores the source"). Everything else about a gridbox survived a save and the binding did not,
	// so a form reopened with the box pointing at nothing — and the report it was built for looked
	// like it had lost its composition.
	m_propertySource->SetNodeValue(node.GetProperty(m_propertySource->GetName()));

	// The sheet a designer typed into travels with the control — read into the sheet the MODEL holds,
	// which with no source bound is the box's own document.
	if (m_spreadsheetModel) {
		ibSpreadsheetDescriptionMemory::ReadNode(node.GetProperty(wxT("Spreadsheet")),
			m_spreadsheetModel->GetSpreadsheetDocument()->GetSpreadsheetDesc());
	}
	return ibValueWindowComposite::ReadData(node);
}
bool ibValueGridBox::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertySource->GetName(), m_propertySource->GetNodeValue());

	if (m_spreadsheetModel) {
		ibDataValue spreadsheet;
		ibSpreadsheetDescriptionMemory::WriteNode(spreadsheet,
			m_spreadsheetModel->GetSpreadsheetDocument()->GetSpreadsheetDesc());
		node.SetProperty(wxT("Spreadsheet"), spreadsheet);
	}
	return ibValueWindowComposite::WriteData(node);
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
	const long lPropAlias = m_members.GetPropAlias(lPropNum);
	if (lPropAlias == eControl) {
		const long lPropData = m_members.GetPropData(lPropNum);
		if (lPropData == eGridValue)
			return SetControlValue(varPropVal);   // ONE door: a script assigns what the form binds
	}

	return ibValueFrame::SetPropVal(lPropNum, varPropVal);
}

bool ibValueGridBox::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	const long lPropAlias = m_members.GetPropAlias(lPropNum);
	if (lPropAlias == eControl) {
		const long lPropData = m_members.GetPropData(lPropNum);
		if (lPropData == eGridValue)
			return GetControlValue(pvarPropVal);
	}
	return ibValueFrame::GetPropVal(lPropNum, pvarPropVal);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_TYPE_REGISTER(ibValueGridBox, "Gridbox", "Container", g_controlGridBoxCLSID);
