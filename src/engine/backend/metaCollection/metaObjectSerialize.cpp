////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metaobject node (de)serialization + tree lifecycle walks
//	              (SaveMeta/LoadMeta, Save/Load/Delete/Run/CloseSubtree) —
//	              split out of metaObject.cpp
////////////////////////////////////////////////////////////////////////////

#include "metaObject.h"
#include "backend/appData.h"

#include "backend/metaData.h"
#include "backend/backend_exception.h"
#include "backend/databaseLayer/databaseErrorCodes.h"
#include "backend/serialize/dataBuilder.h"   // ibDataNode — universal structure bridge (BuildDataNode/ApplyDataNode)

// SaveNode — write this object into the node. Common header (intrinsics → fields,
// editable values → props, interface/roles), then per-type WriteData. Every value
// is in its REAL form, so a JSON view shows `"Name": "Price"`, not a base64 blob.
bool ibValueMetaObject::SaveNode(ibDataNode& node) const
{
	// intrinsics → fields
	node.SetValue(wxT("Guid"), m_metaGuid);
	node.SetValue(wxT("Id"), (s32)m_metaId);
	node.SetValue(wxT("Deleted"), IsDeleted());
	node.SetValue(wxT("Help"), m_strHelpContent);

	// system marker — a predefined (engine-created) object, so a consumer (the AI)
	// can tell it apart from a user-defined one. Only emitted when set; not read back
	// (the flag is re-derived on construction).
	if (IsPinnedToParent())
		node.SetValue(wxT("NodePredefined"), true);

	// editable property values → props (owner names them inline; the property yields the value)
	node.SetProperty(m_propertyName->GetName(),    m_propertyName->GetNodeValue());
	node.SetProperty(m_propertySynonym->GetName(), m_propertySynonym->GetNodeValue());
	node.SetProperty(m_propertyComment->GetName(), m_propertyComment->GetNodeValue());

	// interface / roles → fields (opaque sub-structures; become Child later)
	ibWriterMemory interfaceWriter;
	if (!SaveInterface(interfaceWriter))
		return false;
	node.SetValue(wxT("Interface"), interfaceWriter.buffer());

	ibWriterMemory roleWriter;
	if (!SaveRole(roleWriter))
		return false;
	node.SetValue(wxT("Roles"), roleWriter.buffer());

	return WriteData(node);
}

// LoadNode — read this object from a CONST node. Mirror of SaveNode.
bool ibValueMetaObject::LoadNode(const ibDataNode& node)
{
	// intrinsics ← fields
	m_metaGuid = node.GetValue<ibGuid>(wxT("Guid"));
	m_metaId = (ibMetaID)node.GetValue<s32>(wxT("Id"));
	if (node.GetValue<bool>(wxT("Deleted")))
		MarkAsDeleted();
	m_strHelpContent = node.GetValue<wxString>(wxT("Help"));

	// editable property values ← props (owner names them inline; the property takes the value)
	m_propertyName->ReadNodeValue(node.GetProperty(m_propertyName->GetName()));
	m_propertySynonym->ReadNodeValue(node.GetProperty(m_propertySynonym->GetName()));
	m_propertyComment->ReadNodeValue(node.GetProperty(m_propertyComment->GetName()));

	// interface / roles ← fields
	wxMemoryBuffer interfaceBuf = node.GetValue<wxMemoryBuffer>(wxT("Interface"));
	if (interfaceBuf.GetDataLen()) {
		ibReaderMemory reader(interfaceBuf);
		if (!LoadInterface(reader))
			return false;
	}
	wxMemoryBuffer roleBuf = node.GetValue<wxMemoryBuffer>(wxT("Roles"));
	if (roleBuf.GetDataLen()) {
		ibReaderMemory reader(roleBuf);
		if (!LoadRole(reader))
			return false;
	}

	return ReadData(node);
}

// WriteData / ReadData — the PER-TYPE data hook, the ONLY per-type serialization.
// The base has no data of its own (a type with no extra data — e.g. Enum — needs no
// override). A type overrides to write/read its data as typed named values (props /
// fields / Child sub-nodes). There is no byte SaveData/LoadData anymore: the node IS
// the data, rendered to bytes / json / xml by the chosen provider.

bool ibValueMetaObject::ReadData(const ibDataNode& node)
{
	return true;
}

bool ibValueMetaObject::WriteData(ibDataNode& node) const
{
	return true;
}


// Build this node + descendants into the builder's tree. Metadata owns the
// lifecycle event (OnSaveMetaObject); SaveNode owns the field serialization.
bool ibValueMetaObject::BuildDataNode(ibDataNode& node, int flags)
{
	// The node's identity is THIS object's own — so a builder root fills itself from
	// any root object (configuration / data processor / report), with no hardcoded
	// clsid at the call site. Children already get theirs via AddChild below.
	node.SetClsid(GetClassType());
	node.SetMetaId(GetMetaID());

	const bool saveToFile = (flags & saveToFileFlag) != 0;

	if (!SaveNode(node))
		return false;
	if (!saveToFile && !OnSaveMetaObject(flags))
		return false;

	// children (filtered + non-deleted, recursive)
	for (unsigned int idx = 0; idx < GetChildCount(); idx++) {
		ibValueMetaObject* child = GetChild(idx);
		if (!FilterChild(child->GetClassType()))
			continue;
		if (child->IsDeleted())
			continue;
		ibDataNode& childNode = node.AddChild(child->GetClassType(), child->GetMetaID());
		if (!child->BuildDataNode(childNode, flags))
			return false;
	}
	return true;
}

// Apply the builder's (CONST) tree into this node + descendants. Children first
// (factory-create by clsid + recurse), then own data via LoadNode, then the load
// event (OnLoadMetaObject). Predefined children created in OnCreateMetaObject stay;
// we only APPEND the serialized ones.
bool ibValueMetaObject::ApplyDataNode(const ibDataNode& node, bool resetId)
{
	for (const ibDataNode& childNode : node.Children()) {
		ibValue* ppParams[] = { this };
		ibValueMetaObject* newMetaObject =
			ibValue::CreateAndConvertObjectRef<ibValueMetaObject>(childNode.GetClsid(), ppParams, 1);
		if (newMetaObject == nullptr)
			ibBackendCoreException::Error(
				_("Unknown metadata class id %lld while loading subtree of '%s'"),
				(long long)childNode.GetClsid(), GetName());
		newMetaObject->SetMetaData(m_metaData);
		newMetaObject->ApplyDataNode(childNode, resetId); // throws on error
	}

	if (!LoadNode(node))
		ibBackendCoreException::Error(_("Failed to load metadata fields for '%s'"), GetName());
	if (!OnLoadMetaObject(m_metaData))
		ibBackendCoreException::Error(_("OnLoadMetaObject failed for '%s'"), GetName());

	if (resetId)
		ResetId();
	return true;
}

bool ibValueMetaObject::DeleteSubtree()
{
	// Purge IsDeleted descendants depth-first. Reverse iteration so RemoveChild
	// (which erases from the owning vector → destroys the node) never shifts an
	// index still to be visited — the old forward loop could skip a sibling when
	// two adjacent children were both deleted.
	for (unsigned int idx = GetChildCount(); idx > 0; idx--) {
		ibValueMetaObject* child = GetChild(idx - 1);
		if (!FilterChild(child->GetClassType()))
			continue;

		const bool deleted = child->IsDeleted();
		if (!child->DeleteSubtree())
			return false;
		if (deleted) {
			RemoveChild(child); // owning handle drops the ref → node destroyed
		}
	}

	return true;
}

// Top-down (self before children): the phase hook fires on the node, then recurses. One pass per ibRunPhase.
bool ibValueMetaObject::RunSubtree(int flags, ibRunPhase phase)
{
	if (IsDeleted())
		return true;

	const bool ok =
		phase == ibRunPhase::Before ? OnBeforeRunMetaObject(flags)
		                            : OnAfterRunMetaObject(flags);
	if (!ok)
		return false;

	for (unsigned int idx = 0; idx < GetChildCount(); idx++) {
		ibValueMetaObject* child = GetChild(idx);
		if (!FilterChild(child->GetClassType()))
			continue;
		if (!child->RunSubtree(flags, phase))
			return false;
	}
	return true;
}

// Close is bottom-up (post-order): descendants close before this node's own hook,
// so the root closes last — matching the original container order. LIFO mirror of RunSubtree.
bool ibValueMetaObject::CloseSubtree(ibRunPhase phase)
{
	if (IsDeleted())
		return true;

	for (unsigned int idx = 0; idx < GetChildCount(); idx++) {
		ibValueMetaObject* child = GetChild(idx);
		if (!FilterChild(child->GetClassType()))
			continue;
		if (!child->CloseSubtree(phase))
			return false;
	}

	const bool ok =
		phase == ibRunPhase::Before ? OnBeforeCloseMetaObject()
		                            : OnAfterCloseMetaObject();
	if (!ok)
		return false;
	return true;
}
