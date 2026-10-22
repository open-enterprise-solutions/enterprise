#include "htmlBox.h"
#include "core/serialize/dataBuilder.h"   // ibDataNode (control -> node)

//***********************************************************************************
//*                           IMPLEMENT_DYNAMIC_CLASS                               *
//***********************************************************************************


//***********************************************************************************
//*                                 Value Notebook                                  *
//***********************************************************************************

ibValueHTMLBox::ibValueHTMLBox() : ibValueWindow()
{
	m_members.Bind(this, &ibValueHTMLBox::FillControlMembers);
	m_propertyMinSize->SetValue(ibSize(250, 150));
}

wxObject* ibValueHTMLBox::Create(wxWindow* wxparent, ibVisualHost* visualHost)
{
	wxHtmlWindow* htmlBox = new wxHtmlWindow(wxparent, wxID_ANY,
		wxDefaultPosition,
		wxDefaultSize);

	// The page the code set (in OnOpen, before this window existed), else a placeholder.
	if (!m_page.IsEmpty()) {
		htmlBox->SetPage(m_page);
	}
	else {
		wxString dummy_page(
			wxT("<b>wxHtmlWindow</b><br />")
			wxT("This is a dummy page.</body></html>"));

		htmlBox->SetPage(dummy_page);
	}

	return htmlBox;
}

void ibValueHTMLBox::OnCreated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost, bool firstCreated)
{
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
		// Kept as well as shown: a window created later (or created again) shows it too.
		m_page = paParams[0]->GetString();
		pvarRetValue = htmlBox ?
			htmlBox->SetPage(m_page) : !paParams[0]->IsEmpty();
		return true;
	}

	return ibValueFrame::CallAsFunc(lMethodNum, pvarRetValue, paParams, lSizeArray);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_TYPE_REGISTER(ibValueHTMLBox, "Htmlbox", "Container", control_to_clsid("CT_HTML"));
