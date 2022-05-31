#include "notebook.h"
#include "frontend/visualView/visualEditor.h"  

//***********************************************************************************
//*                           IMPLEMENT_DYNAMIC_CLASS                               *
//***********************************************************************************

wxIMPLEMENT_DYNAMIC_CLASS(CValueNotebookPage, IValueFrame);

//***********************************************************************************
//*                              CValueNotebookPage                                 *
//***********************************************************************************

CValueNotebookPage::CValueNotebookPage() : IValueControl(),
m_caption("NewPage"), m_visible(true),
m_orient(wxVERTICAL)
{
	PropertyContainer *categoryPage = IObjectBase::CreatePropertyContainer("Page");
	categoryPage->AddProperty("name", PropertyType::PT_WXNAME);
	categoryPage->AddProperty("caption", PropertyType::PT_WXSTRING);
	categoryPage->AddProperty("visible", PropertyType::PT_BOOL);
	categoryPage->AddProperty("icon", PropertyType::PT_BITMAP);

	PropertyContainer *categorySizer = IObjectBase::CreatePropertyContainer("Sizer");
	categorySizer->AddProperty("orient", PropertyType::PT_OPTION, &CValueNotebookPage::GetOrient);

	m_category->AddCategory(categoryPage);
	m_category->AddCategory(categorySizer);
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

	if (auiNotebookWnd && m_visible) {
		auiNotebookWnd->AddPage(newPage, m_caption, false, m_bitmap);
		newPage->SetOrientation(m_orient);
	}

	if (visualHost->IsDesignerHost()) {
		newPage->PushEventHandler(new CDesignerWindow::HighlightPaintHandler(newPage));
	}
}

void CValueNotebookPage::OnUpdated(wxObject* wxobject, wxWindow* wxparent, IVisualHost *visualHost)
{
	IValueFrame *parentControl = GetParent(); int idx = wxNOT_FOUND;

	for (unsigned int i = 0; i < parentControl->GetChildCount(); i++)
	{
		CValueNotebookPage *child = dynamic_cast<CValueNotebookPage *>(parentControl->GetChild(i));
		wxASSERT(child);
		if (m_controlId == child->m_controlId) { idx = i; break; }
	}

	CPageWindow *pageNotebook = dynamic_cast<CPageWindow *>(wxobject);
	wxASSERT(pageNotebook);
	wxAuiNotebook *auiNotebookWnd = dynamic_cast<wxAuiNotebook *>(wxparent);
	wxASSERT(auiNotebookWnd);

	int idxPage = wxNOT_FOUND;
	for (unsigned int pageIndex = 0; pageIndex < auiNotebookWnd->GetPageCount(); pageIndex++) { if (auiNotebookWnd->GetPage(pageIndex) == pageNotebook) { idxPage = pageIndex; break; } }
	if (idxPage != wxNOT_FOUND) auiNotebookWnd->RemovePage(idxPage);

	if (m_visible)
	{
		auiNotebookWnd->InsertPage(idx, pageNotebook, m_caption, true, m_bitmap);

		auiNotebookWnd->SetPageText(idx, m_caption);
		auiNotebookWnd->SetPageBitmap(idx, m_bitmap);

		pageNotebook->SetOrientation(m_orient);
	}
}

void CValueNotebookPage::OnSelected(wxObject* wxobject)
{
	IValueFrame *parentCtrl = GetParent();
	wxASSERT(parentCtrl);
	wxAuiNotebook *auiNotebookWnd = dynamic_cast<wxAuiNotebook *>(parentCtrl->GetWxObject());
	wxASSERT(auiNotebookWnd);
	int idxPage = wxNOT_FOUND;
	for (unsigned int pageIndex = 0; pageIndex < auiNotebookWnd->GetPageCount(); pageIndex++) { if (auiNotebookWnd->GetPage(pageIndex) == wxobject) { idxPage = pageIndex; break; } }
	if (idxPage != auiNotebookWnd->GetSelection()) auiNotebookWnd->SetSelection(idxPage);
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
		for (unsigned int idx = 0; idx < auiNotebookWnd->GetPageCount(); idx++) { if (auiNotebookWnd->GetPage(idx) == wxobject) { page = idx; break; } }
		if (page != wxNOT_FOUND) { auiNotebookWnd->RemovePage(page); newPage->Hide(); }
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
	reader.r_stringZ(m_caption);
	m_visible = reader.r_u8();
	m_orient = reader.r_s32(); 
	return IValueControl::LoadData(reader);
}

bool CValueNotebookPage::SaveData(CMemoryWriter &writer)
{
	writer.w_stringZ(m_caption);
	writer.w_u8(m_visible);
	writer.w_s32(m_orient);
	return IValueControl::SaveData(writer);
}

void CValueNotebookPage::ReadProperty()
{
	IObjectBase::SetPropertyValue("name", m_controlName);
	IObjectBase::SetPropertyValue("caption", m_caption);
	IObjectBase::SetPropertyValue("visible", m_visible);
	IObjectBase::SetPropertyValue("icon", m_bitmap);
	IObjectBase::SetPropertyValue("orient", m_orient);
}

void CValueNotebookPage::SaveProperty()
{
	IObjectBase::GetPropertyValue("name", m_controlName);
	IObjectBase::GetPropertyValue("caption", m_caption);
	IObjectBase::GetPropertyValue("visible", m_visible);
	IObjectBase::GetPropertyValue("icon", m_bitmap);
	IObjectBase::GetPropertyValue("orient", m_orient);
}