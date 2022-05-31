////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxFormBuilder
//	Description : visual view events 
////////////////////////////////////////////////////////////////////////////

#include "visualHost.h"

void CVisualHost::ShowForm()
{
	m_valueForm->ShowForm(NULL); //has no parent
}

void CVisualHost::ActivateForm()
{
	m_valueForm->ActivateForm();
}

void CVisualHost::UpdateForm()
{
	m_valueForm->UpdateForm();
}

bool CVisualHost::CloseForm()
{
	return m_valueForm->CloseForm();
}
