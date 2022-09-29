#include "notebook.h"
#include "frontend/visualView/visualEditor.h"  

//***********************************************************************************
//*                           IMPLEMENT_DYNAMIC_CLASS                               *
//***********************************************************************************

wxIMPLEMENT_DYNAMIC_CLASS(CValueNotebookPage, IValueFrame);

//***********************************************************************************
//*                              CValueNotebookPage                                 *
//***********************************************************************************

CValueNotebookPage::CValueNotebookPage() : IValueControl()
{
}

wxObject* CValueNotebookPage::Create(wxObject* parent, IVisualHost *visualHost)
{
	return new CPageWindow((wxWindow*)parent, wxID_ANY);
}

void CValueNotebookPage::OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost *visualHost, bool first—reated)
{
	CPageWindow *newPage = dynamic_cast<CPageWindow *>(wxobject);
	wxASSERT(newPage);
	wxAuiNotebook *auiNotebookWnd = dynamic_cast<wxAuiNotebook *>(wxparent);

	if (auiNotebookWnd && m_propertyVisible->GetValueAsBoolean()) {
		auiNotebookWnd->AddPage(newPage, m_propertyCaption->GetValueAsString(), false, m_propertyIcon->GetValueAsBitmap());
		newPage->SetOrientation(m_propertyOrient->GetValueAsInteger());
	}

	if (visualHost->IsDesignerHost()) {
		newPage->PushEventHandler(new CDesignerWindow::CHighlightPaintHandler(newPage));
	}
}

void CValueNotebookPage::OnUpdated(wxObject* wxobject, wxWindow* wxparent, IVisualHost *visualHost)
{
	IValueFrame *parentControl = GetParent(); int idx = wxNOT_FOUND;

	for (unsigned int i = 0; i < parentControl->GetChildCount(); i++) {
		CValueNotebookPage *child = dynamic_cast<CValueNotebookPage *>(parentControl->GetChild(i));
		wxASSERT(child);
		if (m_controlId == child->m_controlId) { 
			idx = i;
			break; 
		}
	}

	CPageWindow *pageNotebook = dynamic_cast<CPageWindow *>(wxobject);
	wxASSERT(pageNotebook);
	wxAuiNotebook *auiNotebookWnd = dynamic_cast<wxAuiNotebook *>(wxparent);
	wxASSERT(auiNotebookWnd);

	int idxPage = wxNOT_FOUND;
	for (unsigned int pageIndex = 0; pageIndex < auiNotebookWnd->GetPageCount(); pageIndex++) { 
		if (auiNotebookWnd->GetPage(pageIndex) == pageNotebook) { 
			idxPage = pageIndex; 
			break; 
		} 
	}
	
	if (idxPage != wxNOT_FOUND) 
		auiNotebookWnd->RemovePage(idxPage);

	if (m_propertyVisible->GetValueAsBoolean()) {
		auiNotebookWnd->InsertPage(idx, pageNotebook, m_propertyCaption->GetValueAsString(), false, m_propertyIcon->GetValueAsBitmap());
		auiNotebookWnd->SetPageText(idx, m_propertyCaption->GetValueAsString());
		auiNotebookWnd->SetPageBitmap(idx, m_propertyIcon->GetValueAsBitmap());
		pageNotebook->SetOrientation(m_propertyOrient->GetValueAsInteger());
	}
}

void CValueNotebookPage::OnSelected(wxObject* wxobject)
{
	IValueFrame *parentCtrl = GetParent();
	wxASSERT(parentCtrl);
	wxAuiNotebook *auiNotebookWnd = dynamic_cast<wxAuiNotebook *>(parentCtrl->GetWxObject());
	wxASSERT(auiNotebookWnd);
	int idxPage = wxNOT_FOUND;
	for (unsigned int pageIndex = 0; pageIndex < auiNotebookWnd->GetPageCount(); pageIndex++) {
		if (auiNotebookWnd->GetPage(pageIndex) == wxobject) {
			idxPage = pageIndex;
			break;
		}
	}
	if (idxPage != auiNotebookWnd->GetSelection()) 
		auiNotebookWnd->SetSelection(idxPage);
}

void CValueNotebookPage::Cleanup(wxObject* wxobject, IVisualHost *visualHost)
{
	CPageWindow *newPage = dynamic_cast<CPageWindow *>(wxobject);
	wxASSERT(newPage);
	wxAuiNotebook *auiNotebookWnd = dynamic_cast<wxAuiNotebook *>(visualHost->GetWxObject(GetParent()));
	if (auiNotebookWnd) {
		CValueNotebook *notebook = dynamic_cast<CValueNotebook *>(GetParent());
		notebook->m_bInitialized = false; 
		int page = wxNOT_FOUND;
		for (unsigned int idx = 0; idx < auiNotebookWnd->GetPageCount(); idx++) { 
			if (auiNotebookWnd->GetPage(idx) == wxobject) { 
				page = idx; 
				break; 
			} 
		}
		if (page != wxNOT_FOUND) { 
			auiNotebookWnd->RemovePage(page); 
			newPage->Hide();
		}
	}

	if (visualHost->IsDesignerHost()) {
		newPage->PopEventHandler(true);
	}
}

bool CValueNotebookPage::CanDeleteControl() const
{
	return m_parent->GetChildCount() > 1;
}

//***********************************************************************************
//*                              Read & save property                               *
//***********************************************************************************

bool CValueNotebookPage::LoadData(CMemoryReader &reader)
{
	wxString caption; reader.r_stringZ(caption);
	m_propertyCaption->SetValue(caption);
	m_propertyVisible->SetValue(reader.r_u8());
	m_propertyOrient->SetValue(reader.r_s32());
	return IValueControl::LoadData(reader);
}

bool CValueNotebookPage::SaveData(CMemoryWriter &writer)
{
	writer.w_stringZ(m_propertyCaption->GetValueAsString());
	writer.w_u8(m_propertyVisible->GetValueAsBoolean());
	writer.w_s32(m_propertyOrient->GetValueAsInteger());
	return IValueControl::SaveData(writer);
}