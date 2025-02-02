#include "notebook.h"
#include "frontend/visualView/pageWindow.h"

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

wxObject* CValueNotebookPage::Create(wxWindow* wxparent, IVisualHost* visualHost)
{
	return new CPanelPage(wxparent, wxID_ANY);
}

void CValueNotebookPage::OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost, bool first—reated)
{
	CPanelPage* page = dynamic_cast<CPanelPage*>(wxobject);
	wxASSERT(page);
	
	wxAuiNotebook* notebook = dynamic_cast<wxAuiNotebook*>(wxparent);
	
	if (notebook != nullptr && m_propertyVisible->GetValueAsBoolean()) {
		notebook->AddPage(page, m_propertyCaption->GetValueAsString(), false, m_propertyIcon->GetValueAsBitmap());
		page->SetOrientation(m_propertyOrient->GetValueAsInteger());
	}
	
	if (visualHost->IsDesignerHost()) {
		page->PushEventHandler(g_visualHostContext->GetHighlightPaintHandler(page));
	}
}

void CValueNotebookPage::OnUpdated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost)
{
	IValueFrame* parentControl = GetParent(); int pos = wxNOT_FOUND;
	if (m_propertyVisible->GetValueAsBoolean()) {
		for (unsigned int i = 0; i < parentControl->GetChildCount(); i++) {
			CValueNotebookPage* child = dynamic_cast<CValueNotebookPage*>(parentControl->GetChild(i));
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
			notebook->InsertPage(pos, (wxWindow*)wxobject, m_propertyCaption->GetValueAsString(), pos_old == wxNOT_FOUND, m_propertyIcon->GetValueAsBitmap());
		notebook->SetPageText(pos, m_propertyCaption->GetValueAsString());
		notebook->SetPageBitmap(pos, m_propertyIcon->GetValueAsBitmap());
		CPanelPage* page = dynamic_cast<CPanelPage*>(wxobject);
		wxASSERT(page);
		page->SetOrientation(m_propertyOrient->GetValueAsInteger());
	}
}

void CValueNotebookPage::OnSelected(wxObject* wxobject)
{
	wxAuiNotebook* notebook = dynamic_cast<wxAuiNotebook*>(GetParent()->GetWxObject());
	wxASSERT(notebook);
	if (notebook != nullptr) {
		int pos = notebook->GetPageIndex((wxWindow*)wxobject);
		if (pos != notebook->GetSelection())
			notebook->SetSelection(pos);
	}
}

void CValueNotebookPage::Cleanup(wxObject* wxobject, IVisualHost* visualHost)
{
	wxAuiNotebook* notebook = dynamic_cast<wxAuiNotebook*>(visualHost->GetWxObject(GetParent()));
	wxASSERT(notebook);
	if (notebook != nullptr) {
		int pos = notebook->GetPageIndex((wxWindow*)wxobject);
		notebook->RemovePage(pos);
	}

	if (visualHost->IsDesignerHost()) {
		CPanelPage* page = dynamic_cast<CPanelPage*>(wxobject);
		wxASSERT(page);
		page->PopEventHandler(true);
	}
}

bool CValueNotebookPage::CanDeleteControl() const
{
	return m_parent->GetChildCount() > 1;
}

//***********************************************************************************
//*                              Read & save property                               *
//***********************************************************************************

bool CValueNotebookPage::LoadData(CMemoryReader& reader)
{
	wxString caption; reader.r_stringZ(caption);
	m_propertyCaption->SetValue(caption);
	m_propertyVisible->SetValue(reader.r_u8());
	m_propertyOrient->SetValue(reader.r_s32());
	return IValueControl::LoadData(reader);
}

bool CValueNotebookPage::SaveData(CMemoryWriter& writer)
{
	writer.w_stringZ(m_propertyCaption->GetValueAsString());
	writer.w_u8(m_propertyVisible->GetValueAsBoolean());
	writer.w_s32(m_propertyOrient->GetValueAsInteger());
	return IValueControl::SaveData(writer);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

S_CONTROL_TYPE_REGISTER(CValueNotebookPage, "notebookPage", "notebookPage", g_controlNotebookPageCLSID);