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

		if (answer == wxYES)
			RefillFromSource();
	}

	ibValueWindow::OnPropertyChanged(property, oldValue, newValue);
}

void ibValueModelTableBox::RefillFromSource()
{
	// Designer twin of the runtime CreateColumnCollection (which is DesignerMode-gated): (re)build the
	// tablebox's DEFAULT columns from its bound source explorer. ONE traversal, shared by the inspector's
	// Source-change refill AND the drag-to-create drop.
	while (GetChildCount() != 0)
		g_visualHostContext->CutControl(GetChild(0), true);

	// Columns come FAMILY-BLIND from the bound source's explorer — a metaobject source yields its
	// attributes, a queryable dynamic list its query columns — NOT a clsid->metaobject gate. Each column
	// binds THROUGH the tablebox's own path: [tablebox path..., field] -> "List.Field".
	const std::vector<ibSourceHop>& basePath = m_propertySource->GetValueAsPath();
	if (basePath.empty()) {
		// TYPE-ONLY source (only a Type is set, e.g. CatalogList.Catalog1): materialize the model straight
		// from the Type and mirror its columns — the SAME set the runtime CreateColumnCollection builds.
		// Each column binds by its own metaID (a 1-hop path == the attribute).
		ibValuePtr<ibValueModel> typeModel(ibTypeControlFactory::CreateAndConvertValueRef<ibValueModel>());
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
		ibBackendFormAttributeValue* holder = FindSourceHolder(m_propertySource->GetValueAsSourceDesc().GetFirst());
		ibSourceDataObject* source = holder != nullptr ? holder->GetSourceValue() : nullptr;
		const ibSourceExplorer* explorer = source != nullptr ? source->GetSourceExplorer() : nullptr;
		// WALK the path from the head attribute DOWN to the LEAF source node: a tabular section is a CHILD
		// of the object's explorer (its own helpers are the columns); a list attribute IS the head. Head-
		// only resolution filled an object's fields for a section-bound tablebox — the old bug.
		for (size_t i = 1; i < basePath.size() && explorer != nullptr; i++)
			explorer = explorer->FindById(basePath[i].m_id);
		if (explorer != nullptr) {
			// SNAPSHOT the columns BEFORE creating any control: SetSource() below re-materialises the holder's
			// source, whose GetSourceExplorer()->Reset() CLEARS the helper vector this loop reads through
			// (GetHelper returns &m_arraySource[idx]) -> the live nodes dangle mid-loop (freed, 0xDD fill), so
			// only the FIRST column reads real data. Read the explorer ONCE while stable, then build from the copy.
			struct ColumnSnap { ibSourceId id; wxString name; wxString synonym; wxString group; bool visible; };
			std::vector<ColumnSnap> snaps;
			for (unsigned int idx = 0; idx < explorer->GetHelperCount(); idx++) {
				const ibSourceExplorer* columnPtr = explorer->GetHelper(idx);
				if (columnPtr == nullptr)
					continue;
				snaps.push_back({ columnPtr->GetSourceId(), columnPtr->GetSourceName(),
					columnPtr->GetSourceSynonym(), columnPtr->GetSourceGroup(), columnPtr->IsVisible() });
			}

			for (const ColumnSnap& snap : snaps) {

				// COLUMNS THAT NAME THE SAME FAMILY COME OUT IN ONE GROUP — asked of the
				// table, the same way the auto-built form asks it. A newly made group has
				// to be shown to the editor as well, which is what `created` is for.
				bool createdGroup = false;
				ibValueFrame* holderControl = GetColumnGroupHolder(snap.group, &createdGroup);
				if (createdGroup)
					g_visualHostContext->InsertControl(holderControl, this);

				ibValueModelTableBoxColumn* tableBoxColumn =
					dynamic_cast<ibValueModelTableBoxColumn*>(m_formOwner->CreateControl(wxT("TableboxColumn"), holderControl));
				wxASSERT(tableBoxColumn);
				tableBoxColumn->SetControlName(GetControlName() + snap.name);
				tableBoxColumn->SetCaption(snap.synonym);
				ibSourceDescription colDesc;   // inherit the tablebox's own hops (with any pinned types), then this column
				for (const ibSourceHop& hop : basePath) colDesc.AppendSource(hop.m_id, hop.m_type);
				colDesc.AppendSource(snap.id);
				tableBoxColumn->SetSource(colDesc);
				tableBoxColumn->SetVisibleColumn(snap.visible);
				g_visualHostContext->InsertControl(tableBoxColumn, holderControl);
			}
		}
	}

	// No empty-fallback column: if the bound source yields no columns the tablebox stays empty (the user adds
	// them explicitly) — auto-adding one injected a spurious column INTO the value-table (see OnCreated).
	g_visualHostContext->RefreshEditor();
}