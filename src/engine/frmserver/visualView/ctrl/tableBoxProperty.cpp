#include "tableBox.h"
#include "frmserver/visualView/ctrl/form.h"

void ibValueModelTableBox::OnPropertyCreated(ibProperty* property)
{
	//if (m_propertySource == property) {
	//	ibValueModelTableBox::SaveToVariant(m_propertySource->GetValue(), GetMetaData());
	//}
}

bool ibValueModelTableBox::OnPropertyChanging(ibProperty* property, const ibVariant& newValue)
{
	return ibValueWindow::OnPropertyChanging(property, newValue);
}

void ibValueModelTableBox::OnPropertyChanged(ibProperty* property, const ibVariant& oldValue, const ibVariant& newValue)
{
	// A NEW SOURCE, A NEW MODEL AND ITS COLUMNS — set in the inspector, by a script (ibValueFrame::SetPropVal) or by a
	// drop in the designer (SetBoundSource).
	if (m_propertySource == property) {
		ibValueModelTableBox::RefreshModel(true);
		ibValueModelTableBox::CreateColumnCollection();
	}

	ibValueWindow::OnPropertyChanged(property, oldValue, newValue);
}