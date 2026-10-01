#include "metaDimensionObject.h"
#include "backend/metaData.h"

// ⭐ BOTH QUESTIONS ARE THE OWNER'S. Only an accounting register has two sides for a dimension to
// differ across, and only it stands on a chart whose flags there are to name. For an accumulation or
// an information register the words mean nothing, and a control that means nothing is worse than an
// absent one — somebody will set it and expect an effect. The same rule the resource's Balance
// follows, one class up.
void ibValueMetaObjectDimension::OnPropertyRefresh()
{
	ibValueMetaObjectAttribute::OnPropertyRefresh();

	const ibValueMetaObject* owner = m_parent;
	const bool notAccounting = owner == nullptr || owner->GetClassType() != g_metaAccountingRegisterCLSID;
	HideProperty(m_propertyBalance, notAccounting);
	HideProperty(m_propertyAccountingKind, notAccounting);
}
