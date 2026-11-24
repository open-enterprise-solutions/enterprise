#include "tableBox.h"
#include "form.h"

// The view's — what the person did in a cell of this column, handed by the table.
bool ibValueModelTableBoxColumn::OnClientEvent(ibProtocol::ibEvent::ibKind event, const ibDataNode& args)
{
	return m_provider->OnClientEvent(event, args);
}

void ibValueModelTableBoxColumn::ChoiceProcessing(ibValue& vSelected)
{
	m_provider->ChoiceProcessing(vSelected);
}

// THE EDIT IS OVER — what the cell editor's FinishEditing did once the editor was gone: the form refreshes,
// and then the rows are read again.
//
// ⭐⭐ A ROW EDITED OUT OF THE FILTER MUST LEAVE THE LIST, and THIS is the moment to say so. The write itself
// only changes the row, so a cell changed to something the filter no longer passes would stay on screen until
// something else happened to read again. The simplest true answer is to read again (Max, 2026-08-29).
//
// 🛑⭐⭐ AND THERE IS NO CONDITION ON IT. There was one — "only when there is a filter" — and it was wrong twice
// for the same reason: a FILTER decides whether a row belongs, a GROUPING decides where it belongs, and a SORT
// decides that too. The rule is the one an edit actually justifies — the edit is over, READ AGAIN — and what
// that means is the composer's to decide (Max: *"I suggest removing this check altogether"*).
void ibValueModelTableBoxColumn::OnEditingDone()
{
	if (m_formOwner != nullptr)
		m_formOwner->RefreshForm();

	if (ibValueModelTableBox* owner = GetOwner()) {
		if (ibValueModel* model = owner->GetTableModel())
			model->RefetchAll();
	}
}
