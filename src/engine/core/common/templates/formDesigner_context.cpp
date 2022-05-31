#include "formDesigner.h"
#include "frontend/visualView/visualEditor.h"

CValue CFormDocument::GetCommonValueObject()
{
	CFormEditView *m_editview = GetFormDesigner();
	
	if (m_editview)
	{
		CVisualEditorContextForm *m_visualView = m_editview->GetDesignerContext();
		return m_visualView->GetValueForm();
	}

	return CValue();
}