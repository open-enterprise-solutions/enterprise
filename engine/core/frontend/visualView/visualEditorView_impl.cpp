////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxFormBuilder
//	Description : visual view events 
////////////////////////////////////////////////////////////////////////////

#include "visualEditorView.h"

void CVisualView::ShowForm()
{
	m_valueForm->ShowForm(NULL); //has no parent
}

void CVisualView::ActivateForm()
{
	m_valueForm->ActivateForm();
}

void CVisualView::UpdateForm()
{
	m_valueForm->UpdateForm();
}

bool CVisualView::CloseForm()
{
	return m_valueForm->CloseForm();
}
