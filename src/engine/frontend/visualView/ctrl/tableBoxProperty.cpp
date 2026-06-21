#include "tableBox.h"
#include "frontend/visualView/ctrl/form.h"
#include "frontend/visualView/visualHost.h"

#include "backend/metaData.h"
#include "backend/objCtor.h"

void ibValueModelTableBox::OnPropertyCreated(ibProperty* property)
{
	//if (m_propertySource == property) {
	//	ibValueModelTableBox::SaveToVariant(m_propertySource->GetValue(), GetMetaData());
	//}
}

bool ibValueModelTableBox::OnPropertyChanging(ibProperty* property, const wxVariant& newValue)
{
	return ibValueWindow::OnPropertyChanging(property, newValue);
}

void ibValueModelTableBox::OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue)
{
	if (m_propertySource == property) {

		ibValueModelTableBox::RefreshModel(true);

		const int answer = wxMessageBox(
			_("The data source has been changed. Refill columns?"),
			_("TableBox"), wxYES_NO
		);

		if (answer == wxYES) {

			const ibMetaData* metaData = GetMetaData();

			while (GetChildCount() != 0) {
				g_visualHostContext->CutControl(GetChild(0), true);
			}

			const ibClassID& clsid = ibValueModelTableBox::GetFirstClsid();
			const ibCtorMetaValueType* typeCtor =
				metaData->GetTypeCtor(clsid);
			if (typeCtor != nullptr) {
				const ibValueMetaObjectCompositeData* metaObject = dynamic_cast<const ibValueMetaObjectCompositeData*>(typeCtor->GetMetaObject());
				if (metaObject != nullptr) {
					// Each column binds THROUGH the tablebox's own source path (the attribute gate):
					// [tablebox path..., field] — e.g. [mainAttr, field] → "List.Field". A bare field
					// id alone does NOT resolve through the gate (the garbage / no-type symptom).
					const std::vector<ibSourceId> basePath = m_propertySource->GetValueAsPath();
					for (const auto object : metaObject->GetGenericAttributeArrayObject()) {
						ibValueModelTableBoxColumn* tableBoxColumn =
							dynamic_cast<ibValueModelTableBoxColumn*>(m_formOwner->CreateControl(wxT("TableboxColumn"), this));
						wxASSERT(tableBoxColumn);
						tableBoxColumn->SetControlName(GetControlName() + object->GetName());
						tableBoxColumn->SetCaption(object->GetSynonym());
						std::vector<ibSourceId> colPath = basePath;
						colPath.push_back(object->GetMetaID());
						tableBoxColumn->SetSource(colPath);
						tableBoxColumn->SetVisibleColumn(true);
						g_visualHostContext->InsertControl(tableBoxColumn, this);
					}
				}
			}

			if (GetChildCount() == 0) {
				ibValueModelTableBox::AddColumn();
			}

			g_visualHostContext->RefreshEditor();
		}
	}

	ibValueWindow::OnPropertyChanged(property, oldValue, newValue);
}