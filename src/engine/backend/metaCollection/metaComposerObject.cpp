////////////////////////////////////////////////////////////////////////////
//	Description : composer object
////////////////////////////////////////////////////////////////////////////

#include "metaComposerObject.h"
#include "backend/metaData.h"
#include "backend/serialize/dataBuilder.h"   // ibDataNode — per-type node data
#include "backend/metaCollection/partial/dataReport.h"   // the owner told about its default composer
#include "backend/backend_metatree.h"   // ibBackendMetadataTree — the header's choice hears about a new composer

ibValueMetaObjectComposer::ibValueMetaObjectComposer()
	: ibValueMetaObject()
{
	// The column face's answer, fixed at construction — a composer holds a composition and nothing
	// else, so nobody downstream has to ask what kind of composer it is.
	m_typeDesc.AppendMetaType(g_valueDataCompositionCLSID);

	// ⭐⭐ AND THIS METAOBJECT IS THE COMPOSITION'S STRUCTURAL OWNER. The composition RAISES the change
	// signal from every door that writes it, and the settings window raises it where an edit is made
	// — but a signal with nobody above it bubbles to null and dies,
	// which is why editing a composer left the configuration looking unmodified and Save with nothing
	// to do (Max, 2026-08-20). The attach owner is exactly the documented case for this: "set on
	// AttachPropertyObject or by a STRUCTURAL owner" (propertyObject.h).
	//
	// ⚠ Set HERE and nowhere else: at runtime a composition has no metaobject above it, so the same
	// value stays silent by construction rather than by a flag anybody has to remember to check.
	if (ibValueDataComposition* composition = GetComposition())
		composition->SetAttachOwner(this);
}

// ⭐ THE ANSWER TO THAT SIGNAL: the CONFIGURATION is modified. Which is where modified-ness lives
// (Max, 2026-08-20: "modified-ness is set on the metadata") — a metaobject does not carry a dirty
// flag of its own, the container does, and the designer's Save reads it there.
//
// No payload is needed and none is offered: "something below me changed" is the whole fact. The base
// keeps bubbling, so a composer nested under something that also cares stays heard.
void ibValueMetaObjectComposer::OnChildChanged()
{
	if (m_metaData != nullptr)
		m_metaData->Modify(true);

	ibValueMetaObject::OnChildChanged();
}

ibValueMetaObjectComposer::~ibValueMetaObjectComposer() = default;

//***********************************************************************
//*                           read & save events                        *
//***********************************************************************

// The owner hears about its composers the way it hears about its forms — the FIRST one declared
// becomes the default, and losing the default one clears it (see ibValueMetaObjectReport).
bool ibValueMetaObjectComposer::OnCreateMetaObject(ibMetaData* metaData, int flags)
{
	if (!ibValueMetaObject::OnCreateMetaObject(metaData, flags))
		return false;

	if ((flags & newObjectFlag) != 0) {
		if (auto* report = dynamic_cast<ibValueMetaObjectReport*>(m_parent))
			report->OnCreateComposerObject(this);

		// ⭐ AND THE HEADER'S CHOICE IS TOLD FROM HERE, exactly where the form metatype tells it
		// (ibValueMetaObjectForm::OnCreateMetaObject). Hanging it on the tree's create command
		// covers ONE road — paste, undo and the second tree each need their own copy of it, and
		// that is how a list of composers ends up stale in one window and fresh in another. The
		// metatype knows it was born; every road goes through here.
		if (metaData != nullptr) {
			if (ibBackendMetadataTree* metaTree = metaData->GetMetaTree())
				metaTree->UpdateChoiceSelection();
		}
	}

	return true;
}

bool ibValueMetaObjectComposer::OnDeleteMetaObject()
{
	if (auto* report = dynamic_cast<ibValueMetaObjectReport*>(m_parent))
		report->OnRemoveComposerObject(this);

	return ibValueMetaObject::OnDeleteMetaObject();
}

bool ibValueMetaObjectComposer::OnBeforeRunMetaObject(int flags)
{
	return ibValueMetaObject::OnBeforeRunMetaObject(flags);
}

// ⭐⭐ AND THE OWNER'S OBJECT LEARNS THAT ITS FIELDS CHANGED — the same road an ATTRIBUTE and a
// TABULAR SECTION take (ibValueMetaObjectAttributeBase / ibValueMetaObjectTable): a child that adds
// a field to the object asks the owner to re-prepare, and the owner rebuilds its whole value store.
//
// 🛑 THIS IS WHAT WAS MISSING (Max, 2026-08-20 — the assert on a dropped composer). A composition is
// a FIELD of the report object now, created in PrepareEmptyObject beside the attributes; a composer
// declared AFTER the object was prepared therefore had no field, and the first walk into it met
// `wxASSERT(it != m_listObjectValue.end())`. In a configuration it was masked — a config object is
// re-initialised on every CreateObjectValue — and showed itself on an EXTERNAL report, whose object
// is prepared ONCE when the container is opened.
bool ibValueMetaObjectComposer::OnAfterRunMetaObject(int flags)
{
	if ((flags & newObjectFlag) != 0 || (flags & pasteObjectFlag) != 0)
		OnReloadMetaObject();

	return ibValueMetaObject::OnAfterRunMetaObject(flags);
}

bool ibValueMetaObjectComposer::OnReloadMetaObject()
{
	ibValueMetaObject* metaObject = GetParent();
	wxASSERT(metaObject);
	if (metaObject->OnReloadMetaObject())
		return ibValueMetaObject::OnReloadMetaObject();

	return false;
}

bool ibValueMetaObjectComposer::OnAfterCloseMetaObject()
{
	return ibValueMetaObject::OnAfterCloseMetaObject();
}

//***********************************************************************
//*                             read & write                            *
//***********************************************************************

// ⭐ THE COMPOSITION SERIALISES ITSELF, WHOLE. Source, query, every variant and every parameter
// travel through ibValueDataComposition's own pair — the same one the form attribute uses — so a
// composer declared in the metadata and a composition held by a form are written by ONE set of
// rules. Spelling them out again here would be the second copy that drifts on the first change.
//
// 🛑⭐ THROUGH ITS PROPERTY, exactly as the TEMPLATE metatype does with its spreadsheet (Max,
// 2026-08-20: "do it the way the spreadsheet document does" — and before that: "why did the
// metaobject serialisation go down the non-metaobject road, when we separated those clearly?").
//
// The separation is ibDataNode's own: PROPERTIES are what the object IS, CHILDREN are the objects
// under it. What broke it was lending the metaobject's own node to a writer meant for a VALUE — a
// composition writes one sub-node per variant and per parameter, keyed by the LOOP INDEX, and those
// landed in the namespace where a (clsid, metaId) pair means an object's identity. The reader then
// met `CompositionVariant` where a metatype had to be, found no factory, and the file stopped
// opening for good.
//
// Held in a property, the whole thing is two symmetric lines and the mistake cannot recur: the
// composition writes inside its OWN node (ibPropertyComposition packs it as one value), and this
// metaobject's children stay metaobjects by construction.
bool ibValueMetaObjectComposer::ReadData(const ibDataNode& node)
{
	m_propertyComposition->SetNodeValue(node.GetProperty(m_propertyComposition->GetName()));
	return true;
}

bool ibValueMetaObjectComposer::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyComposition->GetName(), m_propertyComposition->GetNodeValue());
	return true;
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(ibValueMetaObjectComposer, "Composer", g_metaComposerCLSID);
