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

#define metaBlock 0x200222
#define helpBlock 0x200224

bool ibValueMetaObject::LoadMeta(ibReaderMemory& dataReader)
{
	//Save meta version
	(void)dataReader.r_u32(); //reserved

	//Load unique guid
	wxString strGuid;
	dataReader.r_stringZ(strGuid);
	m_metaGuid = strGuid;

	//Load meta id
	m_metaId = dataReader.r_u32();

	//Load standart fields
	m_propertyName->LoadData(dataReader);
	m_propertySynonym->LoadData(dataReader);
	m_propertyComment->LoadData(dataReader);

	//special info deleted
	if (dataReader.r_u8()) {
		MarkAsDeleted();
	}

	//load interface 
	if (!LoadInterface(dataReader))
		return false;

	//load roles 
	if (!LoadRole(dataReader))
		return false;

	//load meta 
	wxMemoryBuffer meta_buffer;
	if (!dataReader.r_chunk(metaBlock, meta_buffer))
		return false;

	ibReaderMemory metaObjectReader(meta_buffer);
	metaObjectReader.r_u32(); //reserved flags
	if (!LoadData(metaObjectReader))
		return false;

	//load help 
	wxMemoryBuffer help_buffer;
	if (!dataReader.r_chunk(helpBlock, help_buffer))
		return false;
	
	ibReaderMemory helpReader(help_buffer);
	m_strHelpContent = helpReader.r_stringZ();
	return true;
}

bool ibValueMetaObject::SaveMeta(ibWriterMemory& dataWritter)
{
	//save meta version 
	dataWritter.w_u32(version_oes_last); //reserved 

	//save unique guid
	dataWritter.w_stringZ(m_metaGuid);

	//save meta id 
	dataWritter.w_u32(m_metaId);

	//save standart fields
	m_propertyName->SaveData(dataWritter);
	m_propertySynonym->SaveData(dataWritter);
	m_propertyComment->SaveData(dataWritter);

	//special info deleted
	dataWritter.w_u8(IsDeleted());

	//save interface 
	if (!SaveInterface(dataWritter))
		return false;

	//save roles 
	if (!SaveRole(dataWritter))
		return false;

	//save meta 
	ibWriterMemory metaObjectWritter;
	metaObjectWritter.w_u32(0); //reserved flags
	if (!SaveData(metaObjectWritter))
		return false;

	dataWritter.w_chunk(metaBlock, metaObjectWritter.buffer());

	//save help 
	ibWriterMemory helpWritter;
	helpWritter.w_stringZ(m_strHelpContent);
	dataWritter.w_chunk(helpBlock, helpWritter.buffer());
	return true;
}

bool ibValueMetaObject::LoadSubtree(ibReaderMemory& nodeReader, bool resetId)
{
	// NB: do NOT clear m_children here. The node's constructor (OnCreateMetaObject)
	// already populated it with the PREDEFINED attributes via
	// CreateMetaObjectAndSetParent (SetParent + AddChild). Those are not serialized
	// (FilterChild excludes them in SaveSubtree), so a blanket RemoveAllChildren
	// would orphan them (parent → null, dropped from m_children) and they'd never be
	// rebuilt — breaking GetFullName, the predefined finder, and the restructure
	// diff (predefined seen as removed+new → drop+re-add). We only append the
	// serialized children below.

	// children first (created via factory + recursive LoadSubtree), then own data.
	// Each child inherits this node's metadata at creation (this node's m_metaData
	// is always set — root by the container, children here), so no owner threading.
	std::shared_ptr <ibReaderMemory> readerChildMemory(nodeReader.open_chunk(ibMetaData::eChildBlock));
	if (readerChildMemory) {
		ibClassID clsid = 0;
		ibReaderMemory* prevReaderMemory = nullptr;
		while (!readerChildMemory->eof()) {
			ibReaderMemory* readerMemory = readerChildMemory->open_chunk_iterator(clsid, &*prevReaderMemory);
			if (!readerMemory)
				break;
			u64 meta_id = 0;
			ibReaderMemory* prevReaderMetaMemory = nullptr;
			while (!readerMemory->eof()) {
				ibReaderMemory* readerMetaMemory = readerMemory->open_chunk_iterator(meta_id, &*prevReaderMetaMemory);
				if (!readerMetaMemory)
					break;
				wxASSERT(clsid != 0);
				// AddChild (inside Init via ppParams[0]=this) takes the owning
				// reference; no explicit IncrRef here. A factory miss or a child
				// load failure throws ibBackendException — the half-built tree is
				// owned (ibValuePtr) and unwound by the caller; no catch(...) swallow.
				ibValue* ppParams[] = { this };
				ibValueMetaObject* newMetaObject =
					ibValue::CreateAndConvertObjectRef<ibValueMetaObject>(clsid, ppParams, 1);
				if (newMetaObject == nullptr)
					ibBackendCoreException::Error(
						_("Unknown metadata class id %lld while loading subtree of '%s'"),
						(long long)clsid, GetName());
				newMetaObject->SetMetaData(m_metaData);
				newMetaObject->LoadSubtree(*readerMetaMemory, resetId); // throws on error
				prevReaderMetaMemory = readerMetaMemory;
			}
			prevReaderMemory = readerMemory;
		}
	}

	// own data (folds the former LoadMetaObject: LoadMeta + OnLoadMetaObject).
	// m_metaData is already stamped by the caller (container for the root, parent
	// for children), so we no longer set it here.
	std::shared_ptr <ibReaderMemory> readerDataMemory(nodeReader.open_chunk(ibMetaData::eDataBlock));
	if (!readerDataMemory)
		ibBackendCoreException::Error(_("Missing data block while loading metadata '%s'"), GetName());
	if (!LoadMeta(*readerDataMemory))
		ibBackendCoreException::Error(_("Failed to load metadata fields for '%s'"), GetName());
	if (!OnLoadMetaObject(m_metaData))
		ibBackendCoreException::Error(_("OnLoadMetaObject failed for '%s'"), GetName());

	// Grafting a file's subtree into a config: the file's metaId would collide with the
	// existing config objects (same id → same ctor clsid → "Object is exist" on Run).
	// Regenerate it from this node's metadata counter (already stamped above). The root's
	// guid is reset separately by the importer (ResetAll) since its clsid keys on guid.
	if (resetId)
		ResetId();
	return true;
}

bool ibValueMetaObject::SaveSubtree(const ibClassID& chunkId, ibWriterMemory& writer, int flags)
{
	const bool saveToFile = (flags & saveToFileFlag) != 0;

	// own data (folds the former SaveMetaObject: SaveMeta + OnSaveMetaObject)
	ibWriterMemory writerDataMemory;
	if (!SaveMeta(writerDataMemory))
		return false;
	if (!saveToFile && !OnSaveMetaObject(flags))
		return false;

	ibWriterMemory writerMetaMemory;
	writerMetaMemory.w_chunk(ibMetaData::eDataBlock, writerDataMemory.pointer(), writerDataMemory.size());

	// children
	ibWriterMemory writerChildMemory;
	for (unsigned int idx = 0; idx < GetChildCount(); idx++) {
		ibValueMetaObject* child = GetChild(idx);
		if (!FilterChild(child->GetClassType()))
			continue;
		if (child->IsDeleted())
			continue;
		if (!child->SaveSubtree(child->GetClassType(), writerChildMemory, flags))
			return false;
	}
	writerMetaMemory.w_chunk(ibMetaData::eChildBlock, writerChildMemory.pointer(), writerChildMemory.size());

	ibWriterMemory writerMemory;
	writerMemory.w_chunk(GetMetaID(), writerMetaMemory.pointer(), writerMetaMemory.size());
	writer.w_chunk(chunkId, writerMemory.pointer(), writerMemory.size());
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
		if (deleted) {
			if (!child->DeleteData())
				return false;
		}
		if (!child->DeleteSubtree())
			return false;
		if (deleted) {
			RemoveChild(child); // owning handle drops the ref → node destroyed
		}
	}

	return true;
}

bool ibValueMetaObject::RunSubtree(int flags, bool before)
{
	if (IsDeleted())
		return true;

	if (before ? !OnBeforeRunMetaObject(flags) : !OnAfterRunMetaObject(flags))
		return false;

	for (unsigned int idx = 0; idx < GetChildCount(); idx++) {
		ibValueMetaObject* child = GetChild(idx);
		if (!FilterChild(child->GetClassType()))
			continue;
		if (!child->RunSubtree(flags, before))
			return false;
	}
	return true;
}

// Close is bottom-up (post-order): descendants close before this node's own hook,
// so the root closes last — matching the original container order.
bool ibValueMetaObject::CloseSubtree(bool before)
{
	if (IsDeleted())
		return true;

	for (unsigned int idx = 0; idx < GetChildCount(); idx++) {
		ibValueMetaObject* child = GetChild(idx);
		if (!FilterChild(child->GetClassType()))
			continue;
		if (!child->CloseSubtree(before))
			return false;
	}

	if (before ? !OnBeforeCloseMetaObject() : !OnAfterCloseMetaObject())
		return false;
	return true;
}
