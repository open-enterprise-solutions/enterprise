#include "tableBox.h"
#include "frontend/visualView/controls/form.h"
#include "frontend/visualView/visualEditor.h"
#include "frontend/visualView/visualHost.h"

#include "core/metadata/metadata.h"
#include "core/metadata/singleClass.h"

void CValueTableBox::OnPropertyCreated(Property* property)
{
	if (m_propertySource == property) {
		CValueTableBox::SaveToVariant(m_propertySource->GetValue(), GetMetaData());
	}
}

bool CValueTableBox::OnPropertyChanging(Property* property, const wxVariant& newValue)
{
	if (m_propertySource == property && !CValueTableBox::LoadFromVariant(newValue))
		return false;
	return IValueWindow::OnPropertyChanging(property, newValue);
}

void CValueTableBox::OnPropertyChanged(Property* property)
{
	if (m_propertySource == property) {
		
		int answer = wxMessageBox(		
			_("The data source has been changed. Refill columns?"), 
			_("TableBox"), wxYES_NO
		);

		if (answer == wxYES) {

			IMetadata* metaData = GetMetaData();

			while (GetChildCount() != 0) {
				g_visualHostContext->CutControl(GetChild(0), true);
			}

			const CLASS_ID& clsid = CValueTableBox::GetFirstClsid();
			IMetaTypeObjectValueSingle* singleObject =
				metaData->GetTypeObject(clsid);
			if (singleObject != NULL) {
				IMetaTableData* metaObject =
					dynamic_cast<IMetaTableData*>(singleObject->GetMetaObject());
				if (metaObject != NULL) {
					for (auto attribute : metaObject->GetGenericAttributes()) {
						CValueTableBoxColumn* tableBoxColumn =
							wxDynamicCast(
								m_formOwner->CreateControl(wxT("tableboxColumn"), this), CValueTableBoxColumn
						);
						wxASSERT(tableBoxColumn);
						tableBoxColumn->SetControlName(GetControlName() + wxT("_") + attribute->GetName());
						tableBoxColumn->SetCaption(attribute->GetSynonym());
						tableBoxColumn->SetSourceId(attribute->GetMetaID());
						tableBoxColumn->SetVisibleColumn(true);
						g_visualHostContext->InsertControl(tableBoxColumn, this);
					}
				}
			}

			if (GetChildCount() == 0) {
				CValueTableBox::AddColumn();
			}

			g_visualHostContext->RefreshEditor();
		}
	}
}