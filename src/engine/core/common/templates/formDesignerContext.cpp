#include "formDesigner.h"
#include "frontend/visualView/visualEditor.h"

CValue CFormDocument::GetCommonValueObject() const
{
	CFormEditView *editview = GetFormDesigner();
	if (editview != NULL) {
		CVisualEditorContextForm *m_visualView = editview->GetDesignerContext();
		return m_visualView->GetValueForm();
	}
	return CValue();
}