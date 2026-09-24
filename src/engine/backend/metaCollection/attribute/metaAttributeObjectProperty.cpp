////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : meta-attribues
////////////////////////////////////////////////////////////////////////////

#include "metaAttributeObject.h"

#include "backend/metaData.h"
#include "backend/objCtor.h"

void ibValueMetaObjectAttribute::OnPropertyCreated(ibProperty* property)
{
	//if (m_propertyType == property) {
	/////	ibValueMetaObjectAttribute::SaveToVariant(m_propertyType->GetValue(), m_metaData);
	//}
}

void ibValueMetaObjectAttribute::OnPropertyRefresh()
{
	ibValueMetaObjectAttributeBase::OnPropertyRefresh();

	// SelectMode only means something for an attribute that points at ONE hierarchical
	// reference type: a multi-type attribute has no single hierarchy to choose in.
	bool selectable = false;
	if (GetClsidCount() == 1) {
		const ibCtorMetaValueType* so = GetMetaData()->GetTypeCtor(GetFirstClsid());
		selectable = so != nullptr
			&& so->GetMetaTypeCtor() == ibCtorObjectMetaType::ibCtorObjectMetaType_Reference
			&& dynamic_cast<const ibValueMetaObjectRecordDataHierarchyMutableRef*>(so->GetMetaObject()) != nullptr;
	}
	HideProperty(m_propertySelectMode, !selectable);

	// ItemMode is the OWNER's question — only a hierarchical owner has folders vs items.
	HideProperty(m_propertyItemMode,
		dynamic_cast<ibValueMetaObjectRecordDataHierarchyMutableRef*>(m_parent) == nullptr);

	// ⭐ CHOICE PARAMETERS NARROW A LIST, so a field that refers to nothing — a number, a date — has no
	// list for them to narrow. Hidden by the same rule as the neighbours above (Max, 2026-09-23, opening
	// the editor on a Quantity: "it is completely empty here").
	ibPropertyChoiceList targets;
	ibFieldReferenceTypes(this, targets);
	HideProperty(m_propertyChoiceParameters, targets.GetCount() == 0);

	// 🛑 AND THE LINK IS *NOT* HIDDEN THE SAME WAY, though it looks like the same rule. It was, for one
	// build: the row went away where no neighbour carried a type — and that is the state an author is in
	// exactly while they are building toward it. Fields are written in the order they are thought of,
	// usually the value before the kind, so the property vanished from the field that needs it and there
	// was nowhere to learn it exists (Max, 2026-09-23: "why has the link by type disappeared at all?").
	//
	// A property whose EXISTENCE depends on the order things were created in is worse than an empty one.
	// The refusal belongs where the choice is made: the window offers <not set> and says what a link
	// would decide, which is an answer a person can read.
}

bool ibValueMetaObjectAttribute::OnPropertyChanging(ibProperty* property, const wxVariant& newValue)
{
	//if (m_propertyType == property && !ibValueMetaObjectAttribute::LoadFromVariant(newValue))
	//	return false;
	return ibValueMetaObjectAttributeBase::OnPropertyChanging(property, newValue);
}

void ibValueMetaObjectAttribute::OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue)
{
	// ⭐ THE OWNER'S ROW IS DECIDED WHEN THE TYPE IS — the one moment at which it can be known, and the
	// one moment at which it is not an argument with the author. A field that has just been pointed at a
	// subordinate catalog is chosen within an owner; the row saying so is written now, and from here on
	// it is ordinary data the author owns, may edit and may delete for good.
	//
	// ⚠ Only into an EMPTY table, and only because the table is empty AT THIS INSTANT: the type has just
	// changed, so whatever rows stood there were about the previous type and the author is looking at a
	// field that has none.
	if (property == m_propertyType && !GetChoiceParameters().IsOk()) {
		ibChoiceParameterRowDescription ownerRow;
		if (ibChoiceOwnerRow(this, ownerRow)) {
			ibChoiceParametersDescription params;
			params.SetRow(ownerRow);
			m_propertyChoiceParameters->SetValue(params);
		}
	}

	ibValueMetaObject* metaObject = GetParent();
	wxASSERT(metaObject);

	if (metaObject->OnReloadMetaObject()) {
		ibValueMetaObject::OnPropertyChanged(property, oldValue, newValue);
	}
}