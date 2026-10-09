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
	m_propertyMinSize->SetValue(wxSize(250, 150));
}

void ibValueHTMLBox::OnUpdate(ibDataNode& state, ibVisualHost* host)
{
	ibValueWindow::OnUpdate(state, host);
	state.SetValue(wxT("Page"), m_page);
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
	switch (m_members.GetMethodData(lMethodNum))
	{
	case enSetPage:
		m_page = paParams[0]->GetString();
		pvarRetValue = !paParams[0]->IsEmpty();
		return true;
	}

	return ibValueFrame::CallAsFunc(lMethodNum, pvarRetValue, paParams, lSizeArray);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_TYPE_REGISTER(ibValueHTMLBox, "Htmlbox", "Container", control_to_clsid("CT_HTML"));
