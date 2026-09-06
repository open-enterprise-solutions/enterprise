////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : tabular sections - model
////////////////////////////////////////////////////////////////////////////

#include "tabularSection.h"
//***********************************************************************************
//*                                  Model                                          *
//***********************************************************************************

// The row's place in what is SHOWN. The display order is the composer's — the very order the fetch windows
// into pages — so it is asked of the composer and kept until the VIEW GENERATION moves (a filter, a sort, an
// added or deleted row all bump it). Without the cache this would run a full order per painted cell.
long ibValueTabularSectionDataObjectBase::DisplayNumberOf(const ibDataViewItem& row) const
{
	const ibComposerNode* node = GetViewData<ibComposerNode>(row);
	if (node == nullptr || node->IsGroup())
		return 0;   // a group header is synthetic — it is no line of the section and carries no number

	const long index = StorageIndexOf(row);
	if (index < 0)
		return 0;   // not a row of this table

	if (!m_displayOrderRead || m_displayOrderGeneration != GetViewGeneration()) {
		Storage().SetColumns(GetColumnCollection());   // let the composer resolve field names → column ids
		m_displayOrder = GetModelComposer().ComputeOrder();
		m_displayOrderGeneration = GetViewGeneration();
		m_displayOrderRead = true;
	}

	for (size_t position = 0; position < m_displayOrder.size(); ++position)
		if (m_displayOrder[position] == index)
			return static_cast<long>(position) + 1;

	return 0;   // filtered out — shown nowhere, so numbered nowhere
}

void ibValueTabularSectionDataObjectBase::GetValueByRow(wxVariant& variant,
	const ibDataViewItem& row, unsigned int col) const
{
	ibComposerNode* node = GetViewData<ibComposerNode>(row);
	if (node == nullptr) return;

	if (m_metaTable->IsNumberLine(col)) {
		// ⭐⭐ THE NUMBER A PERSON READS IS THE ROW'S PLACE IN WHAT IS SHOWN — 1, 2, 3, 4, 5, 6, however they
		// have played with the order (Max, 2026-08-29). It comes off the FETCH, which is the one step that
		// knows it, because producing the display order is what it does.
		//
		// 🛑 It used to be the STORAGE index. Truthful about the data and wrong on the screen: under a sort it
		// came out jumbled — 1,2,3,4,6,5 — and under a filter it counted rows nobody could see. The stored
		// line number is still the storage order; it is simply not what this column is.
		//
		// 🛑 And a GROUP HEADER is not a line of the section at all — synthetic, in no storage, no number. It
		// used to print "1" on every header, which is the strange numbering a hierarchical view showed.
		const long number = DisplayNumberOf(row);
		if (number <= 0)
			return;   // nothing to number — leave the cell empty
		variant = new ibVariantDataValueNumberLine(number);
	}
	else if (node->HasColumnValue(col))
		node->GetValue(col, variant);
}

#include "backend/metaData.h"

bool ibValueTabularSectionDataObjectBase::SetValueByRow(const wxVariant& variant,
	const ibDataViewItem& row, unsigned int col)
{
	ibComposerNode* node = GetViewData<ibComposerNode>(row);
	
	if (node == nullptr) return false;

	if (!m_metaTable->IsNumberLine(col)) {
		const ibMetaData* metaData = m_metaTable->GetMetaData();
		wxASSERT(metaData);
		const bool hasCol = node->HasColumnValue(col);
		
		if (hasCol) {

			const wxString& strData = variant.GetString();

			const ibValue& selValue = node->GetTableValue(col);
			const ibValue& newValue = metaData->CreateObject(selValue.GetClassType());

			// The write's own verdict is the answer. It was being dropped into an unread
			// `ok` — and SetValueByMetaID says false for the cases that matter: the section
			// is READ-ONLY, the column is the line number, the attribute or node is gone.
			// Reporting success there tells the view the cell took a value it never stored.
			if (strData.Length() > 0) {
				std::vector<ibValue> listValue;
				const bool found = newValue.FindValue(strData, listValue);

				if (!found)
					return false;

				if (!SetValueByMetaID(row, col, listValue.at(0)))
					return false;
			}
			else if (!SetValueByMetaID(row, col, newValue)) {
				return false;
			}
		}
		else {
			return false;
		}
	};

	// Append/Insert/Remove fire ItemInserted / ItemDeleted; SetValue path
	// fires ValueChanged.  Narrow notifies update the view in place
	// via the control's narrow ItemInserted / ValueChanged paths
	// (RAM-backed gate).  No NotifyReset on every mutation — the
	// heavy hammer caused user-visible flicker on copy / edit.
	return true;
}

void ibValueTabularSectionDataObjectBase::AddValue(const ibDataViewItem& row, const ibDataViewItem& anchor)
{
	// Two different questions, and the front answers both: WHERE the row goes is the selection (user on row 3
	// + Add → new row at 4, focus follows via the ItemInserted handler's Select); WHAT it inherits is the
	// group the user is INSIDE — the anchor, the folder they drilled into.
	//
	// 🛑 Both used to be the selection, so at the ROOT of a hierarchical view the new row copied the
	// highlighted group HEADER's dimension and fell into that group. Standing in no group it must inherit
	// nothing and form an empty group of its own (Max, 2026-08-29).
	const long idx = StorageIndexOf(row);   // displayed item is a composer copy → storage index via bridge
	if (idx >= 0) AppendRow(idx + 1, anchor);
	else          AppendRow(0, anchor);
}

void ibValueTabularSectionDataObjectBase::CopyValue(const ibDataViewItem& row)
{
	const ibDataViewItem& currentItem = row;
	if (!currentItem.IsOk())
		return;
	// The displayed item is a composer COPY — resolve the REAL storage row (and its index) via the bridge.
	ibComposerNode* node = StorageRowOf(currentItem);
	if (node == nullptr)
		return;
	ibComposerNode* rowData = new ibComposerNode();
	// THE GENERIC LIST — the section's own columns AND the predefined ones. GetAttributeArrayObject
	// answers "what did a person add to this section", which is the right question for the designer
	// tree and the wrong one for copying a ROW: every other walk over a row (create, read, write —
	// tabularSection.cpp, tabularSectionQuery.cpp) takes the generic list, and this one did not. A
	// section whose columns are ALL predefined — a chart of accounts' analytics kinds — therefore
	// copied nothing at all: the new row arrived with a line number and four empty cells.
	for (const auto object : m_metaTable->GetGenericAttributeArrayObject()) {
		if (!m_metaTable->IsNumberLine(object->GetMetaID())) {
			rowData->AppendTableValue(object->GetMetaID(), node->GetTableValue(object->GetMetaID()));
		}
		else {
			rowData->AppendTableValue(object->GetMetaID(), ibValue());
		}
	}
	// Copy goes AFTER the source row so the original keeps its
	// position; the new row lands at currentLine+1 and ItemInserted
	// pushes focus onto it via Select.
	long currentLine = StorageIndexOf(currentItem);
	if (currentLine != wxNOT_FOUND) {
		ibValueModelStorage::Insert(rowData, currentLine + 1, !ibBackendException::IsEvalMode());
	}
	else {
		ibValueModelStorage::Append(rowData, !ibBackendException::IsEvalMode());
	}
}

void ibValueTabularSectionDataObjectBase::EditValue(const ibDataViewItem& row)
{
	// Inline editing is opened by the TableBox on the control (front, OnItemActivated → EditItem) — the model no
	// longer tells the control to start editing. (Was RowValueStartEdit → notifier StartEditing, now gone.)
	(void)row;
}

void ibValueTabularSectionDataObjectBase::DeleteValue(const ibDataViewItem& row)
{
	const ibDataViewItem& currentItem = row;
	if (!currentItem.IsOk())
		return;
	// The displayed item is a composer COPY — Remove needs the REAL storage row (a pointer-find of the
	// copy in m_nodeValues would miss), resolved via the bridge.
	ibComposerNode* node = StorageRowOf(currentItem);
	if (node == nullptr)
		return;

	if (!ibBackendException::IsEvalMode()) {
		ibValueModelStorage::Remove(node);
	}
}

// The two ORDER verbs — one line each: the RAM base already carries them out (re-seat the row / set the
// order the rows are read in) and notifies the view. They exist as methods of this class only so the
// reference-owned section below can wrap them, exactly as it wraps Copy and Delete.
void ibValueTabularSectionDataObjectBase::MoveValue(const ibDataViewItem& row, int delta)
{
	MoveRow(row, delta);
}

void ibValueTabularSectionDataObjectBase::SortValue(const ibDataViewColumnItem& column, bool ascending)
{
	if (!column.IsOk() || ibBackendException::IsEvalMode())
		return;

	// ⭐⭐ A SORT CHANGES THE DATA — the rows are re-seated, and that is the order the write will store (Max,
	// 2026-08-29: moving and sorting change the rows physically; a filter, a grouping or a setting is a layer
	// ABOVE them and dies with the window). So the composer's own sort is dropped again at the end: the rows
	// now SIT in this order, and a read-order left on top would go on answering over them.
	//
	// L5-2 says WHAT the order is — nothing here compares — and the rows are then placed into it with the
	// storage's own move. Sorting lives on L5, not in a loop.
	ibDataRamComposer& composer = GetModelComposer();
	composer.ClearSorts();
	composer.Sort(column.m_name, ascending);
	Storage().SetColumns(GetColumnCollection());
	const std::vector<long> order = composer.ComputeOrder();
	composer.ClearSorts();

	// The rows in their new sequence, taken BEFORE anything moves — every move shifts the indices under it.
	std::vector<ibComposerNode*> seated;
	seated.reserve(order.size());
	for (const long index : order)
		if (ibComposerNode* node = Storage().GetNode(index))
			seated.push_back(node);

	// ⚠ Rows a FILTER hides are not in the order, so they come to rest AFTER the sorted ones, keeping their
	// own relative order among themselves. Sorting what is shown cannot avoid saying something about what is not.
	for (size_t position = 0; position < seated.size(); ++position) {
		const long current = Storage().IndexOf(seated[position]);
		if (current >= 0 && current != static_cast<long>(position))
			Storage().MoveValue(this, seated[position],
				static_cast<int>(static_cast<long>(position) - current), /*notify*/ false);
	}

	NotifyReset();
}

void ibValueTabularSectionDataObjectRef::CopyValue(const ibDataViewItem& row)
{
	ibValueTabularSectionDataObjectBase::CopyValue(row);

	if (!ibBackendException::IsEvalMode()) {
		ibBackendValueForm* const foundedForm = ibFormToNotify([this] {
			return ibBackendValueForm::FindFormByUniqueKey(m_objectValue->GetGuid());
		});
		if (foundedForm != nullptr) {
			foundedForm->Modify(true);
		}
	}
}

// The two ORDER verbs, wrapped for the same reason Copy and Delete are: a section that belongs to a
// REFERENCE object has to mark that object modified, and the base has no object to mark.
//
// ⚠ THROUGH THE OBJECT, the way AppendRow does it — the object's own Modify raises the open form too
// (ibValueRecordDataObjectRef::Modify), so the flag is never left on a window while the object it belongs
// to still calls itself unchanged.
void ibValueTabularSectionDataObjectRef::MoveValue(const ibDataViewItem& row, int delta)
{
	ibValueTabularSectionDataObjectBase::MoveValue(row, delta);

	if (!ibBackendException::IsEvalMode())
		m_objectValue->Modify(true);
}

void ibValueTabularSectionDataObjectRef::SortValue(const ibDataViewColumnItem& column, bool ascending)
{
	ibValueTabularSectionDataObjectBase::SortValue(column, ascending);

	if (!ibBackendException::IsEvalMode())
		m_objectValue->Modify(true);
}

void ibValueTabularSectionDataObjectRef::DeleteValue(const ibDataViewItem& row)
{
	ibValueTabularSectionDataObjectBase::DeleteValue(row);

	if (!ibBackendException::IsEvalMode()) {
		ibBackendValueForm* const foundedForm = ibFormToNotify([this] {
			return ibBackendValueForm::FindFormByUniqueKey(m_objectValue->GetGuid());
		});
		if (foundedForm != nullptr) foundedForm->Modify(true);
	}
}