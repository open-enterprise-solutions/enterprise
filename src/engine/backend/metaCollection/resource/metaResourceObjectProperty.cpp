#include "metaResourceObject.h"
#include "backend/metaData.h"

// ⭐ BALANCE IS THE OWNER'S QUESTION. Only an accounting register has two sides for a figure to
// balance across; for an accumulation or an information register the word means nothing, and a
// checkbox that means nothing is worse than an absent one — somebody will tick it and expect an
// effect. The same rule SelectMode and ItemMode already follow one class up.
void ibValueMetaObjectResource::OnPropertyRefresh()
{
	ibValueMetaObjectAttribute::OnPropertyRefresh();

	const ibValueMetaObject* owner = m_parent;
	const bool notAccounting = owner == nullptr || owner->GetClassType() != g_metaAccountingRegisterCLSID;
	HideProperty(m_propertyBalance, notAccounting);
	// …and the two kinds with them: they name what a CHART declares, and only an accounting register
	// stands on one.
	HideProperty(m_propertyAccountingKind, notAccounting);
	HideProperty(m_propertyAccountDimensionAccountingKind, notAccounting);
}
