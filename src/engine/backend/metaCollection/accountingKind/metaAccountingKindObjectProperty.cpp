#include "metaAccountingKindObject.h"
#include "backend/metaData.h"

// ⚠ THE TYPE IS PART OF THE METATYPE, so the property offering to change it is taken off the sheet.
// Hidden rather than merely defaulted: an author who set a kind to String would get a field that every
// reader of kinds ignores and no writer refuses — a silent hole, for a question that has only ever had
// two answers.
void ibValueMetaObjectAccountingKind::OnPropertyRefresh()
{
	ibValueMetaObjectAttribute::OnPropertyRefresh();
	HideProperty(GetProperty(wxT("Type")));
}
