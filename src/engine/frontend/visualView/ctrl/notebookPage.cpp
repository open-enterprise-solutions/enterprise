#include "notebook.h"
#include "backend/serialize/dataBuilder.h"   // ibDataNode (control -> node)
#include "frontend/visualView/pageWindow.h"

//***********************************************************************************
//*                           IMPLEMENT_DYNAMIC_CLASS                               *
//***********************************************************************************


//***********************************************************************************
//*                              ibValueNotebookPage                                 *
//***********************************************************************************

ibValueNotebookPage::ibValueNotebookPage() : ibValueControl()
{
}

wxObject* ibValueNotebookPage::Create(wxWindow* wxparent, ibVisualHost* visualHost)
{
    return new ibPanelPage(wxparent, wxID_ANY);
}

void ibValueNotebookPage::OnCreated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost, bool firstCreated)
{
    ibPanelPage* page = dynamic_cast<ibPanelPage*>(wxobject);
    wxASSERT(page);

    wxAuiNotebook* notebook = dynamic_cast<wxAuiNotebook*>(wxparent);

    if (notebook != nullptr && m_propertyVisible->GetValueAsBoolean()) {
        notebook->AddPage(page, m_propertyTitle->GetValueAsTranslateString(), false, m_propertyPicture->GetValueAsBitmap());
        page->SetOrientation(m_propertyOrient->GetValueAsInteger());
    }
    else {
        // ⚠ A PAGE THAT IS NOT IN THE NOTEBOOK MUST BE HIDDEN. It was built as a CHILD of the
        // notebook, so leaving it shown does not mean "not on a tab" — it means "drawn at (0, 0)",
        // which is where the notebook's TAB STRIP is. The page then paints its own contents over
        // the captions and the strip reads as two words on top of each other.
        // (Same defect, same day, in the query constructor's SyncNotebookPages.)
        page->Hide();
    }

    if (visualHost->IsDesignerHost()) {
        page->PushEventHandler(g_visualHostContext->GetHighlightPaintHandler(page));
    }
}

void ibValueNotebookPage::OnUpdated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost)
{
    ibValueFrame* parentControl = GetParent(); int pos = wxNOT_FOUND;
    if (m_propertyVisible->GetValueAsBoolean()) {
        for (unsigned int i = 0; i < parentControl->GetChildCount(); i++) {
            ibValueNotebookPage* child = dynamic_cast<ibValueNotebookPage*>(parentControl->GetChild(i));
            wxASSERT(child);
            if (m_controlId == child->m_controlId) {
                pos = i; break;
            }
        }
    }
    wxAuiNotebook* notebook = dynamic_cast<wxAuiNotebook*>(wxparent);
    wxASSERT(notebook);
    int pos_old = notebook->FindPage((wxWindow*)wxobject);
    if (pos_old != wxNOT_FOUND && pos != pos_old)
        notebook->RemovePage(pos_old);

    // RemovePage DETACHES without hiding — see OnCreated. A page turned invisible therefore has to
    // be hidden here too, or it goes on painting over the tab strip it was just taken off.
    ((wxWindow*)wxobject)->Show(m_propertyVisible->GetValueAsBoolean());

    if (m_propertyVisible->GetValueAsBoolean()) {

        if (pos != pos_old)
            notebook->InsertPage(pos, (wxWindow*)wxobject, m_propertyTitle->GetValueAsTranslateString(), pos_old == wxNOT_FOUND, m_propertyPicture->GetValueAsBitmap());
        
        if (m_propertyRepresentation->GetValueAsEnum() == ibRepresentation::ibRepresentation_Auto) {
            notebook->SetPageText(pos, m_propertyTitle->GetValueAsTranslateString());
            notebook->SetPageBitmap(pos, m_propertyPicture->GetValueAsBitmap());
        }
        else if (m_propertyRepresentation->GetValueAsEnum() == ibRepresentation::ibRepresentation_PictureAndText) {
            notebook->SetPageText(pos, m_propertyTitle->GetValueAsTranslateString());
            notebook->SetPageBitmap(pos, m_propertyPicture->GetValueAsBitmap());
        }
        else if (m_propertyRepresentation->GetValueAsEnum() == ibRepresentation::ibRepresentation_Picture) {
            notebook->SetPageText(pos, wxEmptyString);
            notebook->SetPageBitmap(pos, m_propertyPicture->GetValueAsBitmap());
        }
        else if (m_propertyRepresentation->GetValueAsEnum() == ibRepresentation::ibRepresentation_Text) {
            notebook->SetPageText(pos, m_propertyTitle->GetValueAsTranslateString());
            notebook->SetPageBitmap(pos, wxNullBitmap);
        }
       
        ibPanelPage* page = dynamic_cast<ibPanelPage*>(wxobject);
        wxASSERT(page);
        page->SetOrientation(m_propertyOrient->GetValueAsInteger());
    }
}

void ibValueNotebookPage::OnSelected(wxObject* wxobject)
{
    wxAuiNotebook* notebook = dynamic_cast<wxAuiNotebook*>(GetParent()->GetWxObject());
    wxASSERT(notebook);
    if (notebook != nullptr) {
        int pos = notebook->GetPageIndex((wxWindow*)wxobject);
        if (pos != notebook->GetSelection())
            notebook->SetSelection(pos);
    }
}

void ibValueNotebookPage::Cleanup(wxObject* wxobject, ibVisualHost* visualHost)
{
    wxAuiNotebook* notebook = dynamic_cast<wxAuiNotebook*>(visualHost->GetWxObject(GetParent()));
    wxASSERT(notebook);
    if (notebook != nullptr) {
        int pos = notebook->GetPageIndex((wxWindow*)wxobject);
        notebook->RemovePage(pos);
    }

    if (visualHost->IsDesignerHost()) {
        ibPanelPage* page = dynamic_cast<ibPanelPage*>(wxobject);
        wxASSERT(page);
        page->PopEventHandler(true);
    }
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