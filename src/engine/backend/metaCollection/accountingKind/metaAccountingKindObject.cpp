////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : meta-accounting kinds of a chart of accounts
////////////////////////////////////////////////////////////////////////////

#include "metaAccountingKindObject.h"
#include "backend/metaData.h"

// A kind is filtered like the boolean field it is — the attribute's own answer, kept here only so the
// two classes have one place to grow a different one if the day comes.
ibSelectorDataType ibValueMetaObjectAccountingKind::GetFilterDataType() const
{
	return ibSelectorDataType::ibSelectorDataType_boolean;
}

// ⭐ THE PICTURE IS THE ATTRIBUTE'S, ON PURPOSE. A kind of accounting IS a boolean field of the account
// — the branch it stands in already says which sort of field — so the attribute's icon is true rather
// than a stand-in, and the engine's common set has no tick-box of its own to borrow instead. Art of
// their own is worth having (a tick-box, and a tick-box over a breakdown) and is a picture to be drawn,
// not a mechanism to be built: dropping two base64 PNGs in here is the whole of it.
wxIcon ibValueMetaObjectAccountingKind::GetIcon() const
{
	return GetIconGroup();
}

wxIcon ibValueMetaObjectAccountingKind::GetIconGroup()
{
	return ibValueMetaObjectAttribute::GetIconGroup();
}

wxIcon ibValueMetaObjectAccountDimensionAccountingKind::GetIcon() const
{
	return GetIconGroup();
}

wxIcon ibValueMetaObjectAccountDimensionAccountingKind::GetIconGroup()
{
	return ibValueMetaObjectAttribute::GetIconGroup();
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(ibValueMetaObjectAccountingKind, "AccountingKind", g_metaAccountingKindCLSID);
METADATA_TYPE_REGISTER(ibValueMetaObjectAccountDimensionAccountingKind, "AccountDimensionAccountingKind", g_metaAccountDimensionAccountingKindCLSID);
