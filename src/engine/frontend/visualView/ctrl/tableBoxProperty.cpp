#include "tableBox.h"
#include "frontend/visualView/ctrl/form.h"
#include "frontend/visualView/visualHost.h"

#include "backend/metaData.h"
#include "backend/objCtor.h"
#include "backend/srcDataObject.h"   // ibSourceExplorer — family-blind column template (refill)

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

			while (GetChildCount() != 0) {
				g_visualHostContext->CutControl(GetChild(0), true);
			}

			// Columns come FAMILY-BLIND from the bound source's explorer — a metaobject source yields
			// its attributes, a queryable dynamic list yields its query columns — NOT a clsid→metaobject
			// gate (a dynamic list carries no metaobject, so the old gate refilled nothing). The source
			// is the head attribute's live value (the gate); each column binds THROUGH the tablebox's
			// own path: [tablebox path..., field] → "List.Field".
			const std::vector<ibSourceId> basePath = m_propertySource->GetValueAsPath();
			if (basePath.empty()) {
				// TYPE-ONLY source (Source = <not selected>, only a Type is set, e.g. CatalogList.Catalog1):
				// there is no head-holder path, so materialize the model straight from the Type and mirror
				// its columns — the SAME set the runtime CreateColumnCollection builds (that path is
				// designer-gated, so the designer needs this twin). Each column binds by its own metaID —
				// a 1-hop path (== the attribute), exactly the id GetModelColumn falls back to.
				ibValuePtr<ibValueModel> typeModel = ibTypeControlFactory::CreateAndConvertValueRef<ibValueModel>();
				ibValueModel::ibValueModelColumnCollection* cols = typeModel != nullptr ? typeModel->GetColumnCollection() : nullptr;
				if (cols != nullptr) {
					for (unsigned int idx = 0; idx < cols->GetColumnCount(); idx++) {
						ibValueModel::ibValueModelColumnCollection::ibValueModelColumnInfo* colInfo = cols->GetColumnInfo(idx);
						if (colInfo == nullptr)
							continue;
						ibValueModelTableBoxColumn* tableBoxColumn =
							dynamic_cast<ibValueModelTableBoxColumn*>(m_formOwner->CreateControl(wxT("TableboxColumn"), this));
						wxASSERT(tableBoxColumn);
						const ibTypeDescription columnType = colInfo->GetColumnType();
						if (columnType.IsOk())
							tableBoxColumn->SetDefaultMetaType(columnType);
						else
							tableBoxColumn->SetDefaultMetaType(ibValueTypes::TYPE_STRING);
						tableBoxColumn->SetCaption(colInfo->GetColumnCaption());
						tableBoxColumn->SetWidthColumn(colInfo->GetColumnWidth());
						tableBoxColumn->SetSource(std::vector<ibSourceId>{ (ibSourceId)colInfo->GetColumnID() });
						g_visualHostContext->InsertControl(tableBoxColumn, this);
					}
				}
			}
			else {
				ibBackendFormAttributeValue* holder = FindSourceHolder(basePath.front());
				ibSourceDataObject* source = holder != nullptr ? holder->GetSourceValue() : nullptr;
				if (source != nullptr) {
					const ibSourceExplorer* sourceExplorerPtr = source->GetSourceExplorer();
					static const ibSourceExplorer s_emptyExplorer;
					const ibSourceExplorer& sourceExplorer = sourceExplorerPtr != nullptr ? *sourceExplorerPtr : s_emptyExplorer;
					for (unsigned int idx = 0; idx < sourceExplorer.GetHelperCount(); idx++) {
						const ibSourceExplorer* columnPtr = sourceExplorer.GetHelper(idx);
						if (columnPtr == nullptr)
							continue;
						const ibSourceExplorer& column = *columnPtr;
						ibValueModelTableBoxColumn* tableBoxColumn =
							dynamic_cast<ibValueModelTableBoxColumn*>(m_formOwner->CreateControl(wxT("TableboxColumn"), this));
						wxASSERT(tableBoxColumn);
						tableBoxColumn->SetControlName(GetControlName() + column.GetSourceName());
						tableBoxColumn->SetCaption(column.GetSourceSynonym());
						std::vector<ibSourceId> colPath = basePath;
						colPath.push_back(column.GetSourceId());
						tableBoxColumn->SetSource(colPath);
						tableBoxColumn->SetVisibleColumn(column.IsVisible() || sourceExplorer.GetHelperCount() == 1);
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