#include "htmlBox.h"
#include "backend/serialize/dataBuilder.h"   // ibDataNode (control -> node)

//***********************************************************************************
//*                           IMPLEMENT_DYNAMIC_CLASS                               *
//***********************************************************************************


//***********************************************************************************
//*                                 Value Notebook                                  *
//***********************************************************************************

ibValueHTMLBox::ibValueHTMLBox() : ibValueWindow()
{
	m_members.Bind(this, &ibValueHTMLBox::FillControlMembers);
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

void ibValueHTMLBox::OnCreated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost, bool firstCreated)
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

bool ibValueHTMLBox::ReadData(const ibDataNode& node)
{
	return ibValueWindow::ReadData(node);
}

bool ibValueHTMLBox::WriteData(ibDataNode& node) const
{
	return ibValueWindow::WriteData(node);
}

//**********************************************************************************

enum Func {
	enSetPage = 0,
};

void ibValueHTMLBox::FillControlMembers(ibMemberTable& helper) const
{
	helper.AppendFunc(wxT("SetPage"), 1, wxT("SetPage(p: page)"), enSetPage, wxNOT_FOUND);
}

bool ibValueHTMLBox::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)       //method call
{
	wxHtmlWindow* htmlBox = dynamic_cast<wxHtmlWindow*>(GetWxObject());
	switch (m_members.GetMethodData(lMethodNum))
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

CONTROL_TYPE_REGISTER(ibValueHTMLBox, "Htmlbox", "Container", control_to_clsid("CT_HTML"));
