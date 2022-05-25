#include "tableBox.h"
#include "frontend/visualView/controls/form.h"
#include "frontend/visualView/visualEditor.h"
#include "frontend/visualView/visualHost.h"

#include "metadata/metadata.h"
#include "metadata/singleMetaTypes.h"

void CValueTableBox::OnPropertyChanged(Property* property)
{
	if (property->GetName() == wxT("source")) {
		
		int answer = wxMessageBox(		
			_("The data source has been changed. Refill columns?"), 
			_("TableBox"), wxYES_NO
		);

		if (answer == wxYES) {

			IMetadata* metaData = GetMetaData();

			while (GetChildCount() != 0) {
				m_visualHostContext->CutObject(GetChild(0), true);
			}

			auto clsids = CValueTableBox::GetClsids(); CLASS_ID clsid = 0;

			if (clsids.size() > 0) {
				auto itStart = clsids.begin();
				std::advance(itStart, 0);
				clsid = *itStart;
			}

			IMetaTypeObjectValueSingle* singleObject =
				metaData->GetTypeObject(clsid);

			if (singleObject != NULL) {

				ITableAttribute* metaObject =
					dynamic_cast<ITableAttribute*>(singleObject->GetMetaObject());

				if (metaObject != NULL) {

					for (auto attribute : metaObject->GetGenericAttributes()) {

						CValueTableBoxColumn* tableBoxColumn =
							wxDynamicCast(
								m_formOwner->CreateControl(wxT("tableboxColumn"), this), CValueTableBoxColumn
							);

						wxASSERT(tableBoxColumn);

						tableBoxColumn->m_controlName = m_controlName + wxT("_") + attribute->GetName();
						tableBoxColumn->m_caption = attribute->GetSynonym();
						tableBoxColumn->m_dataSource = attribute->GetMetaID();
						tableBoxColumn->m_enabled = true;
						tableBoxColumn->m_visible = true;

						tableBoxColumn->ReadProperty();

						m_visualHostContext->InsertObject(tableBoxColumn, this);
						tableBoxColumn->SaveProperty();
					}
				}
			}

			if (GetChildCount() == 0) {
				CValueTableBox::AddColumn();
			}

			m_visualHostContext->RefreshEditor();
		}
	}
}