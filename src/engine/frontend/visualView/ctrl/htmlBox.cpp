#include "htmlbox.h"

//***********************************************************************************
//*                           IMPLEMENT_DYNAMIC_CLASS                               *
//***********************************************************************************

wxIMPLEMENT_DYNAMIC_CLASS(ibValueHTMLBox, ibValueWindow);

//***********************************************************************************
//*                                 Value Notebook                                  *
//***********************************************************************************

ibValueHTMLBox::ibValueHTMLBox() : ibValueWindow()
{
	m_propertyMinSize->SetValue(wxSize(250, 150));
}

wxObject* ibValueHTMLBox::Create(wxWindow* wxparent, ibVisualHost* visualHost)
{
	wxHtmlWindow* htmlBox = new wxHtmlWindow(wxparent, wxID_ANY,
		wxDefaultPosition,
		wxDefaultSize);

	wxString dummy_page(
		wxT("<b>wxHtmlWindow</b><br />")
		wxT("This is a dummy page.</body></html>"));

	htmlBox->SetPage(dummy_page);

	return htmlBox;
}

void ibValueHTMLBox::OnCreated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost, bool firstСreated)
{
	wxHtmlWindow* htmlBox = dynamic_cast<wxHtmlWindow*>(wxobject);
}

void ibValueHTMLBox::OnSelected(wxObject* wxobject)
{
}

void ibValueHTMLBox::Update(wxObject* wxobject, ibVisualHost* visualHost)
{
	wxHtmlWindow* htmlBox = dynamic_cast<wxHtmlWindow*>(wxobject);

	if (htmlBox)
	{
	}

	UpdateWindow(htmlBox);
}

void ibValueHTMLBox::Cleanup(wxObject* obj, ibVisualHost* visualHost)
{
}

//**********************************************************************************
//*                                   Data										   *
//**********************************************************************************

bool ibValueHTMLBox::LoadData(ibReaderMemory& reader)
{
	return ibValueWindow::LoadData(reader);
}

bool ibValueHTMLBox::SaveData(ibWriterMemory writer)
{
	return ibValueWindow::SaveData(writer);
}

//**********************************************************************************

enum Func {
	enSetPage = 0,
};

void ibValueHTMLBox::PrepareNames() const // this method is automatically called to initialize attribute and method names.
{
	ibValueFrame::PrepareNames();

	m_methodHelper->AppendFunc(wxT("SetPage"), 1, wxT("SetPage(p: page)"), enSetPage, wxNOT_FOUND);
}

bool ibValueHTMLBox::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)       //method call
{
	wxHtmlWindow* htmlBox = dynamic_cast<wxHtmlWindow*>(GetWxObject());
	switch (m_methodHelper->GetMethodData(lMethodNum))
	{
	case enSetPage:
		pvarRetValue = htmlBox ?
			htmlBox->SetPage(paParams[0]->GetString()) : !paParams[0]->IsEmpty();
		return true;
	}

	return ibValueFrame::CallAsFunc(lMethodNum, pvarRetValue, paParams, lSizeArray);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_TYPE_REGISTER(ibValueHTMLBox, "Htmlbox", "Container", string_to_clsid("CT_HTML"));