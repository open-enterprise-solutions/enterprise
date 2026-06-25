#include "form.h"
#include "backend/metaCollection/metaFormObject.h"

// The form is serialized as ONE node tree: SaveControl / LoadControl drive the root
// through the binary provider, and ibValueFrame::SaveNode / LoadNode recurse the
// sub-controls as node children. No hand-rolled chunk framing here anymore — the
// provider owns the on-wire layout (kMetaBlock / kChildBlock per node), so an
// unregistered child clsid is skipped by NewObject == nullptr inside LoadNode.

bool ibValueForm::LoadForm(const wxMemoryBuffer& data)
{
	if (data.GetDataLen() == 0)
		return false;

	// Drop the current control tree before repopulating it from the blob.
	for (unsigned int idx = GetChildCount(); idx > 0; idx--) {
		ibValueFrame* controlChild = GetChild(idx - 1);
		if (controlChild != nullptr)
			RemoveControl(controlChild);
	}

	ibReaderMemory readerData(data);
	if (!LoadControl(m_metaFormObject, readerData))   // LoadNode recurses the whole subtree
		return false;

	// Controls loaded from memory → the form's attribute surface is stale.
	InvalidateNames();
	return true;
}

bool ibValueForm::SaveForm(wxMemoryBuffer& data) const
{
	ibWriterMemory writerData;
	if (!SaveControl(m_metaFormObject, writerData))   // SaveNode recurses the whole subtree
		return false;
	data = writerData.buffer();
	return true;
}
