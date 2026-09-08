#include "htmlBox.h"
#include "backend/serialize/dataBuilder.h"   // ibDataNode (control -> node)
#ifdef OES_USE_WEB
#include "frontend/web/webWindow.h"
#endif

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

wxObject* ibValueHTMLBox::Create(ibFrontendWindow* wxparent, ibVisualHost* visualHost)
{
#ifdef OES_USE_WEB
	(void)wxparent;
	(void)visualHost;
	// No dummy page on the web road: the browser draws an empty pane, which is
	// what an HTML box with nothing in it is.
	auto* htmlBox = new ibWebHtmlBox(GetControlID());
	htmlBox->SetPage(m_page);
	return htmlBox;
#else
	wxHtmlWindow* htmlBox = new wxHtmlWindow(wxparent, wxID_ANY,
		wxDefaultPosition,
		wxDefaultSize);

	wxString dummy_page(
		wxT("<b>wxHtmlWindow</b><br />")
		wxT("This is a dummy page.</body></html>"));

	htmlBox->SetPage(dummy_page);

	return htmlBox;
#endif
}

void ibValueHTMLBox::OnCreated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost, bool firstCreated)
{
}

void ibValueHTMLBox::OnSelected(wxObject* wxobject)
{
}

void ibValueHTMLBox::Update(wxObject* wxobject, ibVisualHost* visualHost)
{
#ifdef OES_USE_WEB
	(void)visualHost;
	auto* htmlBox = static_cast<ibWebHtmlBox*>(wxobject);

	if (htmlBox)
	{
		htmlBox->SetPage(m_page);
	}
#else
	wxHtmlWindow* htmlBox = dynamic_cast<wxHtmlWindow*>(wxobject);

	if (htmlBox)
	{
	}
#endif

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
#ifdef OES_USE_WEB
	auto* htmlBox = static_cast<ibWebHtmlBox*>(GetWxObject());
	switch (m_members.GetMethodData(lMethodNum))
	{
	case enSetPage:
		m_page = paParams[0]->GetString();
		if (htmlBox != nullptr)
			htmlBox->SetPage(m_page);
		pvarRetValue = true;
		return true;
	}
#else
	wxHtmlWindow* htmlBox = dynamic_cast<wxHtmlWindow*>(GetWxObject());
	switch (m_members.GetMethodData(lMethodNum))
	{
	case enSetPage:
		pvarRetValue = htmlBox ?
			htmlBox->SetPage(paParams[0]->GetString()) : !paParams[0]->IsEmpty();
		return true;
	}
#endif

	return ibValueFrame::CallAsFunc(lMethodNum, pvarRetValue, paParams, lSizeArray);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_TYPE_REGISTER(ibValueHTMLBox, "Htmlbox", "Container", control_to_clsid("CT_HTML"));
