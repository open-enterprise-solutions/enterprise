#include "toolbar.h"
#include "form.h"

#include "backend/system/value/valueEvent.h"

// Pointer-to-member referenced by the toolbar item's property
// initializer (&ibValueToolBarItem::GetToolAction in toolBar.h). Used
// by the designer to list available Actions for the tool item's
// Action property. Web build never exercises the walker (no property
// editor), but the symbol must exist for the pointer to link.
bool ibValueToolBarItem::GetToolAction(ibEventAction* evtList)
{
#ifndef OES_USE_WEB
	ibValueToolbar* toolbar = dynamic_cast<ibValueToolbar*> (m_parent);
	if (toolbar == nullptr) return false;
	ibValueFrame* sourceElement = toolbar->GetActionSrc() != wxNOT_FOUND ? FindControlByID(toolbar->GetActionSrc()) : nullptr;
	if (sourceElement != nullptr) {
		const ibStandardCommandSet& data = sourceElement->GetStandardCommands(sourceElement->GetTypeForm());
		for (unsigned int i = 0; i < data.GetCount(); i++) {
			const ibActionID& id = data.GetID(i);
			if (id == wxNOT_FOUND) continue;
			const ibPictureDescription& pictureDesc = data.GetPictureByID(id);
			evtList->AppendItem(
				data.GetNameByID(id),
				data.GetCaptionByID(id),
				id,
				pictureDesc.IsEmptyPicture() ? wxNullBitmap : ibBackendPicture::CreatePicture(pictureDesc, GetMetaData()),
				ibValue::CreateObjectValue<ibValueActionEvent>(data.GetNameByID(id), id)
			);
		}
	}

	return true;
#else
	(void)evtList;
	return false;
#endif
}