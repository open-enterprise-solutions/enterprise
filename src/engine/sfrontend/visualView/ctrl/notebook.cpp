#include "notebook.h"
#include "backend/serialize/dataBuilder.h"   // ibDataNode (control -> node)
#include "form.h"

//***********************************************************************************
//*                                 Value Notebook                                  *
//***********************************************************************************

ibValueNotebook::ibValueNotebook() : ibValueWindow(), m_activePage(nullptr)
{
	m_members.Bind(this, &ibValueNotebook::FillControlMembers);
	//set default params
	//m_minimum_size = wxSize(300, 100);
}

//***********************************************************************************
//*                                  Update                                         *
//***********************************************************************************

ibValueNotebookPage* ibValueNotebook::GetActivePage() const
{
	// A page taken off the notebook (hidden, or switched off by the functional options) leaves the
	// selection on the first page that is still on it.
	if (m_activePage != nullptr && m_activePage->IsPageShown())
		return m_activePage;
	for (unsigned int i = 0; i < GetChildCount(); i++) {
		ibValueNotebookPage* child = dynamic_cast<ibValueNotebookPage*>(GetChild(i));
		if (child != nullptr && child->IsPageShown())
			return child;
	}
	return nullptr;
}

void ibValueNotebook::OnUpdate(ibDataNode& state, ibVisualHost* host)
{
	ibValueWindow::OnUpdate(state, host);

	ibValueNotebookPage* selectedPage = GetActivePage();
	state.SetValue(wxT("ActivePage"), selectedPage != nullptr ? selectedPage->GetControlID() : 0);
}

//**********************************************************************************
//*                                   Data		                                   *
//**********************************************************************************

bool ibValueNotebook::ReadData(const ibDataNode& node)
{
	m_propertyOrient->SetNodeValue(node.GetProperty(m_propertyOrient->GetName()));

	//events
	m_eventOnPageChanged->SetNodeValue(node.GetProperty(m_eventOnPageChanged->GetName()));
	return ibValueWindow::ReadData(node);
}

bool ibValueNotebook::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyOrient->GetName(), m_propertyOrient->GetNodeValue());

	//events
	node.SetProperty(m_eventOnPageChanged->GetName(), m_eventOnPageChanged->GetNodeValue());
	return ibValueWindow::WriteData(node);
}

//**********************************************************************************

enum Func {
	enPages = 0,
	enActivePage
};

void ibValueNotebook::FillControlMembers(ibMemberTable& helper) const
{
	helper.AppendFunc(wxT("Pages"), wxT("Pages()"));
	helper.AppendFunc(wxT("ActivePage"), wxT("ActivePage()"));
}

#include "backend/system/value/valueMap.h"

bool ibValueNotebook::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)       //method call
{
	switch (lMethodNum)
	{
	case enPages:
	{
		ibValueStructure* structurePage = new ibValueStructure(true);
		for (unsigned int i = 0; i < GetChildCount(); i++) {
			ibValueNotebookPage* notebookPage = dynamic_cast<ibValueNotebookPage*>(GetChild(i));
			if (notebookPage) {
				structurePage->Insert(notebookPage->GetControlName(), ibValue(notebookPage));
			}
		}
#pragma message("nouverbe to nouverbe: needs more work!")
		pvarRetValue = structurePage;
		return true; 
	}
	case enActivePage:
		pvarRetValue = GetActivePage();
		return true;
	}

	return ibValueWindow::CallAsFunc(lMethodNum, pvarRetValue, paParams, lSizeArray);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_TYPE_REGISTER(ibValueNotebook, "Notebook", "Notebook", g_controlNotebookCLSID);