#include "textBox.h"
#include "backend/serialize/dataBuilder.h"   // ibDataNode (control -> node)
#include "frontend/docView/templates/docViewText.h"

//***********************************************************************************
//*                           IMPLEMENT_DYNAMIC_CLASS                               *
//***********************************************************************************


//***********************************************************************************
//*                                 Value Notebook                                  *
//***********************************************************************************

ibValueTextBox::ibValueTextBox() : ibValueWindow(),
m_textDocument(new ibTextBoxDocument()),
m_textView(new ibTextBoxView())            // empty until Create
{
	m_textView->SetDocument(m_textDocument);

	//set default params
	m_propertyMinSize->SetValue(wxSize(150, 50));
}

#include "frontend/visualView/ctrl/form.h"

ibValueTextBox::~ibValueTextBox()
{
	wxDELETE(m_textView);   // the view first: it is the document's
	wxDELETE(m_textDocument);
}

ibView* ibValueTextBox::GetControlView() const
{
	// An empty view (cleaned up, not created again) has nothing to hand on.
	return m_textView->GetText() != nullptr ? m_textView : nullptr;
}

wxObject* ibValueTextBox::Create(wxWindow* wxparent, ibVisualHost* visualHost)
{
	// The box's view is created the way a document's view is — its frame is this box's parent, and its
	// OnCreate makes the editor, which is what the form engine is handed.
	m_textView->SetFrame(wxparent);
	m_textView->OnCreate(m_textDocument, 0);

	return m_textView->GetText();
}

void ibValueTextBox::OnCreated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost, bool firstCreated)
{
}

void ibValueTextBox::OnSelected(wxObject* wxobject)
{
}

void ibValueTextBox::Update(wxObject* wxobject, ibVisualHost* visualHost)
{
	ibTextEditor* textWindow = dynamic_cast<ibTextEditor*>(wxobject);

	if (textWindow) {
	}

	UpdateWindow(textWindow);

	m_textDocument->UpdateAllViews();
}

void ibValueTextBox::Cleanup(wxObject* wxobject, ibVisualHost* visualHost)
{
	// The view is closed, left empty: the visual host destroys the editor right after this, and the
	// document stays with the box.
	m_textView->Close(false);
}

//**********************************************************************************
//*                                   Data										   *
//**********************************************************************************

bool ibValueTextBox::ReadData(const ibDataNode& node)
{
	return ibValueWindow::ReadData(node);
}

bool ibValueTextBox::WriteData(ibDataNode& node) const
{
	return ibValueWindow::WriteData(node);
}

//***********************************************************************************

bool ibValueTextBox::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	return ibValueFrame::SetPropVal(lPropNum, varPropVal);
}

bool ibValueTextBox::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	return ibValueFrame::GetPropVal(lPropNum, pvarPropVal);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_TYPE_REGISTER(ibValueTextBox, "Textbox", "Container", control_to_clsid("CT_TEXT"));
