#include "notebook.h"
#include "backend/serialize/dataBuilder.h"   // ibDataNode (control -> node)
#ifdef OES_USE_WEB
#include "frontend/web/webWindow.h"
#include "frontend/web/webSizer.h"
#include "backend/backend_picture.h"   // bitmap -> data URI
#else
#include "frontend/visualView/pageWindow.h"
#endif

//***********************************************************************************
//*                           IMPLEMENT_DYNAMIC_CLASS                               *
//***********************************************************************************


//***********************************************************************************
//*                              ibValueNotebookPage                                 *
//***********************************************************************************

ibValueNotebookPage::ibValueNotebookPage() : ibValueControl()
{
}

wxObject* ibValueNotebookPage::Create(ibFrontendWindow* wxparent, ibVisualHost* visualHost)
{
#ifdef OES_USE_WEB
    (void)wxparent;
    (void)visualHost;
    auto* page = new ibWebNotebookPage(GetControlID());
    // The page carries a box sizer of its own, as ibPanelPage does, and the
    // walker hands its contents to that rather than to the page: it is what
    // the layout params on them are addressed to.
    page->SetSizer(new ibWebBoxSizer(m_propertyOrient->GetValueAsInteger()));
    return page;
#else
    return new ibPanelPage(wxparent, wxID_ANY);
#endif
}

void ibValueNotebookPage::OnCreated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost, bool firstCreated)
{
#ifdef OES_USE_WEB
    // The notebook adopts its pages through the walker's SetParent, and which
    // one is in front is the notebook's own state -- there is nothing to add
    // to a strip here.
    (void)wxobject;
    (void)wxparent;
    (void)visualHost;
    (void)firstCreated;
#else
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
#endif
}

void ibValueNotebookPage::OnUpdated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost)
{
#ifdef OES_USE_WEB
    (void)wxparent;
    (void)visualHost;
    auto* page = static_cast<ibWebNotebookPage*>(wxobject);
    if (page == nullptr)
        return;

    // Representation reads the same four ways a tool's does, Auto meaning
    // "whatever there is": the caption, the picture, or both.
    const wxBitmap bmp = m_propertyPicture->GetValueAsBitmap();
    const bool hasPicture = bmp.IsOk();
    ibRepresentation rep = m_propertyRepresentation->GetValueAsEnum();
    if (rep == ibRepresentation::ibRepresentation_Auto) {
        rep = hasPicture ? ibRepresentation::ibRepresentation_PictureAndText
                         : ibRepresentation::ibRepresentation_Text;
    }

    wxString pictureUri;
    if (hasPicture) {
        const wxString b64 = ibBackendPicture::CreateBase64Image(bmp.ConvertToImage());
        if (!b64.IsEmpty())
            pictureUri = wxT("data:image/png;base64,") + b64;
    }

    page->SetLabel(m_propertyTitle->GetValueAsTranslateString());
    page->SetRepresentation(static_cast<int>(rep));
    page->SetHasPicture(hasPicture);
    page->SetPictureDataUri(pictureUri);
    // An invisible page has no tab and no contents -- the same thing Visible
    // means on the desktop, where such a page is simply never added.
    page->Show(m_propertyVisible->GetValueAsBoolean());

    if (auto* box = dynamic_cast<ibWebBoxSizer*>(page->GetSizer()))
        box->SetOrientation(m_propertyOrient->GetValueAsInteger());
#else
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
#endif
}

void ibValueNotebookPage::OnSelected(wxObject* wxobject)
{
#ifdef OES_USE_WEB
    (void)wxobject;
#else
    wxAuiNotebook* notebook = dynamic_cast<wxAuiNotebook*>(GetParent()->GetWxObject());
    wxASSERT(notebook);
    if (notebook != nullptr) {
        int pos = notebook->GetPageIndex((wxWindow*)wxobject);
        if (pos != notebook->GetSelection())
            notebook->SetSelection(pos);
    }
#endif
}

void ibValueNotebookPage::Cleanup(wxObject* wxobject, ibVisualHost* visualHost)
{
#ifdef OES_USE_WEB
    (void)wxobject;
    (void)visualHost;
#else
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
#endif
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