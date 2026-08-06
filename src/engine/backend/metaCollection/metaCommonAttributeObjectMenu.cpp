#include "metaCommonAttributeObject.h"
#include "backend/metaData.h"

// The two context menus of the common-attribute pair, kept apart from the mechanism the
// way every other metatype keeps them (metaSectionObjectMenu.cpp, metaScheduledJobObjectMenu.cpp).
//
// They are opposites, and that is the whole story of this feature in two functions: the
// DECLARATION offers the one thing worth doing to it — saying which objects carry it —
// while the COPY offers nothing but a way back to the declaration, because everything
// about a copy is decided there.

//***********************************************************************
//*                     The declaration, under Common                   *
//***********************************************************************

bool ibValueMetaObjectCommonAttribute::PrepareContextMenu(wxMenu* defaultMenu)
{
	// Named for what it opens, not for the machinery behind it: "composition" alone says
	// nothing to the person reading the menu — composition of what?
	wxMenuItem* menuItem = defaultMenu->Append(ID_METATREE_OPEN_COMPOSITION, _("Open common attributes composition"));
	menuItem->SetBitmap(GetIcon());
	defaultMenu->AppendSeparator();
	return false;   // false = the standard New / Edit / Remove block still applies
}

void ibValueMetaObjectCommonAttribute::ProcessCommand(unsigned int id)
{
	ibBackendMetadataTree* metaTree = m_metaData != nullptr ? m_metaData->GetMetaTree() : nullptr;
	if (metaTree == nullptr)
		return;

	if (id == ID_METATREE_OPEN_COMPOSITION)
		metaTree->OpenObjectForm(this);
}

//***********************************************************************
//*                   The copy that lives in an object                  *
//***********************************************************************

bool ibValueMetaObjectCommonAttributeColumn::PrepareContextMenu(wxMenu* defaultMenu)
{
	// TRUE = the standard New / Edit / Remove block is NOT appended (see the designer
	// tree's context-menu builder). A copy is looked at, not edited: everything about it
	// belongs to the declaration under Common.
	wxMenuItem* menuItem = defaultMenu->Append(ID_METATREE_OPEN_SOURCE, _("Go to common attribute"));
	menuItem->SetBitmap(GetIcon());
	defaultMenu->AppendSeparator();
	return true;
}

void ibValueMetaObjectCommonAttributeColumn::ProcessCommand(unsigned int id)
{
	ibBackendMetadataTree* metaTree = m_metaData != nullptr ? m_metaData->GetMetaTree() : nullptr;
	if (metaTree == nullptr || GetSource() == nullptr)
		return;

	// The one useful action on a copy: take me to where this is actually edited.
	if (id == ID_METATREE_OPEN_SOURCE)
		metaTree->OpenObjectForm(GetSource());
}
