#include "notebook.h"
#include "frontend/visualView/pageWindow.h"

//***********************************************************************************
//*                           IMPLEMENT_DYNAMIC_CLASS                               *
//***********************************************************************************

wxIMPLEMENT_DYNAMIC_CLASS(ibValueNotebookPage, ibValueFrame);

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

void ibValueNotebookPage::OnCreated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost, bool firstСreated)
{
    ibPanelPage* page = dynamic_cast<ibPanelPage*>(wxobject);
    wxASSERT(page);

    wxAuiNotebook* notebook = dynamic_cast<wxAuiNotebook*>(wxparent);

    if (notebook != nullptr && m_propertyVisible->GetValueAsBoolean()) {
        notebook->AddPage(page, m_propertyTitle->GetValueAsTranslateString(), false, m_propertyPicture->GetValueAsBitmap());
        page->SetOrientation(m_propertyOrient->GetValueAsInteger());
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

bool ibValueNotebookPage::LoadData(ibReaderMemory& reader)
{
    m_propertyTitle->LoadData(reader);
    m_propertyRepresentation->LoadData(reader);
    m_propertyPicture->LoadData(reader);
    m_propertyVisible->LoadData(reader);
    m_propertyOrient->LoadData(reader);

    return ibValueControl::LoadData(reader);
}

bool ibValueNotebookPage::SaveData(ibWriterMemory writer)
{
    m_propertyTitle->SaveData(writer);
    m_propertyRepresentation->SaveData(writer);
    m_propertyPicture->SaveData(writer);
    m_propertyVisible->SaveData(writer);
    m_propertyOrient->SaveData(writer);

    return ibValueControl::SaveData(writer);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

S_CONTROL_TYPE_REGISTER(ibValueNotebookPage, "NotebookPage", "NotebookPage", g_controlNotebookPageCLSID);