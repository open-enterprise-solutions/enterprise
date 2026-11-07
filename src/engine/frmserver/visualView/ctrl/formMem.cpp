#include "form.h"
#include "formAttribute.h"   // ibFormAttributeValue — RemapIds walks the attributes
#include "formCommand.h"     // ibFormCommandValue — …and the form commands
#include "backend/metaCollection/metaFormObject.h"

// The form is serialized as ONE node tree: SaveControl / LoadControl drive the root
// through the binary provider, and ibValueFrame::SaveNode / LoadNode recurse the
// sub-controls as node children. No hand-rolled chunk framing here anymore — the
// provider owns the on-wire layout (kMetaBlock / kChildBlock per node), so an
// unregistered child clsid is skipped by NewObject == nullptr inside LoadNode.

bool ibValueForm::LoadForm(const ibMemoryBuffer& data)
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
	if (!LoadControl(m_metaFormObject, readerData))   // LoadNode / PasteNode recurses the whole subtree
		return false;

	// Controls loaded from memory → the form's attribute surface is stale.
	InvalidateNames();
	return true;
}

// #578 — what SaveForm writes, rewritten: the control tree and the form's command bar (ibValueFrame::RemapIds), each
// attribute (its type, the value it holds — attached to it) and each form command. Called on a form just loaded from
// its metaobject's data, which is then saved back (ibValueMetaObjectFormBase::RemapFormData).
bool ibValueForm::RemapIds(const ibMetaIdRemap& remap)
{
	bool remapped = ibValueFrame::RemapIds(remap);
	for (const auto& attribute : m_attributes)
		if (attribute != nullptr)
			remapped = attribute->RemapIds(remap) && remapped;
	for (const auto& command : m_formCommands)
		if (command != nullptr)
			remapped = command->RemapIds(remap) && remapped;
	InvalidateNames();
	return remapped;
}

bool ibValueForm::SaveForm(ibMemoryBuffer& data) const
{
	ibWriterMemory writerData;
	if (!SaveControl(m_metaFormObject, writerData))   // SaveNode recurses the whole subtree
		return false;
	data = writerData.buffer();
	return true;
}
