#include "metaCommonAttributeObject.h"

#include "backend/metaData.h"
#include "backend/metadataConfiguration.h"   // activeMetaData
#include "backend/serialize/dataBuilder.h"

//***********************************************************************
//*                     The declaration, under Common                   *
//***********************************************************************

// The context menus of both classes live in metaCommonAttributeObjectMenu.cpp.

std::vector<ibValueMetaObject*> ibValueMetaObjectCommonAttribute::GetCompositionArrayObject() const
{
	std::vector<ibValueMetaObject*> array;

	// THE COPIES ARE THE MEMBERSHIP. There is no flag anywhere saying "this object carries
	// me" — the copy inside the object says it, and it is the same thing that produces the
	// column. One fact, so nothing can disagree with itself.
	for (ibValueMetaObjectCommonAttributeColumn* ref : GetCommonAttributeColumnArrayObject()) {
		if (ref == nullptr)
			continue;
		if (ibValueMetaObject* const owner = ref->GetParent())
			array.emplace_back(owner);
	}

	return array;
}

bool ibValueMetaObjectCommonAttribute::IsCompositionObject(const ibValueMetaObject* metaObject) const
{
	if (metaObject == nullptr)
		return false;

	// THE COPY IS THE ANSWER, never the flag. Both were tried, and the flag lost twice: a
	// copy created before the flag existed read as "not carried" and a second one was
	// added; then a flag left behind by a copy that had gone read as "carried" and no copy
	// was ever created again — a checkbox ticked over an object with no such attribute.
	//
	// The flag remains as the composition mechanism's own record (ibCompositionObject: it
	// serialises, and other things may ask it), but it is a follower here. What an object
	// carries is what is inside it.
	return !FindCommonAttributeColumnIn(metaObject).empty();
}

bool ibValueMetaObjectCommonAttribute::SetCompositionObject(ibValueMetaObject* metaObject, bool set)
{
	if (metaObject == nullptr)
		return false;

	ibMetaData* const metaData = metaObject->GetMetaData() != nullptr
		? metaObject->GetMetaData() : activeMetaData;
	if (metaData == nullptr)
		return false;

	// TWO HALVES, ONE OPERATION. The object records that it is part of this composition
	// (its own mechanism, its own chunk on disk), and gains the attribute that membership
	// MEANS. Neither is a note about the other: the flag answers "am I in it" cheaply, the
	// copy is what becomes a column. They are only ever changed together, here.
	if (set) {
		if (IsCompositionObject(metaObject))
			return true;   // already carried — nothing to create

		ibValueMetaObject* const created =
			metaData->CreateMetaObject(g_metaCommonAttributeColumnCLSID, metaObject);
		if (created == nullptr)
			return false;

		if (auto* const ref = dynamic_cast<ibValueMetaObjectCommonAttributeColumn*>(created)) {
			ref->BindSource(this);
			// THE NAME IS SET, not answered on demand. ibValueMetaObject::GetName is not
			// virtual — the designer tree, the property grid and the metadata walkers read
			// it directly — so an override would be seen by almost nobody and the copy
			// would show up under its generated name. It is kept in step by OnRename below.
			ref->SetName(GetName());
			ref->SetSynonym(GetSynonym());
		}

		metaObject->SetComposition(GetMetaID(), true);
		return true;
	}

	// Checking out is the same operation backwards: the copy goes, and with it the fact
	// that this object carried anything.
	for (ibValueMetaObjectCommonAttributeColumn* ref : FindCommonAttributeColumnIn(metaObject)) {
		ref->AllowRemoval();   // sanctioned: it is this declaration taking its own copy back
		metaData->RemoveMetaObject(ref, metaObject);
	}

	metaObject->SetComposition(GetMetaID(), false);
	return true;
}

std::vector<ibValueMetaObjectCommonAttributeColumn*> ibValueMetaObjectCommonAttribute::FindCommonAttributeColumnIn(
	const ibValueMetaObject* metaObject) const
{
	std::vector<ibValueMetaObjectCommonAttributeColumn*> array;
	if (metaObject == nullptr)
		return array;

	for (ibValueMetaObjectCommonAttributeColumn* ref : GetCommonAttributeColumnArrayObject()) {
		if (ref != nullptr && ref->GetParent() == metaObject)
			array.emplace_back(ref);
	}

	return array;
}

std::vector<ibValueMetaObjectCommonAttributeColumn*> ibValueMetaObjectCommonAttribute::GetCommonAttributeColumnArrayObject() const
{
	std::vector<ibValueMetaObjectCommonAttributeColumn*> array;

	// const throughout — the sweep only READS the metadata, so it needs no writable handle
	// (GetAnyArrayObject is itself const).
	const ibMetaData* const metaData = GetMetaData() != nullptr ? GetMetaData() : activeMetaData;
	if (metaData == nullptr)
		return array;

	// The LIST form, not the single-clsid one: only this overload forwards
	// use_child_filter, and the copies live INSIDE objects — a top-level sweep sees none.
	for (auto* ref : metaData->GetAnyArrayObject<ibValueMetaObjectCommonAttributeColumn>(
			{ g_metaCommonAttributeColumnCLSID }, true)) {
		if (ref != nullptr && ref->GetSourceMetaID() == GetMetaID())
			array.emplace_back(ref);
	}

	return array;
}

void ibValueMetaObjectCommonAttribute::OnPropertyChanged(
	ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue)
{
	// A TYPE CHANGE REACHES EVERY CARRIER. The copies never held a type of their own —
	// they ask this declaration — so the value is already right the moment this returns.
	// What is NOT right until somebody says so is everything built ON the old answer: an
	// open form's attribute list, the editor's completion, a designer tree row that still
	// shows String where a reference now stands.
	//
	// The carriers are told the way an ordinary attribute tells its owner
	// (metaAttributeObjectProperty.cpp): OnReloadMetaObject on the object, which is the
	// existing "re-read yourself" door. One declaration edited, N objects re-read — which
	// is the whole cost of a name and a type living in one place.
	for (ibValueMetaObjectCommonAttributeColumn* ref : GetCommonAttributeColumnArrayObject()) {
		if (ref == nullptr)
			continue;
		if (ibValueMetaObject* const owner = ref->GetParent())
			owner->OnReloadMetaObject();
	}

	ibValueMetaObjectAttribute::OnPropertyChanged(property, oldValue, newValue);
}

bool ibValueMetaObjectCommonAttribute::OnRenameMetaObject(const wxString& sNewName)
{
	// The copies carry the declaration's name, so a rename here is a rename everywhere —
	// this is the propagation the stored name costs, and the only one.
	for (ibValueMetaObjectCommonAttributeColumn* ref : GetCommonAttributeColumnArrayObject()) {
		if (ref != nullptr)
			ref->SetName(sNewName);
	}

	return ibValueMetaObjectAttribute::OnRenameMetaObject(sNewName);
}

bool ibValueMetaObjectCommonAttribute::OnDeleteMetaObject()
{
	// THE COPIES GO WITH IT. They are not independent attributes that happen to share a
	// name — they exist only because this declaration put them there, and a copy whose
	// declaration is gone has no name and no type to answer with.
	// This event is non-const, so GetMetaData() already hands back a writable handle —
	// removal needs one.
	ibMetaData* const metaData = GetMetaData() != nullptr ? GetMetaData() : activeMetaData;

	if (metaData != nullptr) {
		for (ibValueMetaObjectCommonAttributeColumn* ref : GetCommonAttributeColumnArrayObject()) {
			ibValueMetaObject* const owner = ref->GetParent();
			if (owner != nullptr)
				owner->SetComposition(GetMetaID(), false);
			ref->AllowRemoval();   // sanctioned: the declaration itself is going away
			metaData->RemoveMetaObject(ref, owner);
		}
	}

	return ibValueMetaObjectAttribute::OnDeleteMetaObject();
}

//***********************************************************************
//*                   The copy that lives in an object                  *
//***********************************************************************

ibTypeDescription ibValueMetaObjectCommonAttributeColumn::s_emptyTypeDesc;

ibValueMetaObjectCommonAttributeColumn::ibValueMetaObjectCommonAttributeColumn(
	ibValueMetaObjectCommonAttribute* source)
	: ibValueMetaObjectAttribute(), m_source(source),
	  m_sourceMetaId(source != nullptr ? source->GetMetaID() : 0)
{
}

void ibValueMetaObjectCommonAttributeColumn::BindSource(ibValueMetaObjectCommonAttribute* source)
{
	m_source = source;
	m_sourceMetaId = source != nullptr ? source->GetMetaID() : 0;
}

ibTypeDescription& ibValueMetaObjectCommonAttributeColumn::GetTypeDesc() const
{
	// THE DECLARATION'S, not a copy of it. This is why a type change needs no propagation
	// pass: there was never a second answer to update.
	if (ibValueMetaObjectCommonAttribute* const src = GetSource())
		return src->GetTypeDesc();
	return s_emptyTypeDesc;
}

bool ibValueMetaObjectCommonAttributeColumn::OnDeleteMetaObject()
{
	// THE REFUSAL BELONGS HERE, and putting it one event earlier was a real bug worth
	// recording: OnBeforeCloseMetaObject is not "before delete", it is the CLOSE phase of
	// the metadata lifecycle (load / run / save / close, docs/metadata-lifecycle.md). It
	// runs for every metaobject when a configuration closes — so refusing there stopped the
	// configuration from closing at all, and the next RunDatabase asserted on
	// !IsConfigOpen().
	//
	// Delete has two legitimate sources, and neither is a person pressing Delete on the row:
	//   * the declaration taking its copy back (AllowRemoval was just called);
	//   * the owning object going away — it marks itself deleted first, then walks its
	//     children, and its columns go with it.
	ibValueMetaObject* const owner = GetParent();
	const bool ownerGoingAway = owner != nullptr && owner->IsDeleted();

	if (!m_removalAllowed && !ownerGoingAway)
		return false;

	return ibValueMetaObjectAttribute::OnDeleteMetaObject();
}

bool ibValueMetaObjectCommonAttributeColumn::OnLoadMetaObject(ibMetaData* metaData)
{
	// NOTHING BOUND HERE — see OnBeforeRunMetaObject. At load time the declaration may not
	// exist yet: the tree is walked in stored order, and an object can come before the
	// Common branch.
	return ibValueMetaObjectAttribute::OnLoadMetaObject(metaData);
}

bool ibValueMetaObjectCommonAttributeColumn::OnBeforeRunMetaObject(int flags)
{
	// THE BINDING, in the phase that can keep its promise: by now the whole configuration
	// is loaded, so the declaration this copy names either exists or does not, and the
	// answer will not change later.
	//
	// It matters more than a pointer: the copy has no type of its own — it answers the
	// declaration's — and the source explorer copies a node's type BY VALUE when it builds
	// (ibSourceInfo::m_typeDesc). An unbound copy therefore yields a column with an empty
	// type, and the form cannot build a control for it.
	if (!m_source && m_sourceMetaId != 0 && m_metaData != nullptr)
		m_source = m_metaData->FindAnyObjectByFilter<ibValueMetaObjectCommonAttribute>(
			m_sourceMetaId, g_metaCommonAttributeCLSID);

	return ibValueMetaObjectAttribute::OnBeforeRunMetaObject(flags);
}

bool ibValueMetaObjectCommonAttributeColumn::ReadData(const ibDataNode& node)
{
	m_sourceMetaId = node.GetProp<s32>(wxT("Source"));
	return true;
}

bool ibValueMetaObjectCommonAttributeColumn::WriteData(ibDataNode& node) const
{
	// ONLY THE LINK. Name, type and qualifiers are the declaration's, and writing a copy
	// of them here would be a second truth that a later edit could contradict.
	node.SetProp<s32>(wxT("Source"), m_sourceMetaId);
	return true;
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(ibValueMetaObjectCommonAttribute, "CommonAttribute", g_metaCommonAttributeCLSID);
METADATA_TYPE_REGISTER(ibValueMetaObjectCommonAttributeColumn, "CommonAttributeColumn", g_metaCommonAttributeColumnCLSID);
