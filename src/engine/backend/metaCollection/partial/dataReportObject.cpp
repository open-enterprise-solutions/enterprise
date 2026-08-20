////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : report - object
////////////////////////////////////////////////////////////////////////////

#include "dataReport.h"
#include "backend/serialize/dataBuilder.h"   // ibDataNode — a composer's settings travel as a node

//*********************************************************************************************
//*                                  ObjectCatalogValue                                       *
//*********************************************************************************************

ibValueRecordDataObjectReport::ibValueRecordDataObjectReport(const ibValueMetaObjectReport* metaObject) : ibValueRecordDataObjectExt(metaObject)
{
	m_members.Bind(this, &ibValueRecordDataObjectReport::FillDataMembers);
}

ibValueRecordDataObjectReport::ibValueRecordDataObjectReport(const ibValueRecordDataObjectReport& source) : ibValueRecordDataObjectExt(source)
{
	m_members.Bind(this, &ibValueRecordDataObjectReport::FillDataMembers);
}

// ShowFormValue / GetFormValue inherited from ibValueRecordDataObject.
// GetCurrentObjectFormID is inline in dataReport.h.

//*********************************************************************************************
//*                                    Compositions                                           *
//*********************************************************************************************

// ⭐⭐ A COMPOSITION IS A FIELD OF THE OBJECT, exactly as a TABULAR SECTION is (Max, 2026-08-20:
// "the data composer lives IN the object, as a runtime value — it is created, initialised, the whole
// cycle, like a tabular section; and you went and set up a federation of your own").
//
// So it is created HERE, in the object's own filler, and lands in the ONE store every field lives in
// (m_listObjectValue). Everything else then comes for free and stays in step by construction:
// GetValueByMetaID finds it, the member surface publishes it, a re-read rebuilds it with the rest.
// A second map beside that store was a parallel lifecycle — filled lazily, cleared by nobody,
// surviving a re-read that resets every other field.
//
// ONE LIVE COMPOSITION PER DECLARED COMPOSER, seeded from the metaobject's own: the metaobject holds
// the DEFAULT of the user's settings, and the object gets its own copy so that running a report —
// changing a filter, switching a variant — never writes into the configuration. The seed travels
// through the composition's own node pair, so whatever a composition consists of today (and whatever
// is added later) is carried without this knowing what is inside.
void ibValueRecordDataObjectReport::PrepareEmptyObject()
{
	ibValueRecordDataObjectExt::PrepareEmptyObject();   // attributes + tabular sections, as for any object

	const auto* metaObject = dynamic_cast<const ibValueMetaObjectReport*>(GetMetaObject());
	if (metaObject == nullptr)
		return;

	for (auto* metaComposer : metaObject->GetComposerArrayObject()) {
		if (metaComposer == nullptr || metaComposer->IsDeleted())
			continue;

		ibValuePtr<ibValueDataComposition> composition(new ibValueDataComposition());
		if (ibValueDataComposition* declared = metaComposer->GetComposition()) {
			ibDataNode node;
			declared->WriteProperty(node);
			composition->ReadProperty(node);
		}
		m_listObjectValue.insert_or_assign(metaComposer->GetMetaID(), composition);
	}
}

// The composition standing at a composer's id — the twin of GetTableByMetaID, and the same one line:
// read the field, then say what it is.
ibValueDataComposition* ibValueRecordDataObjectReport::GetComposition(const ibMetaID& id) const
{
	ibValueDataComposition* composition = nullptr;
	return GetValueByMetaID(id).ConvertToValue(composition) ? composition : nullptr;
}

// ⚠ TWO NAMES, ONE VALUE. `Report.Composer` is the default composer itself, not a copy of it — a
// filter set through one name has to be there through the other.
ibValueDataComposition* ibValueRecordDataObjectReport::GetDefaultComposition() const
{
	const auto* metaObject = dynamic_cast<const ibValueMetaObjectReport*>(GetMetaObject());
	if (metaObject == nullptr)
		return nullptr;

	const ibMetaID defaultId = metaObject->GetDefComposer();
	return defaultId != wxNOT_FOUND ? GetComposition(defaultId) : nullptr;
}

//*********************************************************************************************
//*                              What the object publishes                                    *
//*********************************************************************************************

// The base publishes the attributes and the tabular sections; this ADDS the composers by name.
// They are read-only as properties — a composition is configured, never assigned over.
//
// ⚠ IT DOES NOT CALL THE BASE. Binding accumulates: every class in the chain registers its own
// filler and they all run, so calling the base here would publish each attribute and each tabular
// section TWICE.
void ibValueRecordDataObjectReport::FillDataMembers(ibMemberTable& helper) const
{
	const auto* metaObject = dynamic_cast<const ibValueMetaObjectReport*>(GetMetaObject());
	if (metaObject == nullptr)
		return;

	wxString objectName;
	for (auto* metaComposer : metaObject->GetComposerArrayObject()) {
		if (metaComposer == nullptr || metaComposer->IsDeleted())
			continue;
		if (!metaComposer->GetObjectNameAsString(objectName))
			continue;
		helper.AppendProp(objectName, true, false, metaComposer->GetMetaID(), eProperty);
	}
}

// ⭐ AND WHAT A FORM SEES. The base lists the object's attributes and tabular sections; a report
// adds its composers as nodes of their own, typed as compositions — that type is what the form
// builder turns into a GRIDBOX, so declaring a composer is what puts the report on the screen.
const ibSourceExplorer* ibValueRecordDataObjectReport::GetSourceExplorer() const
{
	const ibSourceExplorer* explorer = ibValueRecordDataObject::GetSourceExplorer();

	const auto* metaObject = dynamic_cast<const ibValueMetaObjectReport*>(GetMetaObject());
	if (metaObject == nullptr)
		return explorer;

	for (auto* metaComposer : metaObject->GetComposerArrayObject()) {
		if (metaComposer == nullptr || metaComposer->IsDeleted())
			continue;
		// ⭐ WITH ITS DESCRIPTOR, not just a name and a type. The node keeps the composer itself
		// (which IS an ibBackendSourceColumn), so the source walk returns it as the LEAF — which is
		// what makes the inspector show "DataComposition" instead of the owner's type, and what lets
		// a DRAG of the composer build a gridbox: the drop asks the walk for the leaf first and
		// created nothing when there was none (Max, 2026-08-20: "I drag it and drop does not work,
		// though I can point at it by hand").
		m_sourceExplorer.AppendColumn(metaComposer, metaComposer->GetMetaID());
	}

	return &m_sourceExplorer;
}

// The report's main node — its default composer. Stated here, asked by whoever binds to it.
bool ibValueRecordDataObjectReport::IsMainSourceNode(const ibMetaID& id) const
{
	const auto* metaObject = dynamic_cast<const ibValueMetaObjectReport*>(GetMetaObject());
	return metaObject != nullptr && metaObject->GetDefComposer() == id;
}

// (No GetValueByMetaID override: a composition IS a field of the object now, so the base finds it in
//  the same store as every other one.)

// A COMPOSITION IS NOT ASSIGNED OVER. It is a configured thing — its settings, its variants, the
// document it composed into — and replacing it wholesale would leave every control bound to the
// previous object. Refused here rather than half-done. (The same rule a tabular section follows: the
// field is filled, never swapped.)
bool ibValueRecordDataObjectReport::SetValueByMetaID(const ibMetaID& id, const ibValue& varMetaVal)
{
	if (GetComposition(id) != nullptr)
		return false;

	return ibValueRecordDataObject::SetValueByMetaID(id, varMetaVal);
}
