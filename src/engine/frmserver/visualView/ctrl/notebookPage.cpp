#include "notebook.h"
#include "core/serialize/dataBuilder.h"   // ibDataNode (control -> node)

//***********************************************************************************
//*                              ibValueNotebookPage                                 *
//***********************************************************************************

ibValueNotebookPage::ibValueNotebookPage() : ibValueControl()
{
}

//***********************************************************************************
//*                                   Life                                          *
//***********************************************************************************

void ibValueNotebookPage::OnUpdate(ibDataNode& state, ibVisualHost* host)
{
    // Auto carries both, as PictureAndText does: the title and the picture, whichever are set.
    const ibRepresentation representation = m_propertyRepresentation->GetValueAsEnum();

    state.SetValue(wxT("Visible"), IsPageShown());
    state.SetValue(wxT("Title"), representation != ibRepresentation::ibRepresentation_Picture ?
        m_propertyTitle->GetValueAsTranslateString() : wxString());
    state.SetValue(wxT("PictureVisible"), representation != ibRepresentation::ibRepresentation_Text);
}

void ibValueNotebookPage::OnSelected(ibVisualHost* host)
{
    // The page shows now — the notebook's ActivePage in the next frame. A page not on the notebook stays off.
    ibValueNotebook* const notebook = GetOwner();
    if (notebook != nullptr && IsPageShown())
        notebook->m_activePage = this;
}

bool ibValueNotebookPage::CanDeleteControl() const
{
    return m_parent->GetChildCount() > 1;
}

//***********************************************************************************
//*                              Read & save property                               *
//***********************************************************************************

bool ibValueNotebookPage::ReadData(const ibDataNode& node)
{
    m_propertyTitle->SetNodeValue(node.GetProperty(m_propertyTitle->GetName()));
    m_propertyRepresentation->SetNodeValue(node.GetProperty(m_propertyRepresentation->GetName()));
    m_propertyPicture->SetNodeValue(node.GetProperty(m_propertyPicture->GetName()));
    m_propertyVisible->SetNodeValue(node.GetProperty(m_propertyVisible->GetName()));
    m_propertyOrient->SetNodeValue(node.GetProperty(m_propertyOrient->GetName()));

    return ibValueControl::ReadData(node);
}

bool ibValueNotebookPage::WriteData(ibDataNode& node) const
{
    node.SetProperty(m_propertyTitle->GetName(), m_propertyTitle->GetNodeValue());
    node.SetProperty(m_propertyRepresentation->GetName(), m_propertyRepresentation->GetNodeValue());
    node.SetProperty(m_propertyPicture->GetName(), m_propertyPicture->GetNodeValue());
    node.SetProperty(m_propertyVisible->GetName(), m_propertyVisible->GetNodeValue());
    node.SetProperty(m_propertyOrient->GetName(), m_propertyOrient->GetNodeValue());

    return ibValueControl::WriteData(node);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

S_CONTROL_TYPE_REGISTER(ibValueNotebookPage, "NotebookPage", "NotebookPage", g_controlNotebookPageCLSID);