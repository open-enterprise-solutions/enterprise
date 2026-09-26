////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : common classes for catalogs, docs etc..  
////////////////////////////////////////////////////////////////////////////

#include "commonObject.h"
#include "backend/metaData.h"   // ibMetaData::RegisterSource — the metaobject registers its source into its OWN config
#include "backend/srcDataObject.h"
#include "backend/system/systemManager.h"
#include "backend/objCtor.h"
#include "backend/session/session.h"
#include "backend/serialize/dataBuilder.h"   // node serialization (WriteData / ReadData)
#include "backend/choiceLinkResolver.h"      // what narrows a choice, put on the list before it is shown

#include "backend/metaCollection/partial/reference/reference.h"
#include "backend/metaCollection/partial/declaredPresentation.h"   // how a reference reads in the designer

//***********************************************************************
//*								 metaData                               *
//***********************************************************************

//***********************************************************************
//*					ibSourceDataObject — path walk                       *
//***********************************************************************

// One traversal shared by every source kind. The first id is read off the
// source itself (GetValueByMetaID, which each kind resolves its own way —
// RAM / list / object / record set / manager). Each further id steps into the
// previous reference value by attribute name: the source's metadata resolves the
// id to its name (config-wide, so a nested reference's field is found) and member
// access reads it off the value. Read-only navigation.



//***********************************************************************
//*							ibValueMetaObjectGenericData				    *
//***********************************************************************

#pragma region _form_builder_h_
ibBackendValueForm* ibValueMetaObjectGenericData::GetGenericForm(const ibFormRequest& request, ibBackendControlFrame* ownerControl) const
{
	return CreateAndBuildForm(request, defaultFormType, ownerControl, nullptr);
}
#pragma endregion
#pragma region _form_creator_h_
ibBackendValueForm* ibValueMetaObjectGenericData::CreateObjectForm(const ibValueMetaObjectFormBase* metaForm, const ibUniqueKey& formGuid) const
{
	// ⭐⭐ ONE PLACE MAKES A FORM'S SOURCE, and it is asked by the KIND of form — which is what it
	// switched on all along (`metaObject->GetTypeForm()`), so handing it the metaform was handing it a
	// question it then asked itself. Saying the kind outright is what lets the SELECT getters come
	// through here too, instead of each spelling the same `ibCreateHierarchyList(...)` a second time
	// in the same file (Max, 2026-09-23: "you gave it already — you have to change it all the way").
	const ibFormID form_id = metaForm != nullptr ? metaForm->GetTypeForm() : defaultFormType;

	const ibSourcePtr<ibSourceDataObject> source = CreateSourceObject(ibCreateRequest(), form_id);   // held across the build
	return CreateAndBuildForm(
		ibFormRequest(metaForm != nullptr ? metaForm->GetName() : wxString(), formGuid),
		form_id,
		nullptr,
		source
	);
}

ibSourcePtr<ibSourceDataObject> ibValueMetaObjectGenericData::CreateSourceObject(const ibCreateRequest& request, const ibFormID& form_id) const
{
	return nullptr;
}

ibBackendValueForm* ibValueMetaObjectGenericData::CreateAndBuildForm(const ibFormRequest& request, const ibFormID& form_id, ibBackendControlFrame* ownerControl, ibSourceDataObject* srcObject) const
{
	const ibSourcePtr<ibSourceDataObject> sourceGuard(srcObject);   // held across the build

	ibValueMetaObjectFormBase* creator = nullptr;

	if (!request.m_formName.IsEmpty()) {

		creator = FindFormObjectByFilter(request.m_formName, form_id);

		if (creator == nullptr) {
			ibBackendCoreException::Error(_("Form not found '%s'"), request.m_formName);
			return nullptr;
		}
	}

	if (!AccessRight_Show()) {
		ibBackendAccessException::Error(wxString::Format(_("opening '%s'"), GetSynonym()));
		return nullptr;
	}

	ibBackendValueForm* result = ibBackendValueForm::FindFormByUniqueKey(ownerControl, srcObject, request.m_formGuid);

	if (result == nullptr) {

		result = ibValueMetaObjectFormBase::CreateAndBuildForm(
			request,
			creator != nullptr ? creator : GetDefaultFormByID(form_id),
			form_id,
			ownerControl, srcObject
		);
	}

	return result;
}

#pragma endregion
#pragma region _template_builder_h_

ibValueSpreadsheetDocument* ibValueMetaObjectGenericData::GetTemplate(const wxString& strTemplateName) const
{
	ibValueMetaObjectSpreadsheetBase* creator = nullptr;

	if (!strTemplateName.IsEmpty()) {

		creator = FindTemplateObjectByFilter(strTemplateName);

		if (creator == nullptr) {
			ibBackendCoreException::Error(_("Template not found '%s'"), strTemplateName);
			return nullptr;
		}

		ibValueSpreadsheetDocument* valueSpreadsheetDocument =
			new ibValueSpreadsheetDocument(creator->GetSpreadsheetDesc());

		valueSpreadsheetDocument->InvalidateNames();
		return valueSpreadsheetDocument;
	}

	ibBackendCoreException::Error(_("Template not found!"));
	return nullptr;
}

#pragma endregion

//***********************************************************************
//*                           ibValueMetaObjectRecordData				*
//***********************************************************************

#include "backend/fileSystem/fs.h"

//***********************************************************************
//*                           read & save events                        *
//***********************************************************************

bool ibValueMetaObjectRecordData::OnLoadMetaObject(ibMetaData* metaData)
{
	return ibValueMetaObject::OnLoadMetaObject(metaData);
}

bool ibValueMetaObjectRecordData::OnSaveMetaObject(int flags)
{
	return ibValueMetaObject::OnSaveMetaObject(flags);
}

bool ibValueMetaObjectRecordData::OnDeleteMetaObject()
{
	return ibValueMetaObject::OnDeleteMetaObject();
}

bool ibValueMetaObjectRecordData::OnBeforeRunMetaObject(int flags)
{
	return ibValueMetaObject::OnBeforeRunMetaObject(flags);
}

bool ibValueMetaObjectRecordData::OnAfterCloseMetaObject()
{
	return ibValueMetaObject::OnAfterCloseMetaObject();
}

namespace {

// THE ORDER EVERY METATYPE FALLS BACK ON — its rows' own IDENTITY. Both sides belong to one metatype by
// the time it is asked (the reference checks), so the guid alone tells them apart, and two equal guids
// ARE the same row.
//
// ⚠ FILE-LOCAL, not a member: it needs nothing but what a data object already hands out, and every
// caller is in this file. A protected helper on the base would have put it in a WIDE header for three
// call sites that never leave here.
int ibCompareByIdentity(const ibValueDataObject* lhs, const ibValueDataObject* rhs)
{
	if (lhs == nullptr || rhs == nullptr)
		return 0;
	// THE GUID AS IT LIES — the raw guid of the key each side hands out by reference. When GetGuid built
	// an ibUniqueKey BY VALUE, this asked for four of them per comparison, on the path every sort of read
	// references takes (the payroll sheet's sort before output: 15 s -> 30 s, MEASURED 2026-09-12, Debug).
	const ibGuid& l = lhs->GetGuid().GetGuid();
	const ibGuid& r = rhs->GetGuid().GetGuid();
	if (l < r) return -1;
	if (r < l) return 1;
	return 0;   // the same row
}

} // namespace

//***********************************************************************
//*						ibValueMetaObjectRecordDataExt					*
//***********************************************************************

ibValueMetaObjectRecordDataExt::ibValueMetaObjectRecordDataExt() :
	ibValueMetaObjectRecordData()
{
}

// ⭐⭐ Every creator below holds what it made BEFORE InitializeObject: initializing runs the object's
// module, and the module may take `ThisObject` into a value and let it go again — on a raw pointer at
// refcount 0 that release deleted the object in the middle of its own initialization (issue #154).
// A refused initialization lets the holder go, and the object with it.

ibValuePtr<ibValueRecordDataObjectExt> ibValueMetaObjectRecordDataExt::CreateObjectValue() const
{
	const ibValuePtr<ibValueRecordDataObjectExt> created(CreateObjectExtValue());
	if (created != nullptr && !IsExternalCreate() && !created->InitializeObject())
		return nullptr;
	return created;
}

ibValuePtr<ibValueRecordDataObjectExt> ibValueMetaObjectRecordDataExt::CreateObjectValue(ibValueRecordDataObjectExt* objSrc) const
{
	const ibValuePtr<ibValueRecordDataObjectExt> created(CreateObjectExtValue());
	if (created != nullptr && !IsExternalCreate() && !created->InitializeObject(objSrc))
		return nullptr;
	return created;
}

ibValuePtr<ibValueRecordDataObject> ibValueMetaObjectRecordDataExt::CreateRecordDataObjectValue() const
{
	return CreateObjectValue();
}

//***********************************************************************
//*                           read & save events                        *
//***********************************************************************

bool ibValueMetaObjectRecordDataExt::OnBeforeRunMetaObject(int flags)
{
	if (IsExternalCreate()) {
		registerExternalObject();
		registerExternalManager();
	}
	else {
		registerObject();
		registerManager();
	}

	return ibValueMetaObjectRecordData::OnBeforeRunMetaObject(flags);
}

bool ibValueMetaObjectRecordDataExt::OnAfterCloseMetaObject()
{
	// MIRROR OF THE RUN BRANCH ABOVE. A ctor is dropped by the identity it was filed under, and
	// the external pair is filed under a different KIND (externalObject/externalManager), so the
	// ordinary ids find nothing — UnRegisterCtor then raises "Object with id '…' is not exist",
	// out of CloseSubtree, and the external processor never finishes closing.
	// This stayed invisible while the macros keyed on the NAME: the external ctors inherit
	// GetClassName() from their non-external bases, so both spellings resolved to the same entry.
	if (IsExternalCreate()) {
		unregisterExternalObject();
		unregisterExternalManager();
	}
	else {
		unregisterObject();
		unregisterManager();
	}

	return ibValueMetaObjectRecordData::OnAfterCloseMetaObject();
}

//***********************************************************************
//*						ibValueMetaObjectRecordDataRef					*
//***********************************************************************

ibValueMetaObjectRecordDataRef::ibValueMetaObjectRecordDataRef() : ibValueMetaObjectRecordData()
{
}

ibValueMetaObjectRecordDataRef::~ibValueMetaObjectRecordDataRef()
{
	//wxDELETE((*m_propertyAttributeReference));
}

//***************************************************************************
//*                       Save & load metaData                              *
//***************************************************************************

// Node form. Base ibValueMetaObjectRecordData has no data of its own, so the chain
// bottoms out HERE — no base WriteData call (it would hit the $data bridge).

bool ibValueMetaObjectRecordDataRef::ReadData(const ibDataNode& node)
{
	m_propertyQuickChoice->SetNodeValue(node.GetProperty(m_propertyQuickChoice->GetName()));

	// ⭐⭐ THE NAME A PROPERTY IS STORED UNDER IS ITS KEY, NOT ITS LABEL.
	//
	// This field is called `Ref` now — one spelling for what a script writes, a query names and the
	// field tree shows. But every configuration saved before that keeps the node under the OLD word,
	// and a load that only asks for the new one finds nothing: the attribute stays in its default
	// state, metaID and all, and the first symptom is a query against a column called `fld0_RRRef`
	// that no table has. Two floors from the rename, in the server's words.
	//
	// So the old name is still READ and only the new one is WRITTEN: opening a configuration
	// migrates it, saving it settles the migration, and nobody is asked to do anything.
	ibDataValue reference = node.GetProperty(m_propertyAttributeReference->GetName());
	if (reference.IsEmpty())
		reference = node.GetProperty(wxT("Reference"));   // saved before the rename
	m_propertyAttributeReference->SetNodeValue(reference);
	return true;
}

bool ibValueMetaObjectRecordDataRef::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyQuickChoice->GetName(), m_propertyQuickChoice->GetNodeValue());
	node.SetProperty(m_propertyAttributeReference->GetName(), m_propertyAttributeReference->GetNodeValue());
	return true;
}

//***********************************************************************
//*                           read & save events                        *
//***********************************************************************

bool ibValueMetaObjectRecordDataRef::OnCreateMetaObject(ibMetaData* metaData, int flags)
{
	if (!ibValueMetaObjectRecordData::OnCreateMetaObject(metaData, flags))
		return false;

	return (*m_propertyAttributeReference)->OnCreateMetaObject(metaData, flags);
}

#include "backend/appData.h"
#include "backend/logger/logger.h"
#include "databaseLayer/databaseLayer.h"

bool ibValueMetaObjectRecordDataRef::OnLoadMetaObject(ibMetaData* metaData)
{
	if (!(*m_propertyAttributeReference)->OnLoadMetaObject(metaData))
		return false;

	return ibValueMetaObjectRecordData::OnLoadMetaObject(metaData);
}

bool ibValueMetaObjectRecordDataRef::OnSaveMetaObject(int flags)
{
	if (!(*m_propertyAttributeReference)->OnSaveMetaObject(flags))
		return false;

	return ibValueMetaObjectRecordData::OnSaveMetaObject(flags);
}

bool ibValueMetaObjectRecordDataRef::OnDeleteMetaObject()
{
	if (!(*m_propertyAttributeReference)->OnDeleteMetaObject())
		return false;

	return ibValueMetaObjectRecordData::OnDeleteMetaObject();
}

bool ibValueMetaObjectRecordDataRef::OnBeforeRunMetaObject(int flags)
{
	if (!(*m_propertyAttributeReference)->OnBeforeRunMetaObject(flags))
		return false;

	registerReference();
	registerManager();

	const ibCtorMetaValueType* typeCtor = m_metaData->GetTypeCtor(this, ibCtorObjectMetaType::ibCtorObjectMetaType_Reference);
	if (typeCtor != nullptr)
		(*m_propertyAttributeReference)->SetDefaultMetaType(typeCtor->GetClassType());

	return ibValueMetaObjectRecordData::OnBeforeRunMetaObject(flags);
}

bool ibValueMetaObjectRecordDataRef::OnAfterRunMetaObject(int flags)
{
	if (!ibValueMetaObjectRecordData::OnAfterRunMetaObject(flags))
		return false;
	// Register this record (catalog / document / charts / enum — subtypes chain up here) as
	// an L4 query source: it OWNS its descriptor field m_sourceDescriptor. Register ALWAYS — the factory
	// lives PER-CONFIG in the metadata (not the old global singleton), so EVERY config, incl. a read-only DB
	// load (onlyLoadFlag), must register its OWN sources into its OWN factory or its forms can't resolve them.
	// (NOTHING IS REGISTERED HERE. "A reference" is a base to mix into, not a readable source — the
	//  KINDS below have the queryables: an enumeration, a hierarchy, a recorder. Each registers its
	//  own in its own OnAfterRun, which is also where it can be typed to itself.)
	return true;
}

bool ibValueMetaObjectRecordDataRef::OnBeforeCloseMetaObject()   // un-resolve — mirror of OnRun's registration
{
	return ibValueMetaObjectRecordData::OnBeforeCloseMetaObject();
}

bool ibValueMetaObjectRecordDataRef::OnAfterCloseMetaObject()
{
	if (!(*m_propertyAttributeReference)->OnAfterCloseMetaObject())
		return false;

	if (m_propertyAttributeReference != nullptr)
		(*m_propertyAttributeReference)->SetDefaultMetaType(ibValueTypes::TYPE_EMPTY);

	unregisterReference();
	unregisterManager();

	return ibValueMetaObjectRecordData::OnAfterCloseMetaObject();
}

//***********************************************************************
//*                             form events                             *
//***********************************************************************

//process choice 
bool ibValueMetaObjectRecordDataRef::ProcessChoice(ibBackendControlFrame* ownerValue, const ibFormRequest& request) const
{
	// ⭐⭐ THE REQUEST GOES TO THE MAKING, and opening stays what it always was. Getting the form is
	// where its SOURCE OBJECT is created, and a list reads its settings exactly once — when it is
	// built. Handing a narrowing to a form that already exists rewrites a description the composer
	// was made from and that nothing reads again (Max, 2026-09-23: "pass these parameters as you
	// create the form in memory, and the opening stays as it is, empty").
	//
	// Nobody keeps it either: it is an argument on the way in, and what it asks for has happened by
	// the time the call returns.
	//
	// A flat list has one select form, so the mode says nothing here — what may be picked is a question
	// its hierarchical sibling below answers, because only there is there more than one kind of row.
	ibBackendValueForm* const selectChoiceForm = GetSelectForm(request, ownerValue);
	if (selectChoiceForm == nullptr)
		return false;

	selectChoiceForm->ShowForm();
	return true;
}

ibValueReferenceDataObject* ibValueMetaObjectRecordDataRef::FindObjectValue(const ibGuid& objGuid) const
{
	if (!objGuid.isValid())
		return nullptr;
	return ibValueReferenceDataObject::Create(this, objGuid);
}

// A VALUE SAYS WHAT ITS KIND'S TEMPLATE SAYS — its fields asked of the value in hand. A field it cannot
// answer for is `false`: nothing to show, which is not the same as showing nothing.
//
// ⭐ ONE VALUE FOR THE FIELDS, THEIR TEXTS GLUED INTO ONE STRING. Each field is written into the same value
// in turn — a string written onto a string keeps its buffer (ibValue::Copy) — and its text is taken from
// there as it lies (GetString(scratch)): only a field that is not text yet, a number or a date, is made
// into the scratch. Each text is laid down before the next field is written over it.
bool ibValueMetaObjectRecordDataRef::GenerateDataDesc(const ibValueDataObject* objValue, wxString& out) const
{
	if (objValue == nullptr)
		return false;
	ibDataDescParameter parameter;
	if (!GenerateDataDesc(parameter) || parameter.m_first == nullptr)
		return false;
	ibValue field;
	if (!objValue->GetValueByMetaID(parameter.m_first->GetMetaID(), field))
		return false;
	const ibString& first = field.GetString();
	out.reserve(parameter.m_prefix.length() + first.Len() + parameter.m_separator.length());
	out.assign(parameter.m_prefix).append(first.wc_str(), first.Len());
	if (parameter.m_second == nullptr)
		return true;
	if (!objValue->GetValueByMetaID(parameter.m_second->GetMetaID(), field)) {
		out.clear();
		return false;
	}
	const ibString& second = field.GetString();
	out.append(parameter.m_separator).append(second.wc_str(), second.Len());
	return true;
}

//***********************************************************************
//*						ibValueMetaObjectRecordDataEnumRef					*
//***********************************************************************

///////////////////////////////////////////////////////////////////////////////////////////////

ibValueMetaObjectRecordDataEnumRef::ibValueMetaObjectRecordDataEnumRef() : ibValueMetaObjectRecordDataRef()
{
}

ibValueMetaObjectRecordDataEnumRef::~ibValueMetaObjectRecordDataEnumRef()
{
	//wxDELETE((*m_propertyAttributeOrder));
}

//***************************************************************************
//*                       Save & load metaData                              *
//***************************************************************************

bool ibValueMetaObjectRecordDataEnumRef::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyAttributeOrder->GetName(), m_propertyAttributeOrder->GetNodeValue());
	return ibValueMetaObjectRecordDataRef::WriteData(node);
}

bool ibValueMetaObjectRecordDataEnumRef::ReadData(const ibDataNode& node)
{
	m_propertyAttributeOrder->SetNodeValue(node.GetProperty(m_propertyAttributeOrder->GetName()));
	return ibValueMetaObjectRecordDataRef::ReadData(node);
}

//***********************************************************************
//*                           read & save events                        *
//***********************************************************************

bool ibValueMetaObjectRecordDataEnumRef::OnCreateMetaObject(ibMetaData* metaData, int flags)
{
	if (!ibValueMetaObjectRecordDataRef::OnCreateMetaObject(metaData, flags))
		return false;

	return (*m_propertyAttributeOrder)->OnCreateMetaObject(metaData, flags);
}

bool ibValueMetaObjectRecordDataEnumRef::OnLoadMetaObject(ibMetaData* metaData)
{
	if (!(*m_propertyAttributeOrder)->OnLoadMetaObject(metaData))
		return false;

	return ibValueMetaObjectRecordDataRef::OnLoadMetaObject(metaData);
}

bool ibValueMetaObjectRecordDataEnumRef::OnSaveMetaObject(int flags)
{
	if (!(*m_propertyAttributeOrder)->OnSaveMetaObject(flags))
		return false;

	return ibValueMetaObjectRecordDataRef::OnSaveMetaObject(flags);
}

bool ibValueMetaObjectRecordDataEnumRef::OnDeleteMetaObject()
{
	if (!(*m_propertyAttributeOrder)->OnDeleteMetaObject())
		return false;

	return ibValueMetaObjectRecordDataRef::OnDeleteMetaObject();
}

bool ibValueMetaObjectRecordDataEnumRef::OnBeforeRunMetaObject(int flags)
{
	if (!(*m_propertyAttributeOrder)->OnBeforeRunMetaObject(flags))
		return false;

	return ibValueMetaObjectRecordDataRef::OnBeforeRunMetaObject(flags);
}

bool ibValueMetaObjectRecordDataEnumRef::OnAfterRunMetaObject(int flags)
{
	// THE KIND REGISTERS ITS OWN SOURCE. An enumeration reads through a queryable typed to this kind,
	// and registering it here is what makes a query resolve `Enum.<name>` through that one and no other.
	m_metaData->RegisterSource(&m_queryable);
	return ibValueMetaObjectRecordDataRef::OnAfterRunMetaObject(flags);
}

bool ibValueMetaObjectRecordDataEnumRef::OnBeforeCloseMetaObject()
{
	m_metaData->UnregisterSource(&m_queryable);
	return ibValueMetaObjectRecordDataRef::OnBeforeCloseMetaObject();
}

bool ibValueMetaObjectRecordDataEnumRef::OnAfterCloseMetaObject()
{
	if (!(*m_propertyAttributeOrder)->OnAfterCloseMetaObject())
		return false;

	return ibValueMetaObjectRecordDataRef::OnAfterCloseMetaObject();
}

// ⭐ AN ENUMERATION MEMBER IS DECLARED, NOT STORED — in the designer it reads as it is written,
// `EnumRef.Kinds.Retail`, and at run time as the synonym a person put on it. A guid that names no
// member is `false`: there is nothing to read, which is not the same as reading as nothing.
bool ibValueMetaObjectRecordDataEnumRef::GenerateDataDesc(const ibValueDataObject* objValue,
	wxString& out) const
{
	if (objValue == nullptr)
		return false;

	const bool designer = appData->DesignerMode();
	if (designer) {
		const wxString empty = ibDeclaredEmptyRef(this, objValue->GetGuid());
		if (!empty.IsEmpty()) {
			out = empty;
			return true;
		}
	}

	for (auto obj : GetEnumObjectArray()) {
		if (obj != nullptr && objValue->GetGuid() == obj->GetGuid()) {
			out = designer
				? ibDeclaredTypeName(this) + wxT(".") + obj->GetName()
				: obj->GetSynonym();
			return true;
		}
	}
	return false;
}

// …AND ITS ORDER, WHICH IS THE ONE THE AUTHOR DECLARED. Read off the predefined `Order` attribute — the
// very field the enumeration's own list is built with (`ibCreateList(GetQueryable(), GetDataOrder())`) —
// so a sorted column, a grouping and that list all agree without anyone restating the sequence.
//
// ⚠ NO READ HAPPENS HERE. The caller (ibValueReferenceDataObject::CompareValueLS) asks only when both
// rows are already in hand; a value it cannot find falls through to identity rather than fetching one.
int ibValueMetaObjectRecordDataEnumRef::CompareDataValues(const ibValueDataObject* lhs,
	const ibValueDataObject* rhs) const
{
	const ibValueMetaObjectAttributePredefined* const order = GetDataOrder();
	if (lhs == nullptr || rhs == nullptr || order == nullptr)
		return ibCompareByIdentity(lhs, rhs);
	ibValue here, there;
	if (!lhs->GetValueByMetaID(order->GetMetaID(), here)
		|| !rhs->GetValueByMetaID(order->GetMetaID(), there))
		return ibCompareByIdentity(lhs, rhs);
	// Equal order is not "the same member" — two members declared at one position is not a thing — so the
	// identity step settles it, and it is the only one allowed to end at 0.
	const int byOrder = here.CompareValueLS(there);
	return byOrder != 0 ? byOrder : ibCompareByIdentity(lhs, rhs);
}


//***********************************************************************
//*						ibValueMetaObjectRecordDataMutableRef					*
//***********************************************************************

ibValueMetaObjectRecordDataMutableRef::ibValueMetaObjectRecordDataMutableRef() : ibValueMetaObjectRecordDataRef()
{
	// m_propertyObjectModule is declared on the leaf metaobjects
	// (Catalog / Document / ChartOf*) and isn't visible from this
	// base ctor — common-default SetDefaultProcedure registration
	// stays in each leaf's own ctor where the field is in scope.
}

ibValueMetaObjectRecordDataMutableRef::~ibValueMetaObjectRecordDataMutableRef()
{
	//wxDELETE((*m_propertyAttributeDataVersion);
	//wxDELETE((*m_propertyAttributeDeletionMark));
}

//***************************************************************************
//*                       Save & load metaData                              *
//***************************************************************************

bool ibValueMetaObjectRecordDataMutableRef::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyAttributeDataVersion->GetName(), m_propertyAttributeDataVersion->GetNodeValue());
	node.SetProperty(m_propertyAttributeDeletionMark->GetName(), m_propertyAttributeDeletionMark->GetNodeValue());
	if (!ibValueMetaObjectRecordDataRef::WriteData(node))
		return false;
	node.SetProperty(m_propertyGeneration->GetName(), m_propertyGeneration->GetNodeValue());
	return true;
}

bool ibValueMetaObjectRecordDataMutableRef::ReadData(const ibDataNode& node)
{
	m_propertyAttributeDataVersion->SetNodeValue(node.GetProperty(m_propertyAttributeDataVersion->GetName()));
	m_propertyAttributeDeletionMark->SetNodeValue(node.GetProperty(m_propertyAttributeDeletionMark->GetName()));
	if (!ibValueMetaObjectRecordDataRef::ReadData(node))
		return false;
	m_propertyGeneration->SetNodeValue(node.GetProperty(m_propertyGeneration->GetName()));
	return true;
}

//***********************************************************************
//*                           read & save events                        *
//***********************************************************************

bool ibValueMetaObjectRecordDataMutableRef::OnCreateMetaObject(ibMetaData* metaData, int flags)
{
	if (!ibValueMetaObjectRecordDataRef::OnCreateMetaObject(metaData, flags))
		return false;

	return (*m_propertyAttributeDataVersion)->OnCreateMetaObject(metaData, flags)
		&& (*m_propertyAttributeDeletionMark)->OnCreateMetaObject(metaData, flags);
}

bool ibValueMetaObjectRecordDataMutableRef::OnLoadMetaObject(ibMetaData* metaData)
{
	if (!(*m_propertyAttributeDataVersion)->OnLoadMetaObject(metaData))
		return false;

	if (!(*m_propertyAttributeDeletionMark)->OnLoadMetaObject(metaData))
		return false;

	return ibValueMetaObjectRecordDataRef::OnLoadMetaObject(metaData);
}

bool ibValueMetaObjectRecordDataMutableRef::OnSaveMetaObject(int flags)
{
	if (!(*m_propertyAttributeDataVersion)->OnSaveMetaObject(flags))
		return false;

	if (!(*m_propertyAttributeDeletionMark)->OnSaveMetaObject(flags))
		return false;

	return ibValueMetaObjectRecordDataRef::OnSaveMetaObject(flags);
}

bool ibValueMetaObjectRecordDataMutableRef::OnDeleteMetaObject()
{
	if (!(*m_propertyAttributeDataVersion)->OnDeleteMetaObject())
		return false;

	if (!(*m_propertyAttributeDeletionMark)->OnDeleteMetaObject())
		return false;

	return ibValueMetaObjectRecordDataRef::OnDeleteMetaObject();
}

bool ibValueMetaObjectRecordDataMutableRef::OnBeforeRunMetaObject(int flags)
{
	if (!(*m_propertyAttributeDataVersion)->OnBeforeRunMetaObject(flags))
		return false;

	if (!(*m_propertyAttributeDeletionMark)->OnBeforeRunMetaObject(flags))
		return false;

	registerObject();
	return ibValueMetaObjectRecordDataRef::OnBeforeRunMetaObject(flags);
}

bool ibValueMetaObjectRecordDataMutableRef::OnAfterRunMetaObject(int flags)
{
	if (!(*m_propertyAttributeDataVersion)->OnAfterRunMetaObject(flags))
		return false;

	if (!(*m_propertyAttributeDeletionMark)->OnAfterRunMetaObject(flags))
		return false;

	return ibValueMetaObjectRecordDataRef::OnAfterRunMetaObject(flags);
}

bool ibValueMetaObjectRecordDataMutableRef::OnBeforeCloseMetaObject()
{
	if (!(*m_propertyAttributeDataVersion)->OnBeforeCloseMetaObject())
		return false;

	if (!(*m_propertyAttributeDeletionMark)->OnBeforeCloseMetaObject())
		return false;

	return ibValueMetaObjectRecordDataRef::OnBeforeCloseMetaObject();
}

bool ibValueMetaObjectRecordDataMutableRef::OnAfterCloseMetaObject()
{
	if (!(*m_propertyAttributeDataVersion)->OnAfterCloseMetaObject())
		return false;

	if (!(*m_propertyAttributeDeletionMark)->OnAfterCloseMetaObject())
		return false;

	unregisterObject();
	return ibValueMetaObjectRecordDataRef::OnAfterCloseMetaObject();
}

//***************************************************************************
//*        ibValueMetaObjectRecordDataRecorderRef — records movements        *
//***************************************************************************

ibValueMetaObjectRecordDataRecorderRef::ibValueMetaObjectRecordDataRecorderRef()
	: ibValueMetaObjectRecordDataMutableRef() {}

ibValueMetaObjectRecordDataRecorderRef::~ibValueMetaObjectRecordDataRecorderRef() {}

bool ibValueMetaObjectRecordDataRecorderRef::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyAttributeNumber->GetName(), m_propertyAttributeNumber->GetNodeValue());
	node.SetProperty(m_propertyAttributeDate->GetName(), m_propertyAttributeDate->GetNodeValue());
	// …AND THE MOMENT, so its id survives the file. An id assigned at creation and never written
	// would be a different number in every session, and a number is what a query field is named by.
	node.SetProperty(m_propertyAttributePointInTime->GetName(), m_propertyAttributePointInTime->GetNodeValue());
	node.SetProperty(m_propertyRegisterRecord->GetName(), m_propertyRegisterRecord->GetNodeValue());
	// ⭐⭐ BOTH LISTS, OR THE SECOND ONE IS EDITED AND FORGOTTEN. The sequences a document registers in
	// are a list of its own beside the registers it posts movements to, and a list that is not written
	// here comes back EMPTY on the next load — with three consequences that each look like something
	// else: `Sequences.<Name>` is not found in the posting handler ("Aggregate object field not
	// found"), the differ compares an empty stored list with the edited one and reports the SAME
	// pending change after every apply, and the DDL that change implies — the recorder's reference
	// pair — is emitted again over a table that already has it, which Firebird refuses for good
	// ("violation of PRIMARY or UNIQUE KEY … FLD…_RTREF"). Measured 2026-09-18, in that order.
	node.SetProperty(m_propertySequenceRecord->GetName(), m_propertySequenceRecord->GetNodeValue());
	node.SetProperty(m_propertyRegisterRecordsDeletion->GetName(), m_propertyRegisterRecordsDeletion->GetNodeValue());
	return ibValueMetaObjectRecordDataMutableRef::WriteData(node);
}

bool ibValueMetaObjectRecordDataRecorderRef::ReadData(const ibDataNode& node)
{
	m_propertyAttributeNumber->SetNodeValue(node.GetProperty(m_propertyAttributeNumber->GetName()));
	m_propertyAttributeDate->SetNodeValue(node.GetProperty(m_propertyAttributeDate->GetName()));
	m_propertyAttributePointInTime->SetNodeValue(node.GetProperty(m_propertyAttributePointInTime->GetName()));
	m_propertyRegisterRecord->SetNodeValue(node.GetProperty(m_propertyRegisterRecord->GetName()));
	// …and the sequences, read back beside them. Absent from a configuration saved before sequences
	// existed, which reads as the empty list — a document that registers nowhere, which is right.
	m_propertySequenceRecord->SetNodeValue(node.GetProperty(m_propertySequenceRecord->GetName()));
	// Absent from a configuration saved before it existed: the default stays — the movements are cleared, as they were.
	m_propertyRegisterRecordsDeletion->SetNodeValue(node.GetProperty(m_propertyRegisterRecordsDeletion->GetName()));
	return ibValueMetaObjectRecordDataMutableRef::ReadData(node);
}

bool ibValueMetaObjectRecordDataRecorderRef::OnCreateMetaObject(ibMetaData* metaData, int flags)
{
	if (!ibValueMetaObjectRecordDataMutableRef::OnCreateMetaObject(metaData, flags))
		return false;

	// ⭐ THE MOMENT REGISTERS TOO — it is virtual, not anonymous. It never joins the predefined LIST
	// (what it is built from is already stored), and that is about STORAGE; registration is about
	// IDENTITY. Skipped once, the attribute carried metaID 0 and answered `fld0` when anything asked
	// for its field name — a name no table has (2026-08-22).
	return (*m_propertyAttributeNumber)->OnCreateMetaObject(metaData, flags)
		&& (*m_propertyAttributeDate)->OnCreateMetaObject(metaData, flags)
		&& (*m_propertyAttributePointInTime)->OnCreateMetaObject(metaData, flags);
}

bool ibValueMetaObjectRecordDataRecorderRef::OnLoadMetaObject(ibMetaData* metaData)
{
	if (!(*m_propertyAttributeNumber)->OnLoadMetaObject(metaData))
		return false;
	if (!(*m_propertyAttributeDate)->OnLoadMetaObject(metaData))
		return false;
	// The moment's id has to come back with the rest, or it would be a different field name in every
	// session — and a name is what a query writes down.
	if (!(*m_propertyAttributePointInTime)->OnLoadMetaObject(metaData))
		return false;

	return ibValueMetaObjectRecordDataMutableRef::OnLoadMetaObject(metaData);
}

bool ibValueMetaObjectRecordDataRecorderRef::OnSaveMetaObject(int flags)
{
	if (!(*m_propertyAttributeNumber)->OnSaveMetaObject(flags))
		return false;
	if (!(*m_propertyAttributeDate)->OnSaveMetaObject(flags))
		return false;
	if (!(*m_propertyAttributePointInTime)->OnSaveMetaObject(flags))
		return false;

	return ibValueMetaObjectRecordDataMutableRef::OnSaveMetaObject(flags);
}

bool ibValueMetaObjectRecordDataRecorderRef::OnDeleteMetaObject()
{
	if (!(*m_propertyAttributeNumber)->OnDeleteMetaObject())
		return false;
	if (!(*m_propertyAttributeDate)->OnDeleteMetaObject())
		return false;
	if (!(*m_propertyAttributePointInTime)->OnDeleteMetaObject())
		return false;

	return ibValueMetaObjectRecordDataMutableRef::OnDeleteMetaObject();
}

bool ibValueMetaObjectRecordDataRecorderRef::OnBeforeRunMetaObject(int flags)
{
	if (!(*m_propertyAttributeNumber)->OnBeforeRunMetaObject(flags))
		return false;
	if (!(*m_propertyAttributeDate)->OnBeforeRunMetaObject(flags))
		return false;
	if (!(*m_propertyAttributePointInTime)->OnBeforeRunMetaObject(flags))
		return false;

	return ibValueMetaObjectRecordDataMutableRef::OnBeforeRunMetaObject(flags);
}

bool ibValueMetaObjectRecordDataRecorderRef::OnAfterRunMetaObject(int flags)
{
	// THE KIND REGISTERS ITS OWN SOURCE — and here it matters most: this descriptor's queryable is the
	// one that vends the MOMENT, so registering any other would leave that column vended and invisible.
	m_metaData->RegisterSource(&m_queryable);

	if (!(*m_propertyAttributeNumber)->OnAfterRunMetaObject(flags))
		return false;
	if (!(*m_propertyAttributeDate)->OnAfterRunMetaObject(flags))
		return false;
	if (!(*m_propertyAttributePointInTime)->OnAfterRunMetaObject(flags))
		return false;

	// ⭐ AND EVERY REGISTER THIS ONE RECORDS INTO LEARNS THAT IT DOES. The register's Recorder
	// attribute accepts whatever writes movements into it, so each register named in the record
	// description takes this metatype's reference into that attribute's type — which is what makes
	// `Recorder = <this document>` expressible at all. Undone symmetrically on close, below.
	// …AND THE SEQUENCES IT REGISTERS IN LEARN THE SAME WAY. They are a list of their own, and their
	// Recorder is the same attribute answering the same question — so both lists are walked here, or
	// a sequence would be declared and still refuse to be saved: "no recorder", because nothing had
	// told it who registers in it (2026-09-18).
	const ibMetaDescription& movements = GetRecordDescription(ibRecorderWrites::Movements);
	for (unsigned int idx = 0; idx < movements.GetTypeCount(); idx++) {
		const ibValueMetaObjectRegisterData* registerData = m_metaData->FindAnyObjectByFilter<ibValueMetaObjectRegisterData>(movements.GetByIdx(idx));
		if (registerData != nullptr) {
			ibValueMetaObjectAttributePredefined* infoRecorder = registerData->GetRegisterRecorder();
			wxASSERT(infoRecorder);
			infoRecorder->GetTypeDesc().AppendMetaType((*m_propertyAttributeReference)->GetTypeDesc());
		}
	}

	const ibMetaDescription& sequences = GetRecordDescription(ibRecorderWrites::Sequences);
	for (unsigned int idx = 0; idx < sequences.GetTypeCount(); idx++) {
		const ibValueMetaObjectRegisterData* registerData = m_metaData->FindAnyObjectByFilter<ibValueMetaObjectRegisterData>(sequences.GetByIdx(idx));
		if (registerData != nullptr) {
			ibValueMetaObjectAttributePredefined* infoRecorder = registerData->GetRegisterRecorder();
			wxASSERT(infoRecorder);
			infoRecorder->GetTypeDesc().AppendMetaType((*m_propertyAttributeReference)->GetTypeDesc());
		}
	}

	return ibValueMetaObjectRecordDataMutableRef::OnAfterRunMetaObject(flags);
}

bool ibValueMetaObjectRecordDataRecorderRef::OnBeforeCloseMetaObject()
{
	m_metaData->UnregisterSource(&m_queryable);

	if (!(*m_propertyAttributeNumber)->OnBeforeCloseMetaObject())
		return false;
	if (!(*m_propertyAttributeDate)->OnBeforeCloseMetaObject())
		return false;
	if (!(*m_propertyAttributePointInTime)->OnBeforeCloseMetaObject())
		return false;

	// …and the registers — and the sequences — stop accepting it: the mirror of the append above,
	// over both lists.
	const ibMetaDescription& movements = GetRecordDescription(ibRecorderWrites::Movements);
	for (unsigned int idx = 0; idx < movements.GetTypeCount(); idx++) {
		const ibValueMetaObjectRegisterData* registerData = m_metaData->FindAnyObjectByFilter<ibValueMetaObjectRegisterData>(movements.GetByIdx(idx));
		if (registerData != nullptr) {
			ibValueMetaObjectAttributePredefined* infoRecorder = registerData->GetRegisterRecorder();
			wxASSERT(infoRecorder);
			infoRecorder->GetTypeDesc().ClearMetaType((*m_propertyAttributeReference)->GetTypeDesc());
		}
	}

	const ibMetaDescription& sequences = GetRecordDescription(ibRecorderWrites::Sequences);
	for (unsigned int idx = 0; idx < sequences.GetTypeCount(); idx++) {
		const ibValueMetaObjectRegisterData* registerData = m_metaData->FindAnyObjectByFilter<ibValueMetaObjectRegisterData>(sequences.GetByIdx(idx));
		if (registerData != nullptr) {
			ibValueMetaObjectAttributePredefined* infoRecorder = registerData->GetRegisterRecorder();
			wxASSERT(infoRecorder);
			infoRecorder->GetTypeDesc().ClearMetaType((*m_propertyAttributeReference)->GetTypeDesc());
		}
	}

	return ibValueMetaObjectRecordDataMutableRef::OnBeforeCloseMetaObject();
}

bool ibValueMetaObjectRecordDataRecorderRef::OnAfterCloseMetaObject()
{
	if (!(*m_propertyAttributeNumber)->OnAfterCloseMetaObject())
		return false;
	if (!(*m_propertyAttributeDate)->OnAfterCloseMetaObject())
		return false;
	if (!(*m_propertyAttributePointInTime)->OnAfterCloseMetaObject())
		return false;

	return ibValueMetaObjectRecordDataMutableRef::OnAfterCloseMetaObject();
}

// A RECORDED FACT READS AS ITS NUMBER AND ITS MOMENT — the pair this class owns.
//
// ⭐ A DOCUMENT DECLARES NO VALUES: its only declared reference is the EMPTY one, which is still a
// legitimate thing to write in a setting ("documents open too — a document just has only the
// reference"). Anything else names a row, and a row reads as number + date.
bool ibValueMetaObjectRecordDataRecorderRef::GenerateDataDesc(const ibValueDataObject* objValue,
	wxString& out) const
{
	if (objValue == nullptr)
		return false;

	if (appData->DesignerMode()) {
		const wxString empty = ibDeclaredEmptyRef(this, objValue->GetGuid());
		if (!empty.IsEmpty()) {
			out = empty;
			return true;
		}
	}

	return ibValueMetaObjectRecordDataMutableRef::GenerateDataDesc(objValue, out);   // by its template
}

// …and no order of its own: two records are told apart by identity.
int ibValueMetaObjectRecordDataRecorderRef::CompareDataValues(const ibValueDataObject* lhs,
	const ibValueDataObject* rhs) const
{
	return ibCompareByIdentity(lhs, rhs);
}


///////////////////////////////////////////////////////////////////////////////

ibValuePtr<ibValueRecordDataObjectRef> ibValueMetaObjectRecordDataMutableRef::CreateObjectValue() const
{
	const ibValuePtr<ibValueRecordDataObjectRef> created(CreateObjectRefValue());
	if (created != nullptr && !created->InitializeObject())
		return nullptr;
	return created;
}

ibValuePtr<ibValueRecordDataObjectRef> ibValueMetaObjectRecordDataMutableRef::CreateObjectValue(const ibGuid& guid) const
{
	const ibValuePtr<ibValueRecordDataObjectRef> created(CreateObjectRefValue(guid));
	if (created != nullptr && !created->InitializeObject())
		return nullptr;
	return created;
}

ibValuePtr<ibValueRecordDataObjectRef> ibValueMetaObjectRecordDataMutableRef::CreateObjectValue(ibValueRecordDataObjectRef* objSrc, bool generate) const
{
	if (objSrc == nullptr)
		return nullptr;
	const ibValuePtr<ibValueRecordDataObjectRef> created(CreateObjectRefValue());
	if (created != nullptr && !created->InitializeObject(objSrc, generate))
		return nullptr;
	return created;
}

ibValuePtr<ibValueRecordDataObjectRef> ibValueMetaObjectRecordDataMutableRef::CopyObjectValue(const ibGuid& srcGuid) const
{
	const ibValuePtr<ibValueRecordDataObjectRef> created(CreateObjectRefValue());
	if (created != nullptr && !created->InitializeObject(srcGuid))
		return nullptr;
	return created;
}

ibValuePtr<ibValueRecordDataObject> ibValueMetaObjectRecordDataMutableRef::CreateRecordDataObjectValue() const
{
	return CreateObjectValue();
}

//***********************************************************************
//*						ibValueMetaObjectRecordDataHierarchyMutableRef	*
//***********************************************************************

ibValueMetaObjectRecordDataHierarchyMutableRef::ibValueMetaObjectRecordDataHierarchyMutableRef()
	: ibValueMetaObjectRecordDataMutableRef()
{
}

ibValueMetaObjectRecordDataHierarchyMutableRef::~ibValueMetaObjectRecordDataHierarchyMutableRef()
{
	//wxDELETE(m_propertyAttributeCode);
	//wxDELETE(m_propertyAttributeDescription);
	//wxDELETE(m_propertyAttributeParent);
	//wxDELETE(m_propertyAttributeIsFolder);
}

// HOW A ROW OF THIS FAMILY READS — see the declaration for why it lives here and not three times over.
bool ibValueMetaObjectRecordDataHierarchyMutableRef::GenerateDataDesc(const ibValueDataObject* objValue,
	wxString& out) const
{
	if (objValue == nullptr)
		return false;

	// ⭐⭐ IN THE DESIGNER A REFERENCE IS A DECLARATION — there is no row to describe, and what it says is
	// what the configuration declares: the empty reference, or one of the predefined values, written as
	// `CatalogRef.Goods.Chair`.
	if (appData->DesignerMode()) {
		const wxString empty = ibDeclaredEmptyRef(this, objValue->GetGuid());
		if (!empty.IsEmpty()) {
			out = empty;
			return true;
		}
		for (const auto& item : GetPredefinedValueArray())
			if (item != nullptr && item->GetPredefinedGuid() == objValue->GetGuid()) {
				out = ibDeclaredTypeName(this) + wxT(".") + item->GetPredefinedName();
				return true;
			}
	}

	// …and at run time the row's own Description, by its template. TRUE even when it comes back EMPTY: a
	// row whose description nobody filled in HAS a presentation and it is blank. `false` is kept for
	// "there is nothing here to read at all".
	return ibValueMetaObjectRecordDataMutableRef::GenerateDataDesc(objValue, out);
}

// …and no order of its own: the rows of a catalog, a chart of accounts or a chart of characteristic
// types are told apart by identity.
int ibValueMetaObjectRecordDataHierarchyMutableRef::CompareDataValues(const ibValueDataObject* lhs,
	const ibValueDataObject* rhs) const
{
	return ibCompareByIdentity(lhs, rhs);
}

////////////////////////////////////////////////////////////////////////////////////////////////

ibValuePtr<ibValueRecordDataObjectHierarchyRef> ibValueMetaObjectRecordDataHierarchyMutableRef::CreateObjectValue(ibObjectMode mode) const
{
	const ibValuePtr<ibValueRecordDataObjectHierarchyRef> created(CreateObjectRefValue(mode));
	if (created != nullptr && !created->InitializeObject())
		return nullptr;
	return created;
}

ibValuePtr<ibValueRecordDataObjectHierarchyRef> ibValueMetaObjectRecordDataHierarchyMutableRef::CreateObjectValue(ibObjectMode mode, const ibGuid& guid) const
{
	const ibValuePtr<ibValueRecordDataObjectHierarchyRef> created(CreateObjectRefValue(mode, guid));
	if (created != nullptr && !created->InitializeObject())
		return nullptr;
	return created;
}

ibValuePtr<ibValueRecordDataObjectHierarchyRef> ibValueMetaObjectRecordDataHierarchyMutableRef::CreateObjectValue(ibObjectMode mode, ibValueRecordDataObjectRef* objSrc, bool generate) const
{
	const ibValuePtr<ibValueRecordDataObjectHierarchyRef> created(CreateObjectRefValue(mode));
	if (created != nullptr && !created->InitializeObject(objSrc, generate))
		return nullptr;
	return created;
}

ibValuePtr<ibValueRecordDataObjectHierarchyRef> ibValueMetaObjectRecordDataHierarchyMutableRef::CopyObjectValue(ibObjectMode mode, const ibGuid& srcGuid) const
{
	const ibValuePtr<ibValueRecordDataObjectHierarchyRef> created(CreateObjectRefValue(mode));
	if (created != nullptr && !created->InitializeObject(srcGuid))
		return nullptr;
	return created;
}

//***************************************************************************
//*                       Predefined values                                 *
//***************************************************************************

#define predefinedBlock 0x1234532

//append predefined value
void ibValueMetaObjectRecordDataHierarchyMutableRef::AppendPredefinedValue(const wxString& strPredefinedName,
	const wxString& strCode, const wxString& strDescription,
	bool valueIsFolder, const wxObjectDataPtr<ibPredefinedValueObject>& valueParent)
{
	m_predefinedObjectVector.emplace_back(
		new ibPredefinedValueObject(wxNewUniqueGuid, strPredefinedName,
			strCode, strDescription, valueIsFolder, valueParent));

	m_metaData->Modify(true);
}

void ibValueMetaObjectRecordDataHierarchyMutableRef::SetPredefinedValue(const ibGuid& predefinedGuid,
	const wxString& strPredefinedName,
	const wxString& strCode, const wxString& strDescription,
	bool valueIsFolder, const wxObjectDataPtr<ibPredefinedValueObject>& valueParent)
{
	wxObjectDataPtr<ibPredefinedValueObject> foundedPredefinedValue = FindPredefinedValue(predefinedGuid);

	if (foundedPredefinedValue != nullptr) {
		
		foundedPredefinedValue->m_strPredefinedName = strPredefinedName;
		foundedPredefinedValue->m_strCode = strCode;
		foundedPredefinedValue->m_strDescription = strDescription;
		foundedPredefinedValue->m_valueIsFolder = valueIsFolder;
		foundedPredefinedValue->m_valueParent = valueParent;
		
		m_metaData->Modify(true);
		return;
	}

	m_predefinedObjectVector.emplace_back(
		new ibPredefinedValueObject(predefinedGuid, strPredefinedName,
			strCode, strDescription, valueIsFolder, valueParent));

	m_metaData->Modify(true);
}

void ibValueMetaObjectRecordDataHierarchyMutableRef::DeletePredefinedValue(const ibGuid& predefinedGuid) {
	
	m_predefinedObjectVector.erase(
		std::remove_if(m_predefinedObjectVector.begin(), m_predefinedObjectVector.end(),
			[predefinedGuid](const auto value) { return predefinedGuid == value->GetPredefinedGuid(); }), m_predefinedObjectVector.end());

	m_metaData->Modify(true);
}

//***************************************************************************
//*                       Save & load metaData                              *
//***************************************************************************

bool ibValueMetaObjectRecordDataHierarchyMutableRef::WriteData(ibDataNode& node) const
{
	// predefined values -> an Array of Child{ guid, name, code, description, isFolder }
	std::vector<ibDataValue> predefined;
	for (const auto& value : m_predefinedObjectVector) {
		auto pv = std::make_shared<ibDataNode>();
		pv->SetValue(wxT("Guid"), value->GetPredefinedGuid());
		pv->SetValue(wxT("Name"), value->GetPredefinedName());
		pv->SetValue(wxT("Code"), value->GetPredefinedCode());
		pv->SetValue(wxT("Description"), value->GetPredefinedDescription());
		pv->SetValue(wxT("IsFolder"), (bool)value->IsPredefinedFolder());
		predefined.push_back(ibDataValue::Child(pv));
	}
	node.SetProperty(wxT("Predefined"), ibDataValue::Array(predefined));

	node.SetProperty(m_propertyAttributePredefined->GetName(), m_propertyAttributePredefined->GetNodeValue());
	node.SetProperty(m_propertyAttributeCode->GetName(), m_propertyAttributeCode->GetNodeValue());
	node.SetProperty(m_propertyAttributeDescription->GetName(), m_propertyAttributeDescription->GetNodeValue());
	node.SetProperty(m_propertyAttributeParent->GetName(), m_propertyAttributeParent->GetNodeValue());
	node.SetProperty(m_propertyAttributeIsFolder->GetName(), m_propertyAttributeIsFolder->GetNodeValue());
	// A property absent from this pair does not survive the session that set it, and the failure is
	// silent because the default keeps working.
	node.SetProperty(m_propertyHierarchyType->GetName(), m_propertyHierarchyType->GetNodeValue());
	node.SetProperty(m_propertyDataPresentation->GetName(), m_propertyDataPresentation->GetNodeValue());

	return ibValueMetaObjectRecordDataMutableRef::WriteData(node);
}

bool ibValueMetaObjectRecordDataHierarchyMutableRef::ReadData(const ibDataNode& node)
{
	const ibDataValue predefinedVal = node.GetProperty(wxT("Predefined"));
	if (predefinedVal.Kind() == ibDataKind::Array) {
		for (const ibDataValue& item : predefinedVal.AsArray()) {
			const std::shared_ptr<ibDataNode>& pv = item.AsChild();
			if (!pv)
				continue;
			const ibGuid valueGuid = pv->GetValue<ibGuid>(wxT("Guid"));
			wxString valueName = pv->GetValue<wxString>(wxT("Name"));
			wxString valueCode = pv->GetValue<wxString>(wxT("Code"));
			wxString valueDescription = pv->GetValue<wxString>(wxT("Description"));
			m_predefinedObjectVector.emplace_back(
				new ibPredefinedValueObject(valueGuid, valueName, valueCode, valueDescription));
		}
	}

	m_propertyAttributePredefined->SetNodeValue(node.GetProperty(m_propertyAttributePredefined->GetName()));
	m_propertyAttributeCode->SetNodeValue(node.GetProperty(m_propertyAttributeCode->GetName()));
	m_propertyAttributeDescription->SetNodeValue(node.GetProperty(m_propertyAttributeDescription->GetName()));
	m_propertyAttributeParent->SetNodeValue(node.GetProperty(m_propertyAttributeParent->GetName()));
	m_propertyAttributeIsFolder->SetNodeValue(node.GetProperty(m_propertyAttributeIsFolder->GetName()));

	m_propertyHierarchyType->SetNodeValue(node.GetProperty(m_propertyHierarchyType->GetName()));
	// Restate it on the Parent field at once: everything that asks what a parent may be asks the
	// FIELD, so a configuration loaded and never edited must already say what it declares.
	ApplyHierarchyType();

	// Absent from a configuration saved before it existed: the kind's own default stands (SetNodeValue
	// leaves an empty value alone) - Description for a catalog, Code for a chart of accounts.
	m_propertyDataPresentation->SetNodeValue(node.GetProperty(m_propertyDataPresentation->GetName()));

	return ibValueMetaObjectRecordDataMutableRef::ReadData(node);
}

//***********************************************************************
//*                           read & save events                        *
//***********************************************************************

bool ibValueMetaObjectRecordDataHierarchyMutableRef::OnCreateMetaObject(ibMetaData* metaData, int flags)
{
	if (!ibValueMetaObjectRecordDataMutableRef::OnCreateMetaObject(metaData, flags))
		return false;

	return (*m_propertyAttributePredefined)->OnCreateMetaObject(metaData, flags) &&
		(*m_propertyAttributeCode)->OnCreateMetaObject(metaData, flags) &&
		(*m_propertyAttributeDescription)->OnCreateMetaObject(metaData, flags) &&
		(*m_propertyAttributeParent)->OnCreateMetaObject(metaData, flags) &&
		(*m_propertyAttributeIsFolder)->OnCreateMetaObject(metaData, flags);
}

bool ibValueMetaObjectRecordDataHierarchyMutableRef::OnLoadMetaObject(ibMetaData* metaData)
{
	if (!(*m_propertyAttributePredefined)->OnLoadMetaObject(metaData))
		return false;

	if (!(*m_propertyAttributeCode)->OnLoadMetaObject(metaData))
		return false;

	if (!(*m_propertyAttributeDescription)->OnLoadMetaObject(metaData))
		return false;

	if (!(*m_propertyAttributeParent)->OnLoadMetaObject(metaData))
		return false;

	if (!(*m_propertyAttributeIsFolder)->OnLoadMetaObject(metaData))
		return false;

	return ibValueMetaObjectRecordDataMutableRef::OnLoadMetaObject(metaData);
}

bool ibValueMetaObjectRecordDataHierarchyMutableRef::OnSaveMetaObject(int flags)
{
	if (!(*m_propertyAttributePredefined)->OnSaveMetaObject(flags))
		return false;

	if (!(*m_propertyAttributeCode)->OnSaveMetaObject(flags))
		return false;

	if (!(*m_propertyAttributeDescription)->OnSaveMetaObject(flags))
		return false;

	if (!(*m_propertyAttributeParent)->OnSaveMetaObject(flags))
		return false;

	if (!(*m_propertyAttributeIsFolder)->OnSaveMetaObject(flags))
		return false;

	return ibValueMetaObjectRecordDataMutableRef::OnSaveMetaObject(flags);
}

bool ibValueMetaObjectRecordDataHierarchyMutableRef::OnDeleteMetaObject()
{
	if (!(*m_propertyAttributePredefined)->OnDeleteMetaObject())
		return false;

	if (!(*m_propertyAttributeCode)->OnDeleteMetaObject())
		return false;

	if (!(*m_propertyAttributeDescription)->OnDeleteMetaObject())
		return false;

	if (!(*m_propertyAttributeParent)->OnDeleteMetaObject())
		return false;

	if (!(*m_propertyAttributeIsFolder)->OnDeleteMetaObject())
		return false;

	return ibValueMetaObjectRecordDataMutableRef::OnDeleteMetaObject();
}

bool ibValueMetaObjectRecordDataHierarchyMutableRef::OnBeforeRunMetaObject(int flags)
{
	if (!(*m_propertyAttributePredefined)->OnBeforeRunMetaObject(flags))
		return false;

	if (!(*m_propertyAttributeCode)->OnBeforeRunMetaObject(flags))
		return false;

	if (!(*m_propertyAttributeDescription)->OnBeforeRunMetaObject(flags))
		return false;

	if (!(*m_propertyAttributeParent)->OnBeforeRunMetaObject(flags))
		return false;

	if (!(*m_propertyAttributeIsFolder)->OnBeforeRunMetaObject(flags))
		return false;

	if (!ibValueMetaObjectRecordDataMutableRef::OnBeforeRunMetaObject(flags))
		return false;

	return true;
}

bool ibValueMetaObjectRecordDataHierarchyMutableRef::OnAfterRunMetaObject(int flags)
{
	// THE KIND REGISTERS ITS OWN SOURCE — a hierarchy reads through the queryable typed to it, and a
	// catalogue / chart / job resolves through that one because it IS a hierarchy.
	m_metaData->RegisterSource(&m_queryable);

	if (!(*m_propertyAttributePredefined)->OnAfterRunMetaObject(flags))
		return false;

	if (!(*m_propertyAttributeCode)->OnAfterRunMetaObject(flags))
		return false;

	if (!(*m_propertyAttributeDescription)->OnAfterRunMetaObject(flags))
		return false;

	if (!(*m_propertyAttributeParent)->OnAfterRunMetaObject(flags))
		return false;

	if (!(*m_propertyAttributeIsFolder)->OnAfterRunMetaObject(flags))
		return false;

	return ibValueMetaObjectRecordDataMutableRef::OnAfterRunMetaObject(flags);
}

bool ibValueMetaObjectRecordDataHierarchyMutableRef::OnBeforeCloseMetaObject()
{
	m_metaData->UnregisterSource(&m_queryable);

	if (!(*m_propertyAttributePredefined)->OnBeforeCloseMetaObject())
		return false;

	if (!(*m_propertyAttributeCode)->OnBeforeCloseMetaObject())
		return false;

	if (!(*m_propertyAttributeDescription)->OnBeforeCloseMetaObject())
		return false;

	if (!(*m_propertyAttributeParent)->OnBeforeCloseMetaObject())
		return false;

	if (!(*m_propertyAttributeIsFolder)->OnBeforeCloseMetaObject())
		return false;

	return ibValueMetaObjectRecordDataMutableRef::OnBeforeCloseMetaObject();
}

bool ibValueMetaObjectRecordDataHierarchyMutableRef::OnAfterCloseMetaObject()
{
	if (!(*m_propertyAttributePredefined)->OnAfterCloseMetaObject())
		return false;

	if (!(*m_propertyAttributeCode)->OnAfterCloseMetaObject())
		return false;

	if (!(*m_propertyAttributeDescription)->OnAfterCloseMetaObject())
		return false;

	if (!(*m_propertyAttributeParent)->OnAfterCloseMetaObject())
		return false;

	if (!(*m_propertyAttributeIsFolder)->OnAfterCloseMetaObject())
		return false;

	return ibValueMetaObjectRecordDataMutableRef::OnAfterCloseMetaObject();
}

//////////////////////////////////////////////////////////////////////

bool ibValueMetaObjectRecordDataHierarchyMutableRef::ProcessChoice(ibBackendControlFrame* ownerValue, const ibFormRequest& request) const
{
	const ibSelectMode selMode = request.m_create.m_selectMode;

	if (ownerValue == nullptr)
		return false;

	ibBackendValueForm* selectChoiceForm = nullptr;

	// The `selectChoiceForm == nullptr` guards are gone: the variable is initialised to
	// nullptr on the line above and nothing touches it in between, so both were always true.
	// They also made the first condition read as "(null AND items) OR foldersAndItems"
	// (&& binds tighter), which is what GCC flagged. Behaviour is unchanged — the branch
	// was, and is, chosen purely by selMode.
	//
	// The same as its flat sibling: whichever of the two forms is made, it is MADE with the request,
	// and its list is born narrowed.
	if (selMode == ibSelectMode::ibSelectMode_Items || selMode == ibSelectMode::ibSelectMode_FoldersAndItems) {
		selectChoiceForm = GetSelectForm(request, ownerValue);
	}
	else if (selMode == ibSelectMode::ibSelectMode_Folders) {
		selectChoiceForm = GetFolderSelectForm(request, ownerValue);
	}

	if (selectChoiceForm == nullptr)
		return false;

	selectChoiceForm->ShowForm();
	return true;
}

//////////////////////////////////////////////////////////////////////

ibValuePtr<ibValueRecordDataObjectRef> ibValueMetaObjectRecordDataHierarchyMutableRef::CreateObjectRefValue(const ibGuid& objGuid) const
{
	return CreateObjectRefValue(ibObjectMode::OBJECT_ITEM, objGuid);
}

//***********************************************************************
//*                      ibValueMetaObjectRegisterData						*
//***********************************************************************

ibValueMetaObjectRegisterData::ibValueMetaObjectRegisterData() : ibValueMetaObjectGenericData()
{
}

ibValueMetaObjectRegisterData::~ibValueMetaObjectRegisterData()
{
	//wxDELETE((*m_propertyAttributeLineActive));
	//wxDELETE((*m_propertyAttributePeriod));
	//wxDELETE((*m_propertyAttributeRecorder));
	//wxDELETE((*m_propertyAttributeLineNumber));
}

//***************************************************************************
//*                       Save & load metaData                              *
//***************************************************************************

// Base ibValueMetaObjectGenericData has no data of its own — chain bottoms here.
bool ibValueMetaObjectRegisterData::ReadData(const ibDataNode& node)
{
	m_propertyAttributeLineActive->SetNodeValue(node.GetProperty(m_propertyAttributeLineActive->GetName()));
	m_propertyAttributePeriod->SetNodeValue(node.GetProperty(m_propertyAttributePeriod->GetName()));
	m_propertyAttributeRecorder->SetNodeValue(node.GetProperty(m_propertyAttributeRecorder->GetName()));
	m_propertyAttributeLineNumber->SetNodeValue(node.GetProperty(m_propertyAttributeLineNumber->GetName()));
	return true;
}

bool ibValueMetaObjectRegisterData::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyAttributeLineActive->GetName(), m_propertyAttributeLineActive->GetNodeValue());
	node.SetProperty(m_propertyAttributePeriod->GetName(), m_propertyAttributePeriod->GetNodeValue());
	node.SetProperty(m_propertyAttributeRecorder->GetName(), m_propertyAttributeRecorder->GetNodeValue());
	node.SetProperty(m_propertyAttributeLineNumber->GetName(), m_propertyAttributeLineNumber->GetNodeValue());
	return true;
}

//***********************************************************************
//*                           read & save events                        *
//***********************************************************************

bool ibValueMetaObjectRegisterData::OnCreateMetaObject(ibMetaData* metaData, int flags)
{
	if (!ibValueMetaObject::OnCreateMetaObject(metaData, flags))
		return false;

	return (*m_propertyAttributeLineActive)->OnCreateMetaObject(metaData, flags) &&
		(*m_propertyAttributePeriod)->OnCreateMetaObject(metaData, flags) &&
		(*m_propertyAttributeRecorder)->OnCreateMetaObject(metaData, flags) &&
		(*m_propertyAttributeLineNumber)->OnCreateMetaObject(metaData, flags);
}

bool ibValueMetaObjectRegisterData::OnLoadMetaObject(ibMetaData* metaData)
{
	if (!(*m_propertyAttributeLineActive)->OnLoadMetaObject(metaData))
		return false;

	if (!(*m_propertyAttributePeriod)->OnLoadMetaObject(metaData))
		return false;

	if (!(*m_propertyAttributeRecorder)->OnLoadMetaObject(metaData))
		return false;

	if (!(*m_propertyAttributeLineNumber)->OnLoadMetaObject(metaData))
		return false;

	return ibValueMetaObject::OnLoadMetaObject(metaData);
}

bool ibValueMetaObjectRegisterData::OnSaveMetaObject(int flags)
{
	// ⭐⭐ A REGISTER WITH NOTHING IN IT IS NOT A REGISTER — and it is the one shape that cannot
	// reach the database at all.
	//
	// Dimensions say what a record is ABOUT, resources say what it MEASURES, attributes say what
	// else it carries. With none of the three there is no fact to record: the predefined columns
	// alone describe an occurrence that states nothing.
	//
	// ⚠ AND A TABLE NEEDS AT LEAST ONE COLUMN. An independent register with nothing declared has
	// none, so the differ correctly computed "every column of this table is gone" and emitted
	// `ALTER TABLE … DROP <all of them>` — which Firebird answers with "last column in a table
	// cannot be deleted". That refusal aborts the WHOLE apply, so one half-built register blocked
	// every other change in the configuration, and the message named neither the register nor the
	// reason. Refusing at the door turns a dead end into one sentence naming the object.
	//
	// Same door and same shape as the enumeration with no values
	// (`ibValueMetaObjectEnumeration::OnSaveMetaObject`): an empty metaobject is refused where it is
	// SAVED, by the restructure report, so the message lands in the pane under the editor and no
	// exception leaves the configuration write transaction open.
	if (GetDimensionArrayObject().empty() && GetResourceArrayObject().empty()
		&& GetAttributeArrayObject().empty()) {
		RestructureError(_("! Doesn't have any dimension, resource or attribute ") + GetFullName());
		return false;
	}

	// ⭐⭐ SUBORDINATE TO A RECORDER MEANS THERE HAS TO BE ONE.
	//
	// A register that says it is written BY a recorder cannot exist without one: every row it holds
	// is owned by a document, its whole write path is "the document posts, the register records", and
	// the Recorder column's type IS the set of documents that declared they post here. Empty, that
	// set makes the column admit nothing — so the register can be created, applied and reported on,
	// and nothing will ever be able to write a single row into it.
	//
	// Reported, not thrown, and here rather than on the schema declaration: the same reasoning as the
	// chart of accounts' missing binding — the message belongs in the pane under the editor, the save
	// refuses, and no exception leaves the configuration write transaction open.
	if (HasRecorder() && (*m_propertyAttributeRecorder)->IsEmptyTypeDesc()) {
		// Into the LEDGER — see the note in metaComposerObject.cpp: one road for a refusal, read by
		// whoever asked. Printed here it also stayed on the screen after a probe that only asked.
		RestructureError(wxString::Format(
			_("%s: no recorder - declare a document that posts into this register, or it can never be written to"), GetName()));
		return false;
	}

	if (!(*m_propertyAttributeLineActive)->OnSaveMetaObject(flags))
		return false;

	if (!(*m_propertyAttributePeriod)->OnSaveMetaObject(flags))
		return false;

	if (!(*m_propertyAttributeRecorder)->OnSaveMetaObject(flags))
		return false;

	if (!(*m_propertyAttributeLineNumber)->OnSaveMetaObject(flags))
		return false;

	return ibValueMetaObject::OnSaveMetaObject(flags);
}

bool ibValueMetaObjectRegisterData::OnDeleteMetaObject()
{
	if (!(*m_propertyAttributeLineActive)->OnDeleteMetaObject())
		return false;

	if (!(*m_propertyAttributePeriod)->OnDeleteMetaObject())
		return false;

	if (!(*m_propertyAttributeRecorder)->OnDeleteMetaObject())
		return false;

	if (!(*m_propertyAttributeLineNumber)->OnDeleteMetaObject())
		return false;

	return ibValueMetaObject::OnDeleteMetaObject();
}

bool ibValueMetaObjectRegisterData::OnBeforeRunMetaObject(int flags)
{
	if (!(*m_propertyAttributeLineActive)->OnBeforeRunMetaObject(flags))
		return false;

	if (!(*m_propertyAttributePeriod)->OnBeforeRunMetaObject(flags))
		return false;

	if (!(*m_propertyAttributeRecorder)->OnBeforeRunMetaObject(flags))
		return false;

	if (!(*m_propertyAttributeLineNumber)->OnBeforeRunMetaObject(flags))
		return false;

	registerManager();
	registerRecordKey();
	registerRecordSet();
	registerRecordSet_String();

	registerRecordManager();

	return ibValueMetaObject::OnBeforeRunMetaObject(flags);
}

bool ibValueMetaObjectRegisterData::OnAfterRunMetaObject(int flags)
{
	if (!ibValueMetaObjectGenericData::OnAfterRunMetaObject(flags))
		return false;
	// The register OWNS its main (records) descriptor field. On load it ADDITIONALLY registers
	// its balance / turnover / slice descriptors (separate parameterized descriptors — TODO),
	// and drops them on unload. Register ALWAYS — the factory is PER-CONFIG (in the metadata), so a
	// read-only DB load (onlyLoadFlag) must still register its OWN sources into its OWN factory.
	m_metaData->RegisterSource(&m_queryable);
	return true;
}

bool ibValueMetaObjectRegisterData::OnBeforeCloseMetaObject()   // un-resolve — mirror of OnRun's RegisterSource
{
	m_metaData->UnregisterSource(&m_queryable);
	return ibValueMetaObjectGenericData::OnBeforeCloseMetaObject();
}

bool ibValueMetaObjectRegisterData::OnAfterCloseMetaObject()
{
	if (!(*m_propertyAttributeLineActive)->OnAfterCloseMetaObject())
		return false;

	if (!(*m_propertyAttributePeriod)->OnAfterCloseMetaObject())
		return false;

	if (!(*m_propertyAttributeRecorder)->OnAfterCloseMetaObject())
		return false;

	if (!(*m_propertyAttributeLineNumber)->OnAfterCloseMetaObject())
		return false;

	unregisterManager();
	unregisterRecordKey();
	unregisterRecordSet();
	unregisterRecordSet_String();

	unregisterRecordManager();

	return ibValueMetaObject::OnAfterCloseMetaObject();
}

//***********************************************************************
//*								ARRAY									*
//***********************************************************************

ibValuePtr<ibValueRecordKeyObject> ibValueMetaObjectRegisterData::CreateRecordKeyObjectValue() const
{
	return ibValuePtr<ibValueRecordKeyObject>(new ibValueRecordKeyObject(this));
}

ibValuePtr<ibValueRecordKeyObject> ibValueMetaObjectRegisterData::CreateRecordKeyObjectValue(const ibRowMetaValues& keyValues) const
{
	return ibValuePtr<ibValueRecordKeyObject>(new ibValueRecordKeyObject(this, keyValues));
}

// Held before InitializeObject, as the record data objects are (the note above CreateObjectValue).
ibValuePtr<ibValueRecordSetObject> ibValueMetaObjectRegisterData::CreateRecordSetObjectValue(bool needInitialize) const
{
	const ibValuePtr<ibValueRecordSetObject> created(CreateRecordSetObjectRegValue());
	if (created != nullptr && needInitialize && !created->InitializeObject(nullptr, true))
		return nullptr;
	return created;
}

ibValuePtr<ibValueRecordSetObject> ibValueMetaObjectRegisterData::CreateRecordSetObjectValue(const ibUniqueKeyPair& uniqueKey, bool needInitialize) const
{
	const ibValuePtr<ibValueRecordSetObject> created(CreateRecordSetObjectRegValue(uniqueKey));
	if (created != nullptr && needInitialize && !created->InitializeObject(nullptr, false))
		return nullptr;
	return created;
}

ibValuePtr<ibValueRecordSetObject> ibValueMetaObjectRegisterData::CreateRecordSetObjectValue(ibValueRecordSetObject* source, bool needInitialize) const
{
	const ibValuePtr<ibValueRecordSetObject> created(CreateRecordSetObjectRegValue());
	if (created != nullptr && needInitialize && !created->InitializeObject(source, true))
		return nullptr;
	return created;
}

ibValuePtr<ibValueRecordSetObject> ibValueMetaObjectRegisterData::CopyRecordSetObjectValue(const ibUniqueKeyPair& uniqueKey)
{
	const ibValuePtr<ibValueRecordSetObject> created(CreateRecordSetObjectRegValue(uniqueKey));
	if (created != nullptr && !created->InitializeObject(nullptr, true))
		return nullptr;
	return created;
}

ibValuePtr<ibValueRecordManagerObject> ibValueMetaObjectRegisterData::CreateRecordManagerObjectValue() const
{
	const ibValuePtr<ibValueRecordManagerObject> created(CreateRecordManagerObjectRegValue());
	if (created != nullptr && !created->InitializeObject(nullptr, true))
		return nullptr;
	return created;
}

ibValuePtr<ibValueRecordManagerObject> ibValueMetaObjectRegisterData::CreateRecordManagerObjectValue(const ibUniqueKeyPair& uniqueKey) const
{
	const ibValuePtr<ibValueRecordManagerObject> created(CreateRecordManagerObjectRegValue(uniqueKey));
	if (created != nullptr && !created->InitializeObject(nullptr, false))
		return nullptr;
	return created;
}

ibValuePtr<ibValueRecordManagerObject> ibValueMetaObjectRegisterData::CreateRecordManagerObjectValue(ibValueRecordManagerObject* source) const
{
	const ibValuePtr<ibValueRecordManagerObject> created(CreateRecordManagerObjectRegValue());
	if (created != nullptr && !created->InitializeObject(source, true))
		return nullptr;
	return created;
}

ibValuePtr<ibValueRecordManagerObject> ibValueMetaObjectRegisterData::CopyRecordManagerObjectValue(const ibUniqueKeyPair& uniqueKey) const
{
	const ibValuePtr<ibValueRecordManagerObject> created(CreateRecordManagerObjectRegValue());
	if (created != nullptr && !created->InitializeObject(uniqueKey))
		return nullptr;
	return created;
}

//***********************************************************************
//*                        ibValueManagerDataObject						*
//***********************************************************************

// Manager-module methods. Surfaces the common module's exported methods through its
// runtime descriptor (ExportMethodsToHelper) rather than copying its whole helper
// table — the former CopyMethod wart. The bytecode-function index it keys on is
// exactly what CallAsProc/Func below feed back into pRefData, so dispatch lines up
// with no duplicated table.
void ibValueManagerDataObject::FillMembers(ibMemberTable& helper) const
{
	const ibValueMetaObjectGenericData* valueMetaObject = GetMetaObject();
	wxASSERT(valueMetaObject);

	const ibMetaData* metaData = valueMetaObject->GetMetaData();
	wxASSERT(metaData);

	// Manager module's compiled unit — designer mm in the Designer, session root
	// mm at runtime. FindCommonModule returns the typed descriptor (ibValueModuleUnit).
	auto* moduleManager = ibSession::EditModuleManagerFor(metaData);
	auto* pRefData = moduleManager ? moduleManager->FindCommonModule(GetManagerModule()) : nullptr;

	if (pRefData != nullptr)
		pRefData->ExportMethodsToHelper(&helper, g_aliasExport);
}

// ⭐⭐ THE MODULE IS ASKED BY THE NAME, AND ANSWERS WITH ITS OWN NUMBER. The entry's data is the
// function's ENTRY POINT in the bytecode (ExportMethodsToHelper writes `(long)fn`, m_lCodeLine), and
// the unit's CallAsFunc takes the index into ITS OWN export table and turns it back into a name. The
// two numbers agree for a function whose entry point happens to be the unit's index of it - the
// first function of a module, and nothing after it: a Public function written below a private helper
// was called as some other function or not at all, and the caller got Undefined without a word
// (the payroll demo's printed forms, 2026-09-10; a probe first in the module answered 42, the same
// probe last answered nothing). The name is the one fact both tables share.
//
// ⚠ AND A NAME THE MODULE DOES NOT HAVE IS SAID OUT LOUD. The runtime reads neither answer of a call
// (procUnit ignores CallAsFunc's bool), so a lookup that found nothing was the same silent Undefined
// the numbering gave — the one symptom that hid this defect for the whole life of the manager.
static long ibModuleMethodNum(const ibValue& unit, const wxString& methodName)
{
	const long found = unit.FindMethod(methodName);
	if (found == wxNOT_FOUND)
		ibBackendCoreException::Error(_("The manager module has no Public method '%s'"), methodName);
	return found;
}

bool ibValueManagerDataObject::CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray)
{
	const ibValueMetaObjectGenericData* valueMetaObject = GetMetaObject();
	wxASSERT(valueMetaObject);

	const ibMetaData* metaData = valueMetaObject->GetMetaData();
	wxASSERT(metaData);

	if (m_members.GetMethodAlias(lMethodNum) != g_aliasExport)
		return false;

	auto* moduleManager = ibSession::EditModuleManagerFor(metaData);
	auto* pRefData = moduleManager ? moduleManager->FindCommonModule(GetManagerModule()) : nullptr;

	if (pRefData != nullptr)
		return pRefData->CallAsProc(ibModuleMethodNum(*pRefData, m_members.GetMethodName(lMethodNum)), paParams, lSizeArray);

	return false;
}

bool ibValueManagerDataObject::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	const ibValueMetaObjectGenericData* valueMetaObject = GetMetaObject();
	wxASSERT(valueMetaObject);

	const ibMetaData* metaData = valueMetaObject->GetMetaData();
	wxASSERT(metaData);

	if (m_members.GetMethodAlias(lMethodNum) != g_aliasExport)
		return false;

	auto* moduleManager = ibSession::EditModuleManagerFor(metaData);
	auto* pRefData = moduleManager ? moduleManager->FindCommonModule(GetManagerModule()) : nullptr;

	if (pRefData != nullptr)
		return pRefData->CallAsFunc(ibModuleMethodNum(*pRefData, m_members.GetMethodName(lMethodNum)), pvarRetValue, paParams, lSizeArray);

	return false;
}

ibClassID ibValueManagerDataObject::GetClassType() const
{
	const ibValueMetaObjectGenericData* valueMetaObject = GetMetaObject();
	wxASSERT(valueMetaObject);

	const ibCtorMetaValueType* clsFactory =
		valueMetaObject->GetTypeCtor(ibCtorObjectMetaType::ibCtorObjectMetaType_Manager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassType();
}

wxString ibValueManagerDataObject::GetClassName() const
{
	const ibValueMetaObjectGenericData* valueMetaObject = GetMetaObject();
	wxASSERT(valueMetaObject);

	const ibCtorMetaValueType* clsFactory =
		valueMetaObject->GetTypeCtor(ibCtorObjectMetaType::ibCtorObjectMetaType_Manager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

ibString ibValueManagerDataObject::GetString() const
{
	const ibValueMetaObjectGenericData* valueMetaObject = GetMetaObject();
	wxASSERT(valueMetaObject);

	const ibCtorMetaValueType* clsFactory =
		valueMetaObject->GetTypeCtor(ibCtorObjectMetaType::ibCtorObjectMetaType_Manager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

// See the header for what this is for. The entry says who owns it; only the module's carry
// g_aliasExport, because that is the alias FillMembers hands ExportMethodsToHelper.
long ibValueManagerDataObject::BuiltinMethodNum(const long lMethodNum) const
{
	if (lMethodNum < 0 || lMethodNum >= m_members.GetNMethods())
		return wxNOT_FOUND;

	if (m_members.GetMethodAlias(lMethodNum) == g_aliasExport)
		return wxNOT_FOUND;

	long ordinal = 0;
	for (long i = 0; i < lMethodNum; ++i) {
		if (m_members.GetMethodAlias(i) != g_aliasExport)
			++ordinal;
	}
	return ordinal;
}

//***********************************************************************
//*                        ibValueManagerDataObjectPredefined			*
//***********************************************************************


// Predefined-value props. Composes ONTO FillMembers (both bound along the ctor
// chain), so no base call here — Build() runs FillMembers first, then this.
void ibValueManagerDataObjectPredefined::FillPredefined(ibMemberTable& helper) const
{
	const ibValueMetaObjectRecordDataHierarchyMutableRef* valueMetaObject = GetMetaObject();
	wxASSERT(valueMetaObject);

	//fill custom values
	for (const auto& object : valueMetaObject->GetPredefinedValueArray()) {
		helper.AppendProp(object->GetPredefinedName(), true, false);
	}
}

bool ibValueManagerDataObjectPredefined::SetPropVal(const long lPropNum, const ibValue& cValue)
{
	return false;   // predefined values are read-only; the props register as non-writable
}

bool ibValueManagerDataObjectPredefined::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	const ibValueMetaObjectRecordDataHierarchyMutableRef* valueMetaObject = GetMetaObject();
	wxASSERT(valueMetaObject);

	const auto& predefinedValue =
		valueMetaObject->FindPredefinedValue(m_members.GetPropName(lPropNum));
	if (predefinedValue == nullptr)
		return false;
	pvarPropVal = ibValueReferenceDataObject::Create(valueMetaObject, predefinedValue->GetPredefinedGuid());
	return true;
}

//***********************************************************************
//*                        ibValueRecordDataObject						*
//***********************************************************************


ibValueRecordDataObject::ibValueRecordDataObject(const ibGuid& objGuid, bool newObject) :
	ibValueDynamicMembers(ibValueTypes::TYPE_VALUE), ibValueDataObject(objGuid, newObject),
	ibRuntimeModuleDataObject(m_members, this)
{
	// Common data surface for every record leaf; leaves add their own methods. Module
	// exports autobind in the ibRuntimeModuleDataObject ctor (as the helper's tail).
	m_members.Bind(this, &ibValueRecordDataObject::FillDataMembers);
}

ibValueRecordDataObject::ibValueRecordDataObject(const ibValueRecordDataObject& source) :
	ibValueDynamicMembers(ibValueTypes::TYPE_VALUE), ibValueDataObject(wxNewUniqueGuid, true),
	ibRuntimeModuleDataObject(m_members, this)
{
	m_members.Bind(this, &ibValueRecordDataObject::FillDataMembers);
}

ibValueRecordDataObject::~ibValueRecordDataObject()
{
}

// MY form — the one showing THIS object. Found by SOURCE, not by form key: the key is the
// form's own identity and a caller may set it to anything (an element placed on the start page
// gets a fresh one of its own, so several copies never collide). What never changes is that
// the form's source object is me. Searching by the key only worked while it happened to fall
// back to the source guid — a coincidence, and one that broke the moment a caller supplied a
// key.
ibBackendValueForm* ibValueRecordDataObject::GetForm() const
{
	if (!m_objGuid.isValid())
		return nullptr;

	// ⭐ GETTING A FORM RAISES WHERE THERE ARE NONE, AND THAT IS CORRECT — this asks plainly and does
	// not soften the answer. The refusal belongs to whoever asked for a form; it is not this
	// function's business to decide that the asker did not really mean it
	// (Max, 2026-09-06: *"getting a form should indeed return an exception - that is normal"*).
	return ibBackendValueForm::FindFormBySourceUniqueKey(m_objGuid);
}

//----------------------------------------------------------------------
// Universal form-open trampolines (ShowFormValue / GetFormValue).
// Promoted from the per-leaf duplicates in HierarchyRef, Document,
// DataProcessor and Report. Per-leaf variation collapses to two
// virtual hooks (GetCurrentObjectFormID + OnFormCreated). See header.
//----------------------------------------------------------------------

void ibValueRecordDataObject::ShowFormValue(const ibFormRequest& request, ibBackendControlFrame* ownerControl)
{
	ibBackendValueForm* const foundedForm = GetForm();
	if (foundedForm && foundedForm->IsShown()) {
		foundedForm->ActivateForm();
		return;
	}

	ibBackendValueForm* const valueForm = GetFormValue(request, ownerControl);
	if (valueForm != nullptr) {
		valueForm->Modify(IsModified());
		valueForm->ShowForm();
	}
}

ibBackendValueForm* ibValueRecordDataObject::GetFormValue(const ibFormRequest& request, ibBackendControlFrame* ownerControl)
{
	ibBackendValueForm* const foundedForm = GetForm();
	if (foundedForm != nullptr)
		return foundedForm;

	// An object's window is keyed by the object: one window per object.
	ibFormRequest objectRequest = request;
	objectRequest.m_formGuid = m_objGuid;

	ibBackendValueForm* createdForm = GetMetaObject()->CreateAndBuildForm(
		objectRequest,
		GetCurrentObjectFormID(),
		ownerControl,
		this
	);
	// Ref-flavour leaves used to set CloseOnOwnerClose(false) per-leaf;
	// Ext (DataProcessor / Report) didn't. The flag is harmless when
	// the form has no owner-close interplay — set unconditionally to
	// keep the universal path simple.
	if (createdForm != nullptr)
		createdForm->CloseOnOwnerClose(false);
	return createdForm;
}

ibClassID ibValueRecordDataObject::GetClassType() const
{
	const ibValueMetaObjectRecordData* metaObject = GetMetaObject();
	wxASSERT(metaObject);
	const ibCtorMetaValueType* clsFactory =
		metaObject->GetTypeCtor(ibCtorObjectMetaType::ibCtorObjectMetaType_Object);
	wxASSERT(clsFactory);
	return clsFactory->GetClassType();
}

wxString ibValueRecordDataObject::GetClassName() const
{
	const ibValueMetaObjectRecordData* metaObject = GetMetaObject();
	wxASSERT(metaObject);
	const ibCtorMetaValueType* clsFactory =
		metaObject->GetTypeCtor(ibCtorObjectMetaType::ibCtorObjectMetaType_Object);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

ibString ibValueRecordDataObject::GetString() const
{
	const ibValueMetaObjectRecordData* metaObject = GetMetaObject();
	wxASSERT(metaObject);
	const ibCtorMetaValueType* clsFactory =
		metaObject->GetTypeCtor(ibCtorObjectMetaType::ibCtorObjectMetaType_Object);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}


const ibSourceExplorer* ibValueRecordDataObject::GetSourceExplorer() const
{
	const ibValueMetaObjectRecordData* metaObject = GetMetaObject();

	m_sourceExplorer.Reset(
		wxT("Ref"), _("Ref"), metaObject->GetMetaID(), GetClassType(),
		false, false
	);

	for (const auto object : metaObject->GetGenericAttributeArrayObject()) {
		m_sourceExplorer.AppendColumn(object->GetQueryColumn());
	}

	for (const auto object : metaObject->GetGenericTableArrayObject()) {
		if (object != nullptr && !object->IsDeleted()) {
			ibSourceExplorer& tblNode = m_sourceExplorer.AppendTable(object->GetName(), object->GetSynonym(), object->GetMetaID(), object->GetTypeDesc());
			for (ibValueMetaObjectAttributeBase* tblCol : object->GetGenericAttributeArrayObject()) tblNode.AppendColumn(tblCol->GetQueryColumn());
		}
	}

	return &m_sourceExplorer;
}

#include "backend/metaCollection/partial/tabularSection/tabularSection.h"

bool ibValueRecordDataObject::SetValueByMetaID(const ibMetaID& id, const ibValue& varMetaVal)
{
	auto it = m_listObjectValue.find(id);
	wxASSERT(it != m_listObjectValue.end());
	if (it != m_listObjectValue.end()) {

		const ibValueMetaObjectRecordData* metaObjectValue = GetMetaObject();
		wxASSERT(metaObjectValue);

		// Not found is an answer, not an assertion: the object carries a value for every attribute its
		// metatype declares, and one its owner has switched OFF is not found (FindObjectByFilter asks
		// IsAllowed) — nothing is written into a field that is not in use.
		const ibValueMetaObjectAttributeBase* attribute = metaObjectValue->FindAnyAttributeObjectByFilter(id);
		if (attribute == nullptr)
			return false;
		// ⭐ ADJUSTED THROUGH THE LINK, AND THE HOLDER IS THIS OBJECT. A field typed by a neighbour is
		// narrowed to what that neighbour holds RIGHT NOW — the same narrowing whether the value came
		// from a form, from a script or from a record written on the server (choiceLinkResolver.h).
		const ibValue settled = ibChoiceLinkResolver::Adjust(ibChoiceHolder(this), attribute, varMetaVal);
		const bool changed = !(it->second == settled);
		it->second = settled;

		// ⭐⭐ …AND WHAT WAS CHOSEN WITHIN THIS FIELD IS NOW STALE. A contract belongs to the
		// counterparty that was standing here; write another one — or empty this one — and the
		// contract is a contract with somebody the document no longer names. Emptying it is not
		// tidiness, it is the difference between a wrong value and no value.
		//
		// ⭐ HERE, BESIDE `Adjust`, BECAUSE A FIELD IS A FIELD wherever it is written. This lived on
		// two controls and so answered only for a person typing in a form: the same assignment from a
		// posting handler, from a script or from a record set left the stale value in place (measured
		// 2026-09-23 — a battery set the counterparty and the contract stayed).
		//
		// ⚠ WHICH MAKES THE ORDER OF ASSIGNMENTS MEAN SOMETHING IN CODE TOO. Filling a document
		// contract-first and counterparty-second now empties the contract, exactly as doing it in that
		// order in the form would. That is the rule, not an accident of it: what is chosen within
		// something is chosen AFTER it.
		//
		// 🛑⭐ ON A CHANGE, NOT ON A WRITE — and the difference is the whole of it. Writing the value
		// that is already there makes nothing stale, so clearing on every write punished the two most
		// ordinary things a person does: CHOOSING THE SAME ELEMENT AGAIN emptied the field beside it
		// (Max, 2026-09-23: "I re-pick the element, the same one, and it is removed altogether"), and
		// a save that re-assigns a row's own values wiped them on the way past.
		if (changed) {
			ibChoiceHolder holder(this);
			ibChoiceLinkResolver::ClearLinked(holder, id);
		}
		return true;
	}
	return false;
}

// THE ASSERT IS THE CONTRACT: every caller of this names a field of THIS object. It is not to be
// silenced or softened — when it fires, somebody asked the wrong object, and the fix belongs there.
// A caller that WALKS a path does not reach here for somebody else's field: the walk steps INTO the
// object that owns it (ibSourceDataObject::WalkColumns), so a table's column is asked of the table.
bool ibValueRecordDataObject::GetValueByMetaID(const ibMetaID& id, ibValue& pvarMetaVal) const
{
	auto it = m_listObjectValue.find(id);
	wxASSERT(it != m_listObjectValue.end());
	if (it != m_listObjectValue.end()) {
		pvarMetaVal = it->second;
		return true;
	}
	return false;
}

// ibSourceDataObject hop gate — reads the id, then filters the pin by the field's LIVE declared type
// (FindAnyAttributeObjectByFilter -> GetTypeDesc): a composite field's UNDEFINED resolves to the pinned twin,
// a field retyped away from the pin does not. Empty type (attribute not found) skips the check.
//
bool ibValueRecordDataObject::GetValueBySourceHop(const ibSourceHop& hop, ibValue& out) const
{
	const bool got = GetValueByMetaID(hop.m_id, out);
	const ibValueMetaObjectAttributeBase* attribute = GetMetaObject()->FindAnyAttributeObjectByFilter(hop.m_id);
	return ibValueReferenceDataObject::CoerceHopType(hop, out, attribute != nullptr ? attribute->GetTypeDesc() : ibTypeDescription(), GetSourceMetaData()) || got;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////

ibValueModel* ibValueRecordDataObject::GetTableByMetaID(const ibMetaID& id) const
{
	const ibValue& cTable = GetValueByMetaID(id); ibValueModel* retTable = nullptr;
	if (cTable.ConvertToValue(retTable))
		return retTable;
	return nullptr;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////

#define thisObject wxT("ThisObject")

void ibValueRecordDataObject::PrepareEmptyObject()
{
	const ibValueMetaObjectRecordData* metaObject = GetMetaObject();
	wxASSERT(metaObject);

	m_listObjectValue.clear();

	//attrbutes can refValue 
	for (const auto object : metaObject->GetGenericAttributeArrayObject()) {
		if (object->IsDeleted())
			continue;
		m_listObjectValue.insert_or_assign(object->GetMetaID(), object->CreateValue());
	}

	// table is collection values 
	for (const auto object : metaObject->GetGenericTableArrayObject()) {
		if (object->IsDeleted())
			continue;
		m_listObjectValue.insert_or_assign(object->GetMetaID(), new ibValueTabularSectionDataObject(this, object));
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////////////

// Fixed methods for leaves with no API of their own (DataProcessor, Report); they
// bind this. Leaves with their own method set bind their own FillMethods instead.
void ibValueRecordDataObject::FillBaseMethods(ibMemberTable& helper) const
{
	helper.AppendFunc(wxT("GetFormObject"), 2, wxT("GetFormObject(name : string, owner : any)"));
	helper.AppendFunc(wxT("GetTemplate"), 1, wxT("GetTemplate(name : string)"));
	helper.AppendFunc(wxT("GetMetadata"), wxT("GetMetadata()"));
}

// Shared data surface bound by the base ctor: the metaobject's attributes
// (eProperty) + tabular sections (eTable) + data-object module exports (eProcUnit).
// Attribute writability follows IsDataReference (a self-reference attribute is
// read-only; default false for non-reference metaobjects). ThisObject is bound via
// BindContextVariable in InitializeObject — no manual AppendProp.
void ibValueRecordDataObject::FillDataMembers(ibMemberTable& helper) const
{
	const ibValueMetaObjectRecordData* metaObject = GetMetaObject();
	wxASSERT(metaObject);
	if (metaObject == nullptr)
		return;

	wxString objectName;

	//fill custom attributes
	for (const auto object : metaObject->GetGenericAttributeArrayObject()) {
		if (object->IsDeleted())
			continue;
		if (!object->GetObjectNameAsString(objectName))
			continue;
		helper.AppendProp(
			objectName,
			true,
			!metaObject->IsReadOnlyAttribute(object->GetMetaID()),   // the reference is one of these
			object->GetMetaID(),
			eProperty
		);
	}

	//fill custom tables
	for (const auto object : metaObject->GetGenericTableArrayObject()) {
		if (object->IsDeleted())
			continue;
		if (!object->GetObjectNameAsString(objectName))
			continue;
		helper.AppendProp(
			objectName,
			true,
			false,
			object->GetMetaID(),
			eTable
		);
	}
	// Module exports surface via the descriptor autobind (ExportThunk), not here.
}

bool ibValueRecordDataObject::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	const long lPropAlias = m_members.GetPropAlias(lPropNum);
	if (lPropAlias == eProcUnit) {
		if (m_procUnit != nullptr) {
			return m_procUnit->SetPropVal(
				GetPropName(lPropNum), varPropVal
			);
		}
	}
	else if (lPropAlias == eProperty) {
		return SetValueByMetaID(
			m_members.GetPropData(lPropNum),
			varPropVal
		);
	}
	return false;
}

bool ibValueRecordDataObject::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	const long lPropAlias = m_members.GetPropAlias(lPropNum);
	if (lPropAlias == eProcUnit) {
		if (m_procUnit != nullptr) {
			return m_procUnit->GetPropVal(
				GetPropName(lPropNum), pvarPropVal
			);
		}
	}
	else if (lPropAlias == eProperty || lPropAlias == eTable) {
		return GetValueByMetaID(
			m_members.GetPropData(lPropNum), pvarPropVal
		);
	}
	return false;
}

bool ibValueRecordDataObject::CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray)
{
	const long lMethodAlias = m_members.GetPropAlias(lMethodNum);
	if (lMethodAlias == eProcUnit) {
		return ibRuntimeModuleDataObject::ExecAsProc(
			GetMethodName(lMethodNum), paParams, lSizeArray
		);
	}

	return false;
}

bool ibValueRecordDataObject::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	const long lMethodAlias = m_members.GetPropAlias(lMethodNum);
	if (lMethodAlias == eProcUnit) {
		return ibRuntimeModuleDataObject::ExecAsFunc(
			GetMethodName(lMethodNum), pvarRetValue, paParams, lSizeArray
		);
	}

	switch (lMethodNum)
	{
	case eGetFormObject:
		pvarRetValue = GetFormValue(
			lSizeArray > 0 ? ibFormRequest(paParams[0]->GetString()) : ibFormRequest(),
			lSizeArray > 1 ? paParams[1]->ConvertToType<ibBackendControlFrame>() : nullptr
		);
		return true;
	case enGetTemplate:
		pvarRetValue = GetMetaObject()->GetTemplate(paParams[0]->GetString());
		return true;
	case eGetMetadata:
		pvarRetValue = GetMetaObject();
		return true;
	}

	return false;
}

//***********************************************************************
//*                        ibValueRecordDataObjectExt							*           
//***********************************************************************


ibValueRecordDataObjectExt::ibValueRecordDataObjectExt(const ibValueMetaObjectRecordDataExt* metaObject) :
	ibValueRecordDataObject(wxNewUniqueGuid, true), m_metaObject(metaObject)
{
	// External data objects (DataProcessor / Report) expose only the fixed methods;
	// the data members come from the base FillDataMembers.
	m_members.Bind(this, &ibValueRecordDataObject::FillBaseMethods);
}

ibValueRecordDataObjectExt::ibValueRecordDataObjectExt(const ibValueRecordDataObjectExt& source) :
	ibValueRecordDataObject(source), m_metaObject(source.m_metaObject)
{
	m_members.Bind(this, &ibValueRecordDataObject::FillBaseMethods);
}

ibValueRecordDataObjectExt::~ibValueRecordDataObjectExt()
{
}

ibExternalOwnerHelper::~ibExternalOwnerHelper()
{
	if (m_externalMetadata != nullptr) {
		if (!m_externalMetadata->CloseDatabase(forceCloseFlag)) {
			wxASSERT_MSG(false, "external metadata CloseDatabase() == false");
		}
		wxDELETE(m_externalMetadata);
	}
}

bool ibValueRecordDataObjectExt::InitializeObject()
{
	if (!m_metaObject->IsExternalCreate()) {

		if (!m_metaObject->AccessRight_Use()) {
			ibBackendAccessException::Error(wxString::Format(_("using '%s'"), m_metaObject->GetSynonym()));
			return false;
		}

		ibValueModuleManager* moduleManager = ibSession::EditModuleManagerFor(m_metaObject->GetMetaData());
		wxASSERT(moduleManager);

		// Imperative: parent first, then lazy compile + runtime slot
		// pick up parent automatically on creation.
		ibRuntimeModuleDataObject::SetParent(moduleManager);
		BindContextVariable(thisObject, this);
		InitializeRuntime();

		try {
			Compile();
		}
		catch (const ibBackendException&) {
			if (!appData->DesignerMode())
				throw;
			return false;
		};
	}

	PrepareEmptyObject();

	if (!m_metaObject->IsExternalCreate())
		Run();

	InvalidateNames();

	//is Ok
	return true;
}

bool ibValueRecordDataObjectExt::InitializeObject(ibValueRecordDataObjectExt* source)
{
	if (!m_metaObject->IsExternalCreate()) {
		ibValueModuleManager* moduleManager = ibSession::EditModuleManagerFor(m_metaObject->GetMetaData());
		wxASSERT(moduleManager);

		ibRuntimeModuleDataObject::SetParent(moduleManager);
		BindContextVariable(thisObject, this);
		InitializeRuntime();

		try {
			Compile();
		}
		catch (const ibBackendException&) {
			if (!appData->DesignerMode())
				throw;
			return false;
		};
	}

	PrepareEmptyObject();

	if (!m_metaObject->IsExternalCreate())
		Run();

	InvalidateNames();

	//is Ok
	return true;
}

ibValuePtr<ibValueRecordDataObject> ibValueRecordDataObjectExt::CopyObjectValue()
{
	return m_metaObject->CreateObjectValue(this);
}

//***********************************************************************
//*                        ibValueRecordDataObjectRef							*           
//***********************************************************************


ibValueRecordDataObjectRef::ibValueRecordDataObjectRef(const ibValueMetaObjectRecordDataMutableRef* metaObject, const ibGuid& objGuid) :
	// A new object mints a PLAIN unique guid (pure identity — the type is carried by _RTRef / the metaObject,
	// never baked into the key). An existing object keeps its stored guid.
	ibValueRecordDataObject(objGuid.isValid() ? objGuid : ibGuid(ibGuid::newGuid(GUID_RANDOM)),
		!objGuid.isValid()),
	m_objModified(false),
	m_metaObject(metaObject),
	m_reference_impl(nullptr)
{
	if (m_metaObject != nullptr)
		m_reference_impl = new ibReference(m_objGuid);   // pure guid; type is the metaObject / _RTRef
}

ibValueRecordDataObjectRef::ibValueRecordDataObjectRef(const ibValueRecordDataObjectRef& src) :
	ibValueRecordDataObject(src),
	m_objModified(false),
	m_metaObject(src.m_metaObject),
	m_reference_impl(nullptr)
{
	if (m_metaObject != nullptr)
		m_reference_impl = new ibReference(m_objGuid);   // pure guid; type is the metaObject / _RTRef
}

ibValueRecordDataObjectRef::~ibValueRecordDataObjectRef()
{
	wxDELETE(m_reference_impl);
}

bool ibValueRecordDataObjectRef::InitializeObject(const ibGuid& copyGuid)
{
	if (!m_metaObject->AccessRight_Read()) {
		ibBackendAccessException::Error(wxString::Format(_("reading '%s'"), m_metaObject->GetSynonym()));
		return false;
	}

	// Parent the object's compile module to the module manager whose context it
	// should see — designer manager in the Designer, session root mm at runtime.
	ibValueModuleManager* moduleManager = ibSession::EditModuleManagerFor(m_metaObject->GetMetaData());
	wxASSERT(moduleManager);

	ibRuntimeModuleDataObject::SetParent(moduleManager);
	BindContextVariable(thisObject, this);
	try {
		Compile();
	}
	catch (const ibBackendException&) {
		if (!appData->DesignerMode())
			throw;
		return false;
	};

	bool succes = true;
	if (!appData->DesignerMode()) {
		if (m_newObject && !copyGuid.isValid()) {
			PrepareEmptyObject();
		}
		else if (m_newObject && copyGuid.isValid()) {
			succes = ReadData(copyGuid);
			if (succes) {
				ibValueMetaObjectAttributeBase* codeAttribute = m_metaObject->GetAttributeForCode();
				wxASSERT(codeAttribute);
				m_listObjectValue[codeAttribute->GetMetaID()] = codeAttribute->CreateValue();
			}
			m_objModified = true;
		}
		else {
			if (!ReadData()) PrepareEmptyObject();
		}
		if (!succes) return succes;
	}
	else {
		PrepareEmptyObject();
	}
	if (!appData->DesignerMode()) {
		wxASSERT(m_procUnit == nullptr);
		InitializeRuntime();
		m_procUnit->SetParent(moduleManager->GetProcUnit().get());
		Execute();
		if (m_newObject) {
			succes = Filling();
		}
	}

	InvalidateNames();

	//is Ok
	return succes;
}

bool ibValueRecordDataObjectRef::InitializeObject(ibValueRecordDataObjectRef* source, bool generate)
{
	ibValueModuleManager* moduleManager = ibSession::EditModuleManagerFor(m_metaObject->GetMetaData());
	wxASSERT(moduleManager);

	ibRuntimeModuleDataObject::SetParent(moduleManager);
	BindContextVariable(thisObject, this);

	try {
		Compile();
	}
	catch (const ibBackendException&) {
		if (!appData->DesignerMode())
			throw;
		return false;
	};

	if (!generate && source != nullptr)
		PrepareEmptyObject(source);
	else
		PrepareEmptyObject();

	bool succes = true;
	if (!appData->DesignerMode()) {
		wxASSERT(m_procUnit == nullptr);
		InitializeRuntime();
		// Re-apply descriptor SetParent so cascade picks up the now-
		// existent procUnit and wires its parent too. Compile-side
		// parent is already set from the first call above — rewriting
		// to the same value.
		ibRuntimeModuleDataObject::SetParent(moduleManager);
		Execute();
		// OnCopy / Filling run user script with side effects. Skip them under a
		// debugger watch/eval, the same way BeginWriteScope/BeginDeleteScope skip
		// eval — evaluating a watch must not fire user handlers. (DesignerMode is
		// already excluded by the enclosing guard.)
		// …and the sandbox is the evaluation that MAY fire them: a document filled by nothing is not
		// the document the person would have got, so a measurement taken on it measures the wrong
		// thing. Watches still skip, which is what this guard was written for.
		if (!ibBackendException::IsEvalMode() || ibBackendException::IsEvalSandbox()) {
			if (m_newObject && source != nullptr && !generate) {
				ExecAsProc(wxT("OnCopy"), source->GetValue());
			}
			else if (m_newObject && source == nullptr) {
				succes = Filling();
			}
			else if (generate) {
				ibValuePtr<ibValueReferenceDataObject> refPtr(
					source != nullptr ? source->GetReference() : nullptr);
				succes = Filling(refPtr);
			}
		}
	}

	InvalidateNames();

	//is Ok
	return succes;
}

ibClassID ibValueRecordDataObjectRef::GetClassType() const
{
	return ibValueRecordDataObject::GetClassType();
}

wxString ibValueRecordDataObjectRef::GetClassName() const
{
	return ibValueRecordDataObject::GetClassName();
}

ibString ibValueRecordDataObjectRef::GetString() const
{
	// …and a metatype that has nothing to say leaves the string empty, which is its own answer.
	wxString desc;
	m_metaObject->GenerateDataDesc(this, desc);
	return desc;
}

const ibSourceExplorer* ibValueRecordDataObjectRef::GetSourceExplorer() const
{
	m_sourceExplorer.Reset(
		wxT("Ref"), _("Ref"), m_metaObject->GetMetaID(), GetClassType(),
		false, false
	);

	ibValueMetaObjectAttributeBase* attribute = m_metaObject->GetAttributeForCode();

	for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
		if (!m_metaObject->IsDataReference(object->GetMetaID())) {
			m_sourceExplorer.AppendColumn(object->GetQueryColumn(), object != attribute);
		}
	}

	for (const auto object : m_metaObject->GetGenericTableArrayObject()) {
		if (object != nullptr && !object->IsDeleted()) {
			ibSourceExplorer& tblNode = m_sourceExplorer.AppendTable(object->GetName(), object->GetSynonym(), object->GetMetaID(), object->GetTypeDesc());
			for (ibValueMetaObjectAttributeBase* tblCol : object->GetGenericAttributeArrayObject()) tblNode.AppendColumn(tblCol->GetQueryColumn());
		}
	}

	return &m_sourceExplorer;
}

void ibValueRecordDataObjectRef::Modify(bool mod)
{
	// ⭐⭐ THE FAULT IS HERE, NOT IN THE FORM LOOKUP. Getting a form raises where there are none, and
	// that is right; what is wrong is that MARKING AN OBJECT MODIFIED went looking for one at all.
	// This runs on EVERY field assignment, so `usd.Code = "840"` came through here before any write
	// did — MEASURED 2026-09-06 on the first line of a hundred-payment fill, which refused at
	// JobCode line 2 and left a background job, a daemon and codeRunner unable to assign a field.
	//
	// So the refusal is caught HERE, where the question was asked wrongly, rather than softened
	// THERE, where it is answered correctly (Max, 2026-09-06: *"the problem is not in getting the
	// form - it is exactly that modifiedness tries to get the form; wrap that in try/catch"*).
	//
	// The object is modified either way — that is DATA, and it does not depend on anybody being
	// there to watch it happen.
	ibBackendValueForm* const foundedForm =
		ibFormToNotify([this] { return ibBackendValueForm::FindFormBySourceUniqueKey(m_objGuid); });
	if (foundedForm != nullptr)
		foundedForm->Modify(mod);

	m_objModified = mod;
}

bool ibValueRecordDataObjectRef::Generate()
{
	if (m_newObject)
		return false;

	ibBackendValueForm* const foundedForm = ibBackendValueForm::FindFormBySourceUniqueKey(m_objGuid);
	if (foundedForm != nullptr)
		return foundedForm->GenerateForm(this);

	return false;
}

bool ibValueRecordDataObjectRef::Filling(ibValue cValue) const
{
	ibValue standartProcessing = true;
	ExecAsProc(wxT("Filling"), cValue, standartProcessing);
	return standartProcessing.GetBoolean();
}

bool ibValueRecordDataObjectRef::SetValueByMetaID(const ibMetaID& id, const ibValue& varMetaVal)
{
	// 🛑 WHETHER IT CHANGED IS READ OFF THE FIELD AFTER THE WRITE, not guessed from the raw value before
	// it: the base narrows the value by its link, so "the same raw value" and "the same stored value" are
	// different questions, and asking the first one skipped the narrowing of an empty field into the
	// empty value of its settled type. See the same repair on the tabular section's own write.
	const ibValue before = ibValueRecordDataObject::GetValueByMetaID(id);
	if (!ibValueRecordDataObject::SetValueByMetaID(id, varMetaVal))
		return false;
	// …compared with the value where it is stored, not with a second copy of it: this is every write.
	const auto stored = m_listObjectValue.find(id);
	if (stored == m_listObjectValue.end() || !(before == stored->second))
		ibValueRecordDataObjectRef::Modify(true);
	return true;
}

bool ibValueRecordDataObjectRef::GetValueByMetaID(const ibMetaID& id, ibValue& pvarMetaVal) const
{
	if (m_metaObject->IsDataReference(id)) {
		pvarMetaVal = GetReference();
		return true; 
	}

	return ibValueRecordDataObject::GetValueByMetaID(id, pvarMetaVal);
}

ibValuePtr<ibValueRecordDataObject> ibValueRecordDataObjectRef::CopyObjectValue()
{
	return m_metaObject->CreateObjectValue(this);
}

void ibValueRecordDataObjectRef::PrepareEmptyObject()
{
	m_listObjectValue.clear();
	//attrbutes can refValue 
	for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
		if (object->IsDeleted())
			continue;
		if (!m_metaObject->IsDataReference(object->GetMetaID())) {
			m_listObjectValue.insert_or_assign(object->GetMetaID(), object->CreateValue());
		}
	}
	// table is collection values 
	for (const auto object : m_metaObject->GetGenericTableArrayObject()) {
		if (object->IsDeleted())
			continue;
		m_listObjectValue.insert_or_assign(object->GetMetaID(), new ibValueTabularSectionDataObjectRef(this, object));
	}
	m_objModified = true;
}

void ibValueRecordDataObjectRef::PrepareEmptyObject(const ibValueRecordDataObjectRef* source)
{
	m_listObjectValue.clear();

	ibValueMetaObjectAttributeBase* codeAttribute = m_metaObject->GetAttributeForCode();
	wxASSERT(codeAttribute);

	//attributes can refValue 
	for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
		if (object->IsDeleted())
			continue;
		if (object != codeAttribute && !m_metaObject->IsDataReference(object->GetMetaID())) {
			source->GetValueByMetaID(object->GetMetaID(), m_listObjectValue[object->GetMetaID()]);
		}
	}

	m_listObjectValue[codeAttribute->GetMetaID()] = codeAttribute->CreateValue();

	// table is collection values 
	for (const auto object : m_metaObject->GetGenericTableArrayObject()) {
		if (object->IsDeleted())
			continue;
		ibValueTabularSectionDataObjectRef* tableSection = new ibValueTabularSectionDataObjectRef(this, object);
		if (tableSection->LoadDataFromTable(source->GetTableByMetaID(object->GetMetaID())))
			m_listObjectValue.insert_or_assign(object->GetMetaID(), tableSection);
		else
			wxDELETE(tableSection);
	}
	m_objModified = true;
}

ibValueReferenceDataObject* ibValueRecordDataObjectRef::GetReference() const
{
	if (m_newObject) {
		return ibValueReferenceDataObject::Create(m_metaObject);
	}

	return ibValueReferenceDataObject::Create(m_metaObject, m_objGuid);
}

//***********************************************************************
//*                        ibValueRecordDataObjectHierarchyRef					*
//***********************************************************************

// Hierarchy axis (Catalog / ChartOf* / Enumeration) — see commonObject.h for
// the rationale. Class identity comes from the metaobject's clsFactory via
// GetClassType(); no RTTI macro is involved (wxRTTI was dropped in Phase 3).

ibValueRecordDataObjectHierarchyRef::ibValueRecordDataObjectHierarchyRef(const ibValueMetaObjectRecordDataHierarchyMutableRef* metaObject, const ibGuid& objGuid, ibObjectMode objMode)
	: ibValueRecordDataObjectRef(metaObject, objGuid), m_objMode(objMode)
{
}

ibValueRecordDataObjectHierarchyRef::ibValueRecordDataObjectHierarchyRef(const ibValueRecordDataObjectHierarchyRef& source)
	: ibValueRecordDataObjectRef(source), m_objMode(source.m_objMode)
{
}

ibValueRecordDataObjectHierarchyRef::~ibValueRecordDataObjectHierarchyRef()
{
}

const ibSourceExplorer* ibValueRecordDataObjectHierarchyRef::GetSourceExplorer() const
{
	m_sourceExplorer.Reset(
		wxT("Ref"), _("Ref"), m_metaObject->GetMetaID(), GetClassType(),
		false, false
	);
	ibValueMetaObjectAttributeBase* attribute = m_metaObject->GetAttributeForCode();
	for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
		ibItemMode attrUse = object->GetItemMode();
		if (m_objMode == ibObjectMode::OBJECT_ITEM) {
			if (attrUse == ibItemMode::ibItemMode_Item
				|| attrUse == ibItemMode::ibItemMode_Folder_Item) {
				if (!m_metaObject->IsDataReference(object->GetMetaID())) {
					m_sourceExplorer.AppendColumn(object->GetQueryColumn(), object != attribute);
				}
			}
		}
		else {
			if (attrUse == ibItemMode::ibItemMode_Folder ||
				attrUse == ibItemMode::ibItemMode_Folder_Item) {
				if (!m_metaObject->IsDataReference(object->GetMetaID())) {
					m_sourceExplorer.AppendColumn(object->GetQueryColumn(), object != attribute);
				}
			}
		}
	}

	for (const auto object : m_metaObject->GetGenericTableArrayObject()) {
		ibItemMode tableUse = object->GetTableUse();
		if (m_objMode == ibObjectMode::OBJECT_ITEM) {
			if (tableUse == ibItemMode::ibItemMode_Item
				|| tableUse == ibItemMode::ibItemMode_Folder_Item) {
				if (object != nullptr && !object->IsDeleted()) {
					ibSourceExplorer& tblNode = m_sourceExplorer.AppendTable(object->GetName(), object->GetSynonym(), object->GetMetaID(), object->GetTypeDesc());
					for (ibValueMetaObjectAttributeBase* tblCol : object->GetGenericAttributeArrayObject()) tblNode.AppendColumn(tblCol->GetQueryColumn());
				}
			}
		}
		else {
			if (tableUse == ibItemMode::ibItemMode_Folder ||
				tableUse == ibItemMode::ibItemMode_Folder_Item) {
				if (object != nullptr && !object->IsDeleted()) {
					ibSourceExplorer& tblNode = m_sourceExplorer.AppendTable(object->GetName(), object->GetSynonym(), object->GetMetaID(), object->GetTypeDesc());
					for (ibValueMetaObjectAttributeBase* tblCol : object->GetGenericAttributeArrayObject()) tblNode.AppendColumn(tblCol->GetQueryColumn());
				}
			}
		}
	}

	return &m_sourceExplorer;
}

ibValuePtr<ibValueRecordDataObject> ibValueRecordDataObjectHierarchyRef::CopyObjectValue()
{
	return GetMetaObject()->CreateObjectValue(m_objMode, this);
}

bool ibValueRecordDataObjectHierarchyRef::SetValueByMetaID(const ibMetaID& id, const ibValue& varMetaVal)
{
	const ibValue& cOldValue = ibValueRecordDataObjectRef::GetValueByMetaID(id);
	if (cOldValue.GetType() == TYPE_NULL)
		return false;

	const ibValueMetaObjectRecordDataHierarchyMutableRef* valueMetaObject = GetMetaObject();
	wxASSERT(valueMetaObject);

	if (valueMetaObject->IsDataParent(id) && varMetaVal == GetReference() && !varMetaVal.IsEmpty()) {
		ibBackendCoreException::Error(_("You can't change your parent to yourself!"));
		return false;
	}

	if (valueMetaObject->IsDataPredefinedName(id)) {
		ibBackendCoreException::Error(_("You cannot change predefined value!"));
		return false;
	}

	return ibValueRecordDataObjectRef::SetValueByMetaID(id, varMetaVal);
}

bool ibValueRecordDataObjectHierarchyRef::GetValueByMetaID(const ibMetaID& id, ibValue& pvarMetaVal) const
{
	return ibValueRecordDataObjectRef::GetValueByMetaID(id, pvarMetaVal);
}

void ibValueRecordDataObjectHierarchyRef::PrepareEmptyObject()
{
	m_listObjectValue.clear();
	//attrbutes can refValue
	for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
		if (object->IsDeleted())
			continue;
		ibItemMode attrUse = object->GetItemMode();
		if (m_objMode == ibObjectMode::OBJECT_ITEM) {
			if (attrUse == ibItemMode::ibItemMode_Item ||
				attrUse == ibItemMode::ibItemMode_Folder_Item) {
				m_listObjectValue.insert_or_assign(object->GetMetaID(), object->CreateValue());
			}
			else {
				m_listObjectValue.insert_or_assign(object->GetMetaID(), ibValueTypes::TYPE_NULL);
			}
		}
		else {
			if (attrUse == ibItemMode::ibItemMode_Folder ||
				attrUse == ibItemMode::ibItemMode_Folder_Item) {
				m_listObjectValue.insert_or_assign(object->GetMetaID(), object->CreateValue());
			}
			else {
				m_listObjectValue.insert_or_assign(object->GetMetaID(), ibValueTypes::TYPE_NULL);
			}
		}
	}
	const ibValueMetaObjectRecordDataHierarchyMutableRef* metaFolder = GetMetaObject();
	wxASSERT(metaFolder);
	if (m_objMode == ibObjectMode::OBJECT_ITEM) {
		m_listObjectValue.insert_or_assign(*metaFolder->GetDataIsFolder(), false);
	}
	else if (m_objMode == ibObjectMode::OBJECT_FOLDER) {
		m_listObjectValue.insert_or_assign(*metaFolder->GetDataIsFolder(), true);
	}
	// table is collection values 
	for (const auto object : m_metaObject->GetGenericTableArrayObject()) {
		if (object->IsDeleted())
			continue;
		ibItemMode tableUse = object->GetTableUse();
		if (m_objMode == ibObjectMode::OBJECT_ITEM) {
			if (tableUse == ibItemMode::ibItemMode_Item ||
				tableUse == ibItemMode::ibItemMode_Folder_Item) {
				m_listObjectValue.insert_or_assign(object->GetMetaID(), new ibValueTabularSectionDataObjectRef(this, object));
			}
			else {
				m_listObjectValue.insert_or_assign(object->GetMetaID(), ibValueTypes::TYPE_NULL);
			}
		}
		else {
			if (tableUse == ibItemMode::ibItemMode_Folder ||
				tableUse == ibItemMode::ibItemMode_Folder_Item) {
				m_listObjectValue.insert_or_assign(object->GetMetaID(), new ibValueTabularSectionDataObjectRef(this, object));
			}
			else {
				m_listObjectValue.insert_or_assign(object->GetMetaID(), ibValueTypes::TYPE_NULL);
			}
		}
	}
	m_objModified = true;
}

void ibValueRecordDataObjectHierarchyRef::PrepareEmptyObject(const ibValueRecordDataObjectRef* source)
{
	m_listObjectValue.clear();
	ibValueMetaObjectAttributeBase* codeAttribute = m_metaObject->GetAttributeForCode();
	wxASSERT(codeAttribute);
	m_listObjectValue[codeAttribute->GetMetaID()] = codeAttribute->CreateValue();
	//attributes can refValue 
	for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
		if (object->IsDeleted())
			continue;
		if (object != codeAttribute && !m_metaObject->IsDataReference(object->GetMetaID())) {
			source->GetValueByMetaID(object->GetMetaID(), m_listObjectValue[object->GetMetaID()]);
		}
	}
	const ibValueMetaObjectRecordDataHierarchyMutableRef* metaFolder = GetMetaObject();
	wxASSERT(metaFolder);
	if (m_objMode == ibObjectMode::OBJECT_ITEM) {
		m_listObjectValue.insert_or_assign(*metaFolder->GetDataIsFolder(), false);
	}
	else if (m_objMode == ibObjectMode::OBJECT_FOLDER) {
		m_listObjectValue.insert_or_assign(*metaFolder->GetDataIsFolder(), true);
	}
	// table is collection values 
	for (const auto object : m_metaObject->GetGenericTableArrayObject()) {
		if (object->IsDeleted())
			continue;
		ibValueTabularSectionDataObjectRef* tableSection = new ibValueTabularSectionDataObjectRef(this, object);
		if (tableSection->LoadDataFromTable(source->GetTableByMetaID(object->GetMetaID())))
			m_listObjectValue.insert_or_assign(object->GetMetaID(), tableSection);
		else
			wxDELETE(tableSection);
	}
	m_objModified = true;
}


//----------------------------------------------------------------------
// Phase B — template-method Write/Delete on
// ibValueRecordDataObjectHierarchyRef. The 3 hierarchy-mutable-ref
// leaves (Catalog / ChartOfAccounts / ChartOfCharacteristicTypes)
// share byte-identical pipelines mod class-name qualification on
// IsNewObject / GenerateUniqueIdentifier / ResetUniqueIdentifier;
// virtual dispatch lets the same body serve all three.
//----------------------------------------------------------------------

bool ibValueRecordDataObjectHierarchyRef::WriteObject()
{
	ibConnectionScope scope = ibSession::Current()->OpenConnectionScope();
	if (!BeginWriteScope(scope)) return true;
	ibWriteScope objectScope(*this);   // a refusal below leaves the object as it was (commonObject.h)

	// Asked only so an OPEN window can be told afterwards — see ibFormToNotify (backend_form.h).
	// A server has none, and that is not a reason for a write to fail.
	ibBackendValueForm* const valueForm = ibFormToNotify([this] { return GetForm(); });
	const bool newObject = IsNewObject();

	// Stage-named failures — same rule as the recorder path: the message says which stage
	// stopped the write and on which object, and a script cancel reads as a cancel. A refusal is the
	// exception and nothing else: the connection scope rolls back, the object scope puts the object back.
	const auto refuse = [this](const wxString& stage) -> bool {
		ibBackendCoreException::Error(stage, GetSourceCaption());
		return false;
	};

	{
		ibValue cancel = false;
		ExecAsProc(wxT("BeforeWrite"), cancel);
		if (cancel.GetBoolean())
			return refuse(_("%s: writing cancelled by the BeforeWrite handler"));
	}

	if (!IsSetUniqueIdentifier()) {
		ibValue prefix = wxEmptyString, standartProcessing = true;
		ExecAsProc(wxT("SetNewCode"), prefix, standartProcessing);
		if (standartProcessing.GetBoolean())
			GenerateUniqueIdentifier(prefix.GetString());
	}

	if (!SaveData())
		return refuse(_("%s: failed to save the object data"));

	{
		ibValue cancel = false;
		ExecAsProc(wxT("OnWrite"), cancel);
		if (cancel.GetBoolean())
			return refuse(_("%s: writing cancelled by the OnWrite handler"));
	}

	CommitWriteScope(scope, objectScope, valueForm, newObject);
	return true;
}

bool ibValueRecordDataObjectHierarchyRef::DeleteObject()
{
	// Predefined-guard fires before the scope — pure policy check that
	// doesn't need a TX. DesignerMode is checked first so the predefined
	// lookup itself doesn't run during metadata editing.
	if (!appData->DesignerMode()) {
		const ibValueMetaObjectRecordDataHierarchyMutableRef* valueMetaObject = GetMetaObject();
		wxASSERT(valueMetaObject);
		const ibGuid& objGuid = GetGuid();
		if (valueMetaObject->FindPredefinedValue(objGuid) != nullptr) {
			ibBackendCoreException::Error(_("%s cannot be deleted: it is a predefined element"),
				GetSourceCaption());
			return false;
		}
	}

	ibConnectionScope scope = ibSession::Current()->OpenConnectionScope();
	if (!BeginDeleteScope(scope)) return true;

	// Asked only so an OPEN window can be told afterwards — see ibFormToNotify (backend_form.h).
	// A server has none, and that is not a reason for a write to fail.
	ibBackendValueForm* const valueForm = ibFormToNotify([this] { return GetForm(); });

	{
		ibValue cancel = false;
		ExecAsProc(wxT("BeforeDelete"), cancel);
		if (cancel.GetBoolean()) {
			scope.SafeRollBackTransaction();
			ibBackendCoreException::Error(_("%s: deletion cancelled by the BeforeDelete handler"),
				GetSourceCaption());
			return false;
		}
	}

	if (!DeleteData()) {
		scope.SafeRollBackTransaction();
		ibBackendCoreException::Error(_("%s: failed to delete the object data"), GetSourceCaption());
		return false;
	}

	{
		ibValue cancel = false;
		ExecAsProc(wxT("OnDelete"), cancel);
		if (cancel.GetBoolean()) {
			scope.SafeRollBackTransaction();
			ibBackendCoreException::Error(_("%s: deletion cancelled by the OnDelete handler"),
				GetSourceCaption());
			return false;
		}
	}

	CommitDeleteScope(scope, valueForm);
	return true;
}


//*********************************************************************************************
//*           ibValueRecordDataObjectRecorderRef::ibRecorderRegister	                      *
//*********************************************************************************************
// Per-recorder register holder. Iterates the leaf metaobject's
// RecordDescription, creates one ibValueRecordSetObject per declared
// register seeded with this recorder's reference, fans Write/Delete
// across all of them. Promoted from being nested in Document with the
// Phase B-Recorder split. Leaf-metaobject access stays generic via
// the GetRecordDescription virtual hook on RecorderRef.


void ibValueRecordDataObjectRecorderRef::ibRecorderRegister::CreateRecordSet()
{
	// The list this holder is made of — the recorder answers by it.
	const ibMetaDescription* metaDesc = m_recorder->GetRecordDescription(m_writes);
	if (metaDesc == nullptr) return;   // recorder without static description — no cascade
	const ibMetaData* metaData = m_recorder->GetMetaObject()->GetMetaData();
	wxASSERT(metaData);

	ibRecorderRegister::ClearRecordSet();

	for (unsigned int idx = 0; idx < metaDesc->GetTypeCount(); idx++) {
		const ibValueMetaObjectRegisterData* metaObject = metaData->FindAnyObjectByFilter<ibValueMetaObjectRegisterData>(metaDesc->GetByIdx(idx));
		if (metaObject == nullptr || !metaObject->IsAllowed())
			continue;
		ibValueMetaObjectAttributePredefined* registerRecord = metaObject->GetRegisterRecorder();
		wxASSERT(registerRecord);
		ibValuePtr<ibValueRecordSetObject> recordSet(metaObject->CreateRecordSetObjectValue());
		recordSet->SetKeyValue(registerRecord->GetMetaID(), m_recorder->GetReference());
		m_records.insert_or_assign(metaObject->GetMetaID(), recordSet);
	}

	InvalidateNames();
}

bool ibValueRecordDataObjectRecorderRef::ibRecorderRegister::WriteRecordSet()
{
	for (auto& pair : m_records) {
		ibValueRecordSetObject* record = pair.second;
		wxASSERT(record);

		// 🛑 A SET THE HANDLER ALREADY WROTE MUST NOT BE WRITTEN AGAIN — because this write REPLACES:
		// it deletes everything under the recorder and inserts what the set holds. A handler that
		// wrote its own movements (a legitimate thing to do - a later step may need to read them
		// back) leaves the set EMPTY behind it, and this pass then deleted the very rows it had just
		// stored: the document posted, and its movements were gone (measured 2026-09-03 in the
		// journal - DELETE, INSERT, then DELETE with no INSERT).
		//
		// ⭐ MODIFIED IS THE QUESTION, and the set answers it: CommitRecordSetScope clears the flag
		// on a successful write, so "not modified" means "already in the database as it stands". A
		// set the handler filled - or CLEARED, which is how movements are taken away - is modified
		// and is written here.
		if (!record->IsModified())
			continue;

		// The register's OWN exception is the informative one — it names the line, the required
		// attribute, the lock conflict, the access deny. It travels UP intact instead of being
		// flattened into a bare false that the recorder above can only report as "failed to post":
		// the whole point of this pass is that the user learns WHICH register refused and WHY.
		// The write scope's dtor rolls back the unmatched Begin, so leaving through a throw is safe.
		if (!record->WriteRecordSet())
			ibBackendCoreException::Error(_("Failed to write the movements of register '%s'"),
				record->GetMetaObject()->GetSynonym());
	}
	return true;
}

bool ibValueRecordDataObjectRecorderRef::ibRecorderRegister::DeleteRecordSet()
{
	// A deleted document asks nothing (Max, 2026-09-15): no document, no movements of it.
	for (auto& pair : m_records) {
		ibValueRecordSetObject* record = pair.second;
		wxASSERT(record);
		// Same as WriteRecordSet — the register speaks for itself; this only names the one that
		// returned a plain false without saying anything.
		if (!record->DeleteRecordSet())
			ibBackendCoreException::Error(_("Failed to clear the movements of register '%s'"),
				record->GetMetaObject()->GetSynonym());
	}
	return true;
}

bool ibValueRecordDataObjectRecorderRef::ibRecorderRegister::DeleteRecordSet(ibDocumentWriteMode writeMode)
{
	// ⭐ THE ONE PLACE A POSTING'S MOVEMENTS ARE CLEARED DECIDES WHETHER (Max, 2026-09-14) — the owner's
	// RegisterRecordsDeletion: before a posting again only when Automatically; the posting undone — unless Never.
	const ibDocumentRecordsDeletion deletion = m_recorder->GetMetaObject()->GetRegisterRecordsDeletion();
	if (writeMode != ibDocumentWriteMode::ibDocumentWriteMode_Posting)
		return deletion == ibDocumentRecordsDeletion::ibDocumentRecordsDeletion_Never || DeleteRecordSet();
	if (deletion != ibDocumentRecordsDeletion::ibDocumentRecordsDeletion_Automatically)
		return true;
	for (auto& pair : m_records) {
		ibValueRecordSetObject* record = pair.second;
		wxASSERT(record);
		// The mirror of WriteRecordSet's rule: a set that says it is modified holds rows somebody put there,
		// and it replaces what is stored when it is written — the delete would only take its flag away.
		if (record->IsModified())
			continue;
		if (!record->DeleteRecordSet())
			ibBackendCoreException::Error(_("Failed to clear the movements of register '%s'"),
				record->GetMetaObject()->GetSynonym());
	}
	return true;
}

void ibValueRecordDataObjectRecorderRef::ibRecorderRegister::ClearRecordSet()
{
	m_records.clear();
}

void ibValueRecordDataObjectRecorderRef::ibRecorderRegister::RefreshRecordSet()
{
	for (auto& pair : m_records) {
		ibValueRecordSetObject* record = pair.second;
		wxASSERT(record);
		const ibValueMetaObjectRegisterData* object = record->GetMetaObject();
		wxASSERT(object);
		// Refreshing an open register list after a document wrote its movements — pure notification,
		// and there is nothing to refresh in a process with no windows (ibFormToNotify,
		// backend_form.h). MEASURED 2026-09-06: this is what stopped a document write on a server
		// AFTER the row had already been created and logged.
		ibBackendValueForm* backendFrame =
			ibFormToNotify([object] { return ibBackendValueForm::FindFormBySourceUniqueKey(object->GetGuid()); });
		if (backendFrame != nullptr) backendFrame->UpdateForm();
	}
}

ibValueRecordDataObjectRecorderRef::ibRecorderRegister::ibRecorderRegister(ibValueRecordDataObjectRecorderRef* recorder, ibRecorderWrites of) :
	ibValueDynamicMembers(ibValueTypes::TYPE_VALUE), m_recorder(recorder), m_writes(of)
{
	m_members.Bind(this, &ibRecorderRegister::FillMembers);
	ibRecorderRegister::CreateRecordSet();
}

ibValueRecordDataObjectRecorderRef::ibRecorderRegister::~ibRecorderRegister()
{
	ibRecorderRegister::ClearRecordSet();
}

namespace { enum { enWriteRegister = 0 }; }

void ibValueRecordDataObjectRecorderRef::ibRecorderRegister::FillMembers(ibMemberTable& helper) const
{
	helper.AppendFunc(wxT("Write"), wxT("Write()"));
	for (auto& pair : m_records) {
		ibValueRecordSetObject* record = pair.second;
		wxASSERT(record);
		const ibValueMetaObjectRegisterData* metaObject = record->GetMetaObject();
		wxASSERT(metaObject);
		helper.AppendProp(metaObject->GetName(), true, false, pair.first);
	}
}

bool ibValueRecordDataObjectRecorderRef::ibRecorderRegister::SetPropVal(const long /*lPropNum*/, const ibValue& /*varPropVal*/)
{
	return false;
}

bool ibValueRecordDataObjectRecorderRef::ibRecorderRegister::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	auto it = m_records.find(m_members.GetPropData(lPropNum));
	if (it != m_records.end()) {
		pvarPropVal = it->second;
		return true;
	}
	return false;
}

bool ibValueRecordDataObjectRecorderRef::ibRecorderRegister::CallAsFunc(const long lMethodNum, ibValue& /*pvarRetValue*/, ibValue** /*paParams*/, const long /*lSizeArray*/)
{
	switch (lMethodNum) {
	case enWriteRegister:
		WriteRecordSet();
		return true;
	}
	return false;
}

//*********************************************************************************************
//*               ibValueRecordDataObjectRecorderRef — ctors + scaffold                       *
//*********************************************************************************************

ibValueRecordDataObjectRecorderRef::ibValueRecordDataObjectRecorderRef(
	const ibValueMetaObjectRecordDataRecorderRef* metaObject, const ibGuid& objGuid) :
	ibValueRecordDataObjectRef(metaObject, objGuid)
{
	// m_registerRecords is intentionally NOT initialized here — the
	// leaf ctor calls InitRegisterRecords() once its own vtable is
	// active so ibRecorderRegister::CreateRecordSet's virtual call
	// to GetRecordDescription dispatches to the leaf override. See
	// the header for the full rationale.
}

ibValueRecordDataObjectRecorderRef::ibValueRecordDataObjectRecorderRef(
	const ibValueRecordDataObjectRecorderRef& src) :
	ibValueRecordDataObjectRef(src)
{
	// Same as the primary ctor — leaf does InitRegisterRecords.
}

ibValueRecordDataObjectRecorderRef::~ibValueRecordDataObjectRecorderRef() = default;

void ibValueRecordDataObjectRecorderRef::InitRegisterRecords()
{
	wxASSERT(m_registerRecords == nullptr);
	m_registerRecords = new ibRecorderRegister(this);
	// …and the other list, where a kind has one: the sets of registrations a document writes.
	m_sequenceRecords = new ibRecorderRegister(this, ibRecorderWrites::Sequences);
}

// RegisterRecords — EXPORTED context variable, bound BEFORE the base compiles so
// the module resolves it. SetParent first so the lazily-built compile module gets
// the scope chain; the base re-SetParents (idempotent), binds ThisObject and
// compiles. The bind is the single source for both designer (compile module only —
// m_binder null) and runtime (binder). Shared by all recorder/document-like objects.
bool ibValueRecordDataObjectRecorderRef::InitializeObject(const ibGuid& copyGuid)
{
	ibRuntimeModuleDataObject::SetParent(ibSession::EditModuleManagerFor(m_metaObject->GetMetaData()));
	ibRecorderRegister* recordSet = m_registerRecords;
	BindExportVariable(wxT("RegisterRecords"), recordSet);
	// …and the other list, bound beside it: the sets of registrations this document writes.
	ibRecorderRegister* sequenceSet = m_sequenceRecords;
	BindExportVariable(wxT("Sequences"), sequenceSet);
	return ibValueRecordDataObjectRef::InitializeObject(copyGuid);
}

bool ibValueRecordDataObjectRecorderRef::InitializeObject(ibValueRecordDataObjectRef* source, bool generate)
{
	ibRuntimeModuleDataObject::SetParent(ibSession::EditModuleManagerFor(m_metaObject->GetMetaData()));
	ibRecorderRegister* recordSet = m_registerRecords;
	BindExportVariable(wxT("RegisterRecords"), recordSet);
	ibRecorderRegister* sequenceSet = m_sequenceRecords;
	BindExportVariable(wxT("Sequences"), sequenceSet);
	return ibValueRecordDataObjectRef::InitializeObject(source, generate);
}

bool ibValueRecordDataObjectRecorderRef::WriteObject(ibDocumentWriteMode writeMode, ibDocumentPostingMode postingMode)
{
	// Posting pre-guard: a recorder marked for deletion is not posted.
	if (!appData->DesignerMode()
	    && writeMode == ibDocumentWriteMode::ibDocumentWriteMode_Posting
	    && GetValueByMetaID(*GetMetaObject()->GetDataDeletionMark()).GetBoolean())
	{
		ibBackendCoreException::Error(_("%s cannot be posted: it is marked for deletion"),
			GetSourceCaption());
		return false;
	}

	// Scaffold via Phase A Begin/CommitWriteScope. Per-recorder middle:
	// BeforeWrite(wm, pm) + the posted mark (SetPosted) + SetNew
	// Number codegen + the date of a new one + SaveData +
	// register cascade (CreateRecordSet for new, Posting/UndoPosting
	// scripts + WriteRecordSet/DeleteRecordSet) + OnWrite.
	ibConnectionScope scope = ibSession::Current()->OpenConnectionScope();
	if (!BeginWriteScope(scope)) return true;
	ibWriteScope objectScope(*this);   // a refusal below leaves the recorder as it was, posted mark included (commonObject.h)

	// Asked only so an OPEN window can be told afterwards — see ibFormToNotify (backend_form.h).
	// A server has none, and that is not a reason for a write to fail.
	ibBackendValueForm* const valueForm = ibFormToNotify([this] { return GetForm(); });
	const bool newObject = IsNewObject();
	// Asked before the write marks it posted (SetPosted below): is this a posting AGAIN.
	const bool reposting = !newObject && IsPosted();

	// Every failure below says WHICH STAGE refused and on WHICH OBJECT. A posting run walks a long
	// chain — handler, row, movements per register, handler again — and "failed to write object in
	// db!" for all of them tells the user nothing about where to look. A cancel raised by script is
	// also reported as a cancel, not as a database failure: nothing went wrong in the DB there.
	//
	// A refusal is the exception and nothing else: the connection scope rolls the transaction back as it
	// unwinds, and the object scope puts the object back. (Each branch used to reset the number and roll back by hand
	// before raising - twelve copies, and every road that left by an exception of its own missed both.)
	const auto refuse = [this](const wxString& stage) -> bool {
		ibBackendCoreException::Error(stage, GetSourceCaption());
		return false;
	};

	{
		ibValue cancel = false;
		ExecAsProc(wxT("BeforeWrite"), cancel,
			ibValue::CreateEnumObject<ibValueEnumDocumentWriteMode>(writeMode),
			ibValue::CreateEnumObject<ibValueEnumDocumentPostingMode>(postingMode)
		);
		if (cancel.GetBoolean())
			return refuse(_("%s: writing cancelled by the BeforeWrite handler"));
		// A plain Write leaves the mark as it is; posting and undoing it set it.
		if (writeMode != ibDocumentWriteMode::ibDocumentWriteMode_Write)
			SetPosted(writeMode == ibDocumentWriteMode::ibDocumentWriteMode_Posting);
	}

	if (!IsSetUniqueIdentifier()) {
		ibValue prefix = wxEmptyString, standartProcessing = true;
		ExecAsProc(wxT("SetNewNumber"), prefix, standartProcessing);
		if (standartProcessing.GetBoolean())
			GenerateUniqueIdentifier(prefix.GetString());
	}

	// A new recorder written without a date is dated by the write. The date is the recorder's own
	// (its metaobject declares it beside the number), so no leaf is asked.
	if (newObject) {
		ibValueMetaObjectAttributePredefined* const date = GetMetaObject()->GetDocumentDate();
		if (GetValueByMetaID(*date).IsEmpty())
			SetValueByMetaID(*date, ibValueSystemFunction::CurrentDate());
	}

	if (!SaveData())
		return refuse(_("%s: failed to save the object data"));

	if (newObject) {
		m_registerRecords->CreateRecordSet();
		m_sequenceRecords->CreateRecordSet();
	}

	// Posting / UndoPosting cascade — scripts then the matching
	// register set Write/Delete. The cascade rides under this recorder's
	// row-lock from BeginWriteScope, so concurrent re-posts on the same
	// recorder serialise here.
	if (writeMode == ibDocumentWriteMode::ibDocumentWriteMode_Posting) {
		// ⭐⭐ A POSTING AGAIN STARTS FROM A BASE WITHOUT THIS DOCUMENT'S OWN MOVEMENTS. The handler reads the
		// base to compute what it writes — a correction reads the pieces of the record it corrects — and
		// what it read used to include what this very document wrote the last time: its own storno still
		// took the corrected record out of force, and the second posting of a payroll counted 0 days where
		// the first had counted the month (the 40 000-employee bench, 2026-09-11). So they go first, in this
		// transaction and through the door undoing a posting uses; the handler then writes the document's
		// movements from nothing. A register the handler leaves alone therefore keeps none — its movements
		// are what its handler writes. (A set somebody filled before the write is left to replace its own; and whether
		// they are cleared at all is the document's to say — ibRecorderRegister::DeleteRecordSet asks it.)
		if (reposting && !m_registerRecords->DeleteRecordSet(writeMode))
			return refuse(_("%s: failed to clear the movements of the previous posting"));
		// …and the registrations of the previous posting, by the same rule and in the same transaction.
		if (reposting && !m_sequenceRecords->DeleteRecordSet(writeMode))
			return refuse(_("%s: failed to clear the registrations of the previous posting"));

		ibValue cancel = false;
		ExecAsProc(wxT("Posting"), cancel,
			ibValue::CreateEnumObject<ibValueEnumDocumentPostingMode>(postingMode));
		if (cancel.GetBoolean())
			return refuse(_("%s: posting cancelled by the Posting handler"));

		// The cascade names the failing register itself (and lets its own exception through);
		// this only covers a silent false from the fan-out.
		if (!m_registerRecords->WriteRecordSet())
			return refuse(_("%s: failed to write the register movements"));
		// …and the registrations the handler filled — written here, where each set moves its own
		// sequence's border (ibValueRecordSetObjectSequence::WriteRecordSet).
		if (!m_sequenceRecords->WriteRecordSet())
			return refuse(_("%s: failed to write the sequence registrations"));
	}
	else if (writeMode == ibDocumentWriteMode::ibDocumentWriteMode_UndoPosting) {
		ibValue cancel = false;
		ExecAsProc(wxT("UndoPosting"), cancel);
		if (cancel.GetBoolean())
			return refuse(_("%s: undo posting cancelled by the UndoPosting handler"));
		if (!m_registerRecords->DeleteRecordSet(writeMode))
			return refuse(_("%s: failed to clear the register movements"));
		if (!m_sequenceRecords->DeleteRecordSet(writeMode))
			return refuse(_("%s: failed to clear the sequence registrations"));
	}

	{
		ibValue cancel = false;
		ExecAsProc(wxT("OnWrite"), cancel);
		if (cancel.GetBoolean())
			return refuse(_("%s: writing cancelled by the OnWrite handler"));
	}

	// Posting / UndoPosting audit. Layered on top of the generic
	// record.saved that CommitWriteScope emits — admin sees BOTH the
	// row change and the posting state change as distinct events.
	// Plain ibDocumentWriteMode_Write skips this — the generic
	// record.* audit covers it.
	if (ibLog && ibLog->IsEnabled(ibLogLevel::Audit)
	    && writeMode != ibDocumentWriteMode::ibDocumentWriteMode_Write)
	{
		const wxString refGuid = m_reference_impl
			? ibGuid(m_reference_impl->m_guid).str() : wxString();
		const int refMetaId = m_metaObject != nullptr
			? static_cast<int>(m_metaObject->GetMetaID()) : 0;   // type from the metaObject, not the key bytes
		const wxString evt = (writeMode == ibDocumentWriteMode::ibDocumentWriteMode_Posting)
			? wxT("posted") : wxT("unposted");
		ibLog->Audit(wxT("document"), evt, GetSourceCaption(), refGuid, refMetaId);
	}

	CommitWriteScope(scope, objectScope, valueForm, newObject);
	m_registerRecords->RefreshRecordSet();
	m_sequenceRecords->RefreshRecordSet();
	return true;
}

ibValueRecordDataObjectRecorderRef::ibWriteScope::~ibWriteScope()
{
	if (!IsCommitted())
		m_recorder.SetPosted(m_wasPosted);   // the posting never became durable, and its mark goes with it
}

void ibValueRecordDataObjectRecorderRef::SetDeletionMark(bool deletionMark)
{
	// Recorder-flavour of the deletion-mark algorithm: same as the
	// catalog/charts path (set the flag + SaveModify) but with an
	// up-front un-post so the row's movements clear before the mark
	// lands. UndoPosting is a no-op for non-posted recorders via the
	// IsPosted / SetPosted hooks.
	if (m_newObject)
		return;
	WriteObject(ibDocumentWriteMode::ibDocumentWriteMode_UndoPosting,
	             ibDocumentPostingMode::ibDocumentPostingMode_Regular);
	ibValueRecordDataObjectRef::SetDeletionMark(deletionMark);
}

bool ibValueRecordDataObjectRecorderRef::DeleteObject()
{
	// Scaffold via Phase A Begin/CommitDeleteScope. Per-recorder middle:
	// BeforeDelete + register-set DeleteRecordSet (cascading off this
	// recorder) + OnDelete + DeleteData. The register clear runs
	// BEFORE OnDelete + DeleteData so the recorder row exists for any
	// script side-effects and so the cascade uses recorder-level
	// row-locks for serialization.
	ibConnectionScope scope = ibSession::Current()->OpenConnectionScope();
	if (!BeginDeleteScope(scope)) return true;

	// Asked only so an OPEN window can be told afterwards — see ibFormToNotify (backend_form.h).
	// A server has none, and that is not a reason for a write to fail.
	ibBackendValueForm* const valueForm = ibFormToNotify([this] { return GetForm(); });

	// Stage-named failures, as on the write path: a delete that stops has a reason and a place.
	{
		ibValue cancel = false;
		ExecAsProc(wxT("BeforeDelete"), cancel);
		if (cancel.GetBoolean()) {
			scope.SafeRollBackTransaction();
			ibBackendCoreException::Error(_("%s: deletion cancelled by the BeforeDelete handler"),
				GetSourceCaption());
			return false;
		}
	}

	if (!m_registerRecords->DeleteRecordSet()) {
		scope.SafeRollBackTransaction();
		ibBackendCoreException::Error(_("%s: failed to clear the register movements"),
			GetSourceCaption());
		return false;
	}
	// A deleted document takes its registrations with it, as it takes its movements — and each set
	// sends its sequence's border back before its rows go.
	if (!m_sequenceRecords->DeleteRecordSet()) {
		scope.SafeRollBackTransaction();
		ibBackendCoreException::Error(_("%s: failed to clear the sequence registrations"),
			GetSourceCaption());
		return false;
	}

	{
		ibValue cancel = false;
		ExecAsProc(wxT("OnDelete"), cancel);
		if (cancel.GetBoolean()) {
			scope.SafeRollBackTransaction();
			ibBackendCoreException::Error(_("%s: deletion cancelled by the OnDelete handler"),
				GetSourceCaption());
			return false;
		}
	}

	if (!DeleteData()) {
		scope.SafeRollBackTransaction();
		ibBackendCoreException::Error(_("%s: failed to delete the object data"), GetSourceCaption());
		return false;
	}

	CommitDeleteScope(scope, valueForm);
	m_registerRecords->RefreshRecordSet();
	m_sequenceRecords->RefreshRecordSet();
	return true;
}

//***********************************************************************
//*						     metaData									*
//***********************************************************************




//***********************************************************************
//*                      Record key & set								*
//***********************************************************************

//////////////////////////////////////////////////////////////////////
//						  ibValueRecordKeyObject							//
//////////////////////////////////////////////////////////////////////

ibValueRecordKeyObject::ibValueRecordKeyObject(const ibValueMetaObjectRegisterData* metaObject) : ibValueDynamicMembers(ibValueTypes::TYPE_VALUE),
m_metaObject(metaObject)
{
	m_members.Bind(this, &ibValueRecordKeyObject::FillMembers);
}

ibValueRecordKeyObject::ibValueRecordKeyObject(const ibValueMetaObjectRegisterData* metaObject, const ibRowMetaValues& keyValues) : ibValueDynamicMembers(ibValueTypes::TYPE_VALUE),
m_metaObject(metaObject)
{
	m_members.Bind(this, &ibValueRecordKeyObject::FillMembers);
	// VERIFY completeness against the register's OWN dimensions: take each from the supplied row values, and FILL
	// a missing one with its typed-empty default (mirrors CreateUniqueKeyPair). So the caller can hand a WHOLE row
	// map — the key keeps only its dimensions and is always COMPLETE, whatever the row carried.
	if (metaObject != nullptr) {
		for (const auto* attr : metaObject->GetGenericDimensionArrayObject()) {
			const auto it = keyValues.find(attr->GetMetaID());
			m_keyValues.insert_or_assign(attr->GetMetaID(), it != keyValues.end() ? it->second : attr->CreateValue());
		}
	}
}

ibValueRecordKeyObject::~ibValueRecordKeyObject()
{
}

bool ibValueRecordKeyObject::IsEmpty() const
{
	for (auto value : m_keyValues) {
		const ibValue& cValue = value.second;
		if (!cValue.IsEmpty())
			return false;
	}

	return true;
}

ibClassID ibValueRecordKeyObject::GetClassType() const
{
	const ibCtorMetaValueType* clsFactory =
		m_metaObject->GetTypeCtor(ibCtorObjectMetaType::ibCtorObjectMetaType_RecordKey);
	wxASSERT(clsFactory);
	return clsFactory->GetClassType();
}

wxString ibValueRecordKeyObject::GetClassName() const
{
	const ibCtorMetaValueType* clsFactory =
		m_metaObject->GetTypeCtor(ibCtorObjectMetaType::ibCtorObjectMetaType_RecordKey);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

ibString ibValueRecordKeyObject::GetString() const
{
	const ibCtorMetaValueType* clsFactory =
		m_metaObject->GetTypeCtor(ibCtorObjectMetaType::ibCtorObjectMetaType_RecordKey);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

//////////////////////////////////////////////////////////////////////
//						  ibValueRecordManagerObject						//
//////////////////////////////////////////////////////////////////////

void ibValueRecordManagerObject::CreateEmptyKey()
{
	m_recordSet->CreateEmptyKey();
}

bool ibValueRecordManagerObject::InitializeObject(const ibValueRecordManagerObject* source, bool newRecord)
{
	if (!m_recordSet->InitializeObject(source ? source->GetRecordSet() : nullptr, newRecord))
		return false;

	if (!appData->DesignerMode()) {
		if (!newRecord && !ReadData(m_objGuid)) {
			PrepareEmptyObject(source);
		}
		else if (newRecord) {
			PrepareEmptyObject(source);
		}
	}
	else {
		PrepareEmptyObject(source);
	}

	InvalidateNames();

	//is Ok
	return true;
}

bool ibValueRecordManagerObject::InitializeObject(const ibUniqueKeyPair& key)
{
	if (!m_recordSet->InitializeObject(nullptr, true))
		return false;

	if (!appData->DesignerMode()) {
		if (ReadData(key)) {
			m_recordSet->m_selected = false; // is new 
			m_recordSet->Modify(true); // and modify
		}
		else {
			PrepareEmptyObject(nullptr);
		}
	}
	else {
		PrepareEmptyObject(nullptr);
	}

	InvalidateNames();

	//is Ok
	return true;
}

ibValuePtr<ibValueRecordManagerObject> ibValueRecordManagerObject::CopyRegisterValue()
{
	return m_metaObject->CreateRecordManagerObjectValue(this);
}

ibValueRecordManagerObject::ibValueRecordManagerObject(const ibValueMetaObjectRegisterData* metaObject, const ibUniqueKeyPair& uniqueKey) : ibValueDynamicMembers(ibValueTypes::TYPE_VALUE),
m_objGuid(uniqueKey),
m_metaObject(metaObject),
m_recordSet(m_metaObject->CreateRecordSetObjectValue(uniqueKey, false)), m_recordLine(nullptr)
{
}

ibValueRecordManagerObject::ibValueRecordManagerObject(const ibValueRecordManagerObject& source) : ibValueDynamicMembers(ibValueTypes::TYPE_VALUE),
m_objGuid(source.m_metaObject->CreateUniqueKeyPair()),
m_metaObject(source.m_metaObject),
m_recordSet(m_metaObject->CreateRecordSetObjectValue(source.m_recordSet, false)), m_recordLine(nullptr)
{
}

ibValueRecordManagerObject::~ibValueRecordManagerObject()
{
}

ibBackendValueForm* ibValueRecordManagerObject::GetForm() const
{
	if (!m_objGuid.isValid())
		return nullptr;
	// Same as the object's own GetForm above — asked plainly, and a process with no forms says so.
	if (m_recordSet->m_selected)
		return ibBackendValueForm::FindFormBySourceUniqueKey(m_objGuid);
	return nullptr;
}

bool ibValueRecordManagerObject::IsEmpty() const
{
	return m_recordSet->IsEmpty();
}

const ibSourceExplorer* ibValueRecordManagerObject::GetSourceExplorer() const
{
	m_sourceExplorer.Reset(
		wxT("Ref"), _("Ref"), m_metaObject->GetMetaID(), GetClassType(), false, false
	);

	for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
		m_sourceExplorer.AppendColumn(object->GetQueryColumn());
	}

	return &m_sourceExplorer;
}

void ibValueRecordManagerObject::Modify(bool mod)
{
	// Same as the object's Modify — the record set is modified either way, and "there was nobody to
	// tell" is not a failure of the modification.
	ibBackendValueForm* const foundedForm =
		ibFormToNotify([this] { return ibBackendValueForm::FindFormBySourceUniqueKey(m_objGuid); });
	if (foundedForm != nullptr)
		foundedForm->Modify(mod);

	m_recordSet->Modify(mod);
}

bool ibValueRecordManagerObject::IsModified() const
{
	return m_recordSet->IsModified();
}

// ibSourceDataObject hop gate — reads the id, then filters the pin by the field's LIVE declared type
// (FindAnyAttributeObjectByFilter -> GetTypeDesc): a composite field's UNDEFINED resolves to the pinned twin,
// a field retyped away from the pin does not. Empty type (attribute not found) skips the check.
bool ibValueRecordManagerObject::GetValueBySourceHop(const ibSourceHop& hop, ibValue& out) const
{
	const bool got = GetValueByMetaID(hop.m_id, out);
	const ibValueMetaObjectAttributeBase* attribute = GetMetaObject()->FindAnyAttributeObjectByFilter(hop.m_id);
	return ibValueReferenceDataObject::CoerceHopType(hop, out, attribute != nullptr ? attribute->GetTypeDesc() : ibTypeDescription(), GetSourceMetaData()) || got;
}

bool ibValueRecordManagerObject::SetValueByMetaID(const ibMetaID& id, const ibValue& varMetaVal)
{
	// Read off the record after the write, for the reason given at ibValueRecordDataObjectRef::SetValueByMetaID.
	const ibValue before = ibValueRecordManagerObject::GetValueByMetaID(id);
	const bool result = m_recordLine->SetValueByMetaID(id, varMetaVal);
	if (!(before == ibValueRecordManagerObject::GetValueByMetaID(id)))
		ibValueRecordManagerObject::Modify(true);
	return result;
}

bool ibValueRecordManagerObject::GetValueByMetaID(const ibMetaID& id, ibValue& pvarMetaVal) const
{
	return m_recordLine->GetValueByMetaID(id, pvarMetaVal);
}

ibClassID ibValueRecordManagerObject::GetClassType() const
{
	const ibCtorMetaValueType* clsFactory =
		m_metaObject->GetTypeCtor(ibCtorObjectMetaType::ibCtorObjectMetaType_RecordManager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassType();
}

wxString ibValueRecordManagerObject::GetClassName() const
{
	const ibCtorMetaValueType* clsFactory =
		m_metaObject->GetTypeCtor(ibCtorObjectMetaType::ibCtorObjectMetaType_RecordManager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

ibString ibValueRecordManagerObject::GetString() const
{
	const ibCtorMetaValueType* clsFactory =
		m_metaObject->GetTypeCtor(ibCtorObjectMetaType::ibCtorObjectMetaType_RecordManager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

/////////////////////////////////////////////////////////////////////

void ibValueRecordManagerObject::PrepareEmptyObject(const ibValueRecordManagerObject* source)
{
	m_recordLine = nullptr;

	if (source == nullptr) {
		m_recordLine = new ibValueRecordSetObject::ibValueRecordSetObjectRegisterReturnLine(
			m_recordSet,
			m_recordSet->GetItem(
				m_recordSet->AppendRow()
			)
		);
	}
	else if (source != nullptr) {
		m_recordLine = m_recordSet->GetRowAt(
			m_recordSet->GetItem(0)
		);
	}

	m_recordSet->Modify(true);
}

//////////////////////////////////////////////////////////////////////
//						  ibValueRecordSetObject							//
//////////////////////////////////////////////////////////////////////

// The set declares no property of its own — see the declaration for why `Filter` is a bind.
bool ibValueRecordSetObject::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	// The set's exported names — `Filter` among them — resolve exactly as a document's do: through
	// the ProcUnit where there is one, and through the BIND itself where there is not (the designer
	// has no runtime, and the bind map is present in both). Nothing here knows the name `Filter`;
	// it is bound in InitializeObject and reaches the member table through the descriptor autobind.
	if (m_members.GetPropAlias(lPropNum) != eProcUnit)
		return false;

	if (m_procUnit != nullptr && m_procUnit->GetPropVal(GetPropName(lPropNum), pvarPropVal))
		return true;

	if (ibValue* bound = GetBoundValue(GetPropName(lPropNum))) {
		pvarPropVal = bound;
		return true;
	}

	return false;
}

void ibValueRecordSetObject::CreateEmptyKey()
{
	m_keyValues.clear();
	for (const auto object : m_metaObject->GetGenericDimensionArrayObject()) {
		if (object->IsDeleted())
			continue;
		m_keyValues.insert_or_assign(
			object->GetMetaID(), object->CreateValue()
		);
	}
}

bool ibValueRecordSetObject::InitializeObject(const ibValueRecordSetObject* source, bool newRecord)
{
	if (!m_metaObject->AccessRight_Read()) {
		ibBackendAccessException::Error(wxString::Format(_("reading register '%s'"), m_metaObject->GetSynonym()));
		return false;
	}

	ibValueModuleManager* moduleManager = ibSession::EditModuleManagerFor(m_metaObject->GetMetaData());
	wxASSERT(moduleManager);

	ibRuntimeModuleDataObject::SetParent(moduleManager);
	BindContextVariable(thisObject, this);                   // contextual
	BindExportVariable(wxT("Filter"), m_recordSetKeyValue);  // exported — register filter/key

	try {
		Compile();
	}
	catch (const ibBackendException&) {
		if (!appData->DesignerMode())
			throw;
		return false;
	};

	if (source != nullptr) {
		for (long row = 0; row < source->GetRowCount(); row++) {
			ibComposerNode* node = source->GetViewData<ibComposerNode>(source->GetItem(row));
			wxASSERT(node);
			ibValueModelStorage::Append(new ibComposerNode(*node), false);
		}
	}

	if (!appData->DesignerMode()) {
		if (!newRecord) ReadData();
	}

	if (!appData->DesignerMode()) {
		wxASSERT(m_procUnit == nullptr);
		InitializeRuntime();
		// Descriptor parent cascades both compile and procUnit parents.
		ibRuntimeModuleDataObject::SetParent(moduleManager);
		Execute();
	}

	InvalidateNames();

	//is Ok
	return true;
}

///////////////////////////////////////////////////////////////////////////////////

ibValuePtr<ibValueRecordSetObject> ibValueRecordSetObject::CopyRegisterValue()
{
	return m_metaObject->CreateRecordSetObjectValue(this);
}

///////////////////////////////////////////////////////////////////////////////////

ibValueRecordSetObject::ibValueRecordSetObject(const ibValueMetaObjectRegisterData* metaObject, const ibUniqueKeyPair& uniqueKey) : ibValueModelStorage(),
ibRuntimeModuleDataObject(m_members, this),
m_objModified(false), m_selected(false),
// ⭐⭐ A SET MADE WITHOUT A KEY HAS NO FILTER AT ALL — not a filter of empty values.
//
// The filter's `Use` IS the key's PRESENCE here: setting Use = True inserts the entry, False erases
// it, and reading Use asks whether it is in the map. Seeding every dimension with an empty value
// therefore turned every filter ON, with nothing in it — so a set built and written from a script
// stored blank keys over whatever its lines held (an information register took year 0001 and an
// all-zero reference, and the second line collided with the first on the unique index).
//
// Nothing is lost by starting empty: a caller who wants to address the set says so — Filter.X.Set(v)
// — and a set read by key gets its pair handed in.
m_keyValues(uniqueKey.IsOk() ? ibRowMetaValues(uniqueKey) : ibRowMetaValues()), m_metaObject(metaObject),
m_recordColumnCollection(new ibValueRecordSetObjectRegisterColumnCollection(this)), m_recordSetKeyValue(new ibValueRecordSetObjectRegisterKeyValue(this))
{
}

ibValueRecordSetObject::ibValueRecordSetObject(const ibValueRecordSetObject& source) : ibValueModelStorage(),
ibRuntimeModuleDataObject(m_members, this),
m_objModified(true), m_selected(false),
m_keyValues(source.m_keyValues), m_metaObject(source.m_metaObject),
m_recordColumnCollection(new ibValueRecordSetObjectRegisterColumnCollection(this)), m_recordSetKeyValue(new ibValueRecordSetObjectRegisterKeyValue(this))
{
	for (long row = 0; row < source.GetRowCount(); row++) {
		ibComposerNode* node = source.GetViewData<ibComposerNode>(source.GetItem(row));
		wxASSERT(node);
		ibValueModelStorage::Append(new ibComposerNode(*node), false);
	}
}

ibValueRecordSetObject::~ibValueRecordSetObject()
{
}


//----------------------------------------------------------------------
// Phase B template-method Write/Delete for register-set leaves.
// Accumulation / Accounting / Information are byte-identical mod
// SaveData / DeleteData (virtual). The base owns this scaffold;
// subclasses inherit it verbatim and override only SaveData /
// DeleteData with their per-type UPSERT / DELETE SQL.
//----------------------------------------------------------------------

bool ibValueRecordSetObject::WriteRecordSet(bool replace, bool clearTable)
{
	ibConnectionScope scope = ibSession::Current()->OpenConnectionScope();
	if (!BeginRecordSetWriteScope(scope)) return true;

	{
		ibValue cancel = false;
		ExecAsProc(wxT("BeforeWrite"), cancel);
		if (cancel.GetBoolean()) {
			scope.SafeRollBackTransaction();
			ibBackendCoreException::Error(_("Register '%s': writing cancelled by the BeforeWrite handler"),
				m_metaObject->GetSynonym());
			return false;
		}
	}

	// SaveData already reports a failed fill check per line ("The %s is required on line %i");
	// this names the register whose rows could not be stored.
	if (!SaveData(replace, clearTable)) {
		scope.SafeRollBackTransaction();
		ibBackendCoreException::Error(_("Register '%s': failed to store the records"),
			m_metaObject->GetSynonym());
		return false;
	}

	{
		ibValue cancel = false;
		ExecAsProc(wxT("OnWrite"), cancel);
		if (cancel.GetBoolean()) {
			scope.SafeRollBackTransaction();
			ibBackendCoreException::Error(_("Register '%s': writing cancelled by the OnWrite handler"),
				m_metaObject->GetSynonym());
			return false;
		}
	}

	CommitRecordSetScope(scope);
	return true;
}

bool ibValueRecordSetObject::DeleteRecordSet()
{
	ibConnectionScope scope = ibSession::Current()->OpenConnectionScope();
	if (!BeginRecordSetDeleteScope(scope)) return true;

	{
		ibValue cancel = false;
		ExecAsProc(wxT("BeforeDelete"), cancel);
		if (cancel.GetBoolean()) {
			scope.SafeRollBackTransaction();
			ibBackendCoreException::Error(_("Register '%s': deletion cancelled by the BeforeDelete handler"),
				m_metaObject->GetSynonym());
			return false;
		}
	}

	if (!DeleteData()) {
		scope.SafeRollBackTransaction();
		ibBackendCoreException::Error(_("Register '%s': failed to delete the records"),
			m_metaObject->GetSynonym());
		return false;
	}

	{
		ibValue cancel = false;
		ExecAsProc(wxT("OnDelete"), cancel);
		if (cancel.GetBoolean()) {
			scope.SafeRollBackTransaction();
			ibBackendCoreException::Error(_("Register '%s': deletion cancelled by the OnDelete handler"),
				m_metaObject->GetSynonym());
			return false;
		}
	}

	CommitRecordSetScope(scope);
	return true;
}

bool ibValueRecordSetObject::GetAt(const ibValue& varKeyValue, ibValue& pvarValue)
{
	long index = varKeyValue.GetUInteger();
	if (index >= GetRowCount() && !appData->DesignerMode()) {
		ibBackendCoreException::Error(_("Array index out of bounds"));
		return false;
	}
	pvarValue = new ibValueRecordSetObjectRegisterReturnLine(this, GetItem(index));
	return true;
}

ibClassID ibValueRecordSetObject::GetClassType() const
{
	const ibCtorMetaValueType* clsFactory =
		m_metaObject->GetTypeCtor(ibCtorObjectMetaType::ibCtorObjectMetaType_RecordSet);
	wxASSERT(clsFactory);
	return clsFactory->GetClassType();
}

wxString ibValueRecordSetObject::GetClassName() const
{
	const ibCtorMetaValueType* clsFactory =
		m_metaObject->GetTypeCtor(ibCtorObjectMetaType::ibCtorObjectMetaType_RecordSet);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

ibString ibValueRecordSetObject::GetString() const
{
	const ibCtorMetaValueType* clsFactory =
		m_metaObject->GetTypeCtor(ibCtorObjectMetaType::ibCtorObjectMetaType_RecordSet);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

#include "backend/system/value/valueTable.h"

bool ibValueRecordSetObject::LoadDataFromTable(ibValueModel* srcTable)
{
	ibValueModelColumnCollection* colData = srcTable->GetColumnCollection();

	if (colData == nullptr)
		return false;
	wxArrayString columnName;
	for (unsigned int idx = 0; idx < colData->GetColumnCount() - 1; idx++) {
		ibValueModelColumnCollection::ibValueModelColumnInfo* colInfo = colData->GetColumnInfo(idx);
		wxASSERT(colInfo);
		if (m_recordColumnCollection->GetColumnByName(colInfo->GetColumnName()) != nullptr) {
			columnName.push_back(colInfo->GetColumnName());
		}
	}
	unsigned int rowCount = srcTable->GetRowCount();
	for (unsigned int row = 0; row < rowCount; row++) {
		const ibDataViewItem& srcItem = srcTable->GetItem(row);
		const ibDataViewItem& dstItem = GetItem(AppendRow());
		for (auto colName : columnName) {
			ibValue cRetValue;
			if (srcTable->GetValueByMetaID(srcItem, srcTable->GetColumnIDByName(colName), cRetValue)) {
				const ibMetaID& id = GetColumnIDByName(colName);
				if (id != wxNOT_FOUND) SetValueByMetaID(dstItem, id, cRetValue);
			}
		}
	}

	return true;
}

ibValuePtr<ibValueModel> ibValueRecordSetObject::SaveDataToTable() const
{
	const ibValuePtr<ibValueModelTable> valueTable = ibValue::CreateObject<ibValueModelTable>();

	ibValueModelColumnCollection* colData = valueTable->GetColumnCollection();
	for (unsigned int idx = 0; idx < m_recordColumnCollection->GetColumnCount() - 1; idx++) {
		ibValueModelColumnCollection::ibValueModelColumnInfo* colInfo = m_recordColumnCollection->GetColumnInfo(idx);
		wxASSERT(colInfo);
		ibValueModelColumnCollection::ibValueModelColumnInfo* newColInfo = colData->AddColumn(
			colInfo->GetColumnName(), colInfo->GetColumnType(), colInfo->GetColumnCaption(), colInfo->GetColumnWidth()
		);
		newColInfo->SetColumnID(colInfo->GetColumnID());
	}
	valueTable->InvalidateNames();
	for (long row = 0; row < GetRowCount(); row++) {
		const ibDataViewItem& srcItem = GetItem(row);
		const ibDataViewItem& dstItem = valueTable->GetItem(valueTable->AppendRow());
		for (unsigned int col = 0; col < colData->GetColumnCount(); col++) {
			ibValue cRetValue;
			ibValueModelColumnCollection::ibValueModelColumnInfo* colInfo = colData->GetColumnInfo(col);
			wxASSERT(colInfo);
			if (GetValueByMetaID(srcItem, colInfo->GetColumnID(), cRetValue)) {
				const ibMetaID& id = GetColumnIDByName(colInfo->GetColumnName());
				if (id != wxNOT_FOUND) valueTable->SetValueByMetaID(dstItem, id, cRetValue);
			}
		}
	}

	return valueTable;
}

bool ibValueRecordSetObject::SetValueByMetaID(const ibDataViewItem& item, const ibMetaID& id, const ibValue& varMetaVal)
{
	if (!appData->DesignerMode()) {
		ibComposerNode* node = GetViewData<ibComposerNode>(item);
		if (node != nullptr) {
			// The set's own column map, not a walk of the metaobject's children: a value set in a line is the
			// commonest thing a posting does — 72 234 movements a payroll, several fields each.
			const ibValueMetaObjectAttributeBase* attribute = m_recordColumnCollection->GetAttributeByID(id);
			if (attribute != nullptr) {
				// 🛑 A LINE THAT WAS CHANGED MAKES ITS SET CHANGED. The posting pass writes only the sets
				// that say they are modified (ibRecorderRegister::WriteRecordSet — a set already written as
				// it stands must not be written again, because that write replaces). Adding and clearing
				// said so; changing a value in a line did not. MEASURED 2026-09-10 on a payroll document:
				// the handler wrote its lines, asked the base, read them back and put the results in — and
				// the pass skipped the set as unmodified, so every result stayed 0 with the document posted
				// and no word. Reading fills the lines past this door (AppendTableValue), so a set fresh from
				// the database still answers "not modified".
				// …and narrowed by the field's link, with THIS LINE as the holder: a resource typed by a
				// kind column takes the type of the kind standing in the same record, not in some other
				// one (choiceLinkResolver.h). A set written by a posting pass is exactly the road that
				// has no control on it.
				const ibValue settled =
					ibChoiceLinkResolver::Adjust(ibChoiceHolder(this, item), attribute, varMetaVal);

				ibValue previous;
				const bool changed = !node->GetValue(id, previous) || !(previous == settled);

				const bool set = node->SetValue(id, settled, true);
				if (set) {
					// …and this record's other cells that were chosen within this one go with it — THIS
					// record, not the set: a kind column changed in one line says nothing about another.
					//
					// ⚠ ON A CHANGE, NOT ON A WRITE (see the object's own write, above in this file).
					if (changed) {
						ibChoiceHolder holder(this, item);
						ibChoiceLinkResolver::ClearLinked(holder, id);
					}
					Modify(true);
				}
				return set;
			}
		}
	}

	return false;
}

bool ibValueRecordSetObject::GetValueByMetaID(const ibDataViewItem& item, const ibMetaID& id, ibValue& pvarMetaVal) const
{
	if (appData->DesignerMode()) {
		const ibValueMetaObjectAttributeBase* attribute = m_metaObject->FindAnyAttributeObjectByFilter(id);
		if (attribute != nullptr) {
			pvarMetaVal = attribute->CreateValue();
			return true;
		}
		return false;
	}

	ibComposerNode* node = GetViewData<ibComposerNode>(item);
	if (node == nullptr)
		return false;
	return node->GetValue(id, pvarMetaVal);
}

//////////////////////////////////////////////////////////////////////
//					ibValueRecordSetObjectRegisterColumnCollection				//
//////////////////////////////////////////////////////////////////////


ibValueRecordSetObject::ibValueRecordSetObjectRegisterColumnCollection::ibValueRecordSetObjectRegisterColumnCollection() :
	ibValueModelColumnCollection(),
	m_ownerTable(nullptr)
{
}

ibValueRecordSetObject::ibValueRecordSetObjectRegisterColumnCollection::ibValueRecordSetObjectRegisterColumnCollection(ibValueRecordSetObject* ownerTable) :
	ibValueModelColumnCollection(),
	m_ownerTable(ownerTable)
{
	const ibValueMetaObjectGenericData* metaObject = m_ownerTable->GetMetaObject();
	wxASSERT(metaObject);

	for (const auto object : metaObject->GetGenericAttributeArrayObject()) {
		m_listColumnInfo.insert_or_assign(object->GetMetaID(),
			new ibValueRecordSetRegisterColumnInfo(object));
	}
}

ibValueRecordSetObject::ibValueRecordSetObjectRegisterColumnCollection::~ibValueRecordSetObjectRegisterColumnCollection()
{
}

bool ibValueRecordSetObject::ibValueRecordSetObjectRegisterColumnCollection::SetAt(const ibValue& varKeyValue, const ibValue& varValue)// array index starts at 0
{
	return false;
}

bool ibValueRecordSetObject::ibValueRecordSetObjectRegisterColumnCollection::GetAt(const ibValue& varKeyValue, ibValue& pvarValue) // array index starts at 0
{
	unsigned int index = varKeyValue.GetUInteger();
	// `index` is unsigned, so `index < 0` was dead code, and && binds tighter than ||
	// — the condition already meant "out of range AND not in the designer". Spelled out;
	// the designer-mode exemption is preserved, not introduced (see docs/portability.md).
	if (index >= m_listColumnInfo.size() && !appData->DesignerMode()) {
		ibBackendCoreException::Error(_("Index goes beyond array"));
		return false;
	}

	auto it = m_listColumnInfo.begin();
	std::advance(it, index);
	pvarValue = it->second;
	return true;
}

//////////////////////////////////////////////////////////////////////
//					ibValueRecordSetRegisterColumnInfo               //
//////////////////////////////////////////////////////////////////////


ibValueRecordSetObject::ibValueRecordSetObjectRegisterColumnCollection::ibValueRecordSetRegisterColumnInfo::ibValueRecordSetRegisterColumnInfo() :
	ibValueModelColumnInfo(), m_metaAttribute(nullptr)
{
}

ibValueRecordSetObject::ibValueRecordSetObjectRegisterColumnCollection::ibValueRecordSetRegisterColumnInfo::ibValueRecordSetRegisterColumnInfo(ibValueMetaObjectAttributeBase* attribute) :
	ibValueModelColumnInfo(), m_metaAttribute(attribute)
{
}

ibValueRecordSetObject::ibValueRecordSetObjectRegisterColumnCollection::ibValueRecordSetRegisterColumnInfo::~ibValueRecordSetRegisterColumnInfo()
{
}

//////////////////////////////////////////////////////////////////////
//					 ibValueRecordSetObjectRegisterReturnLine					//
//////////////////////////////////////////////////////////////////////


ibValueRecordSetObject::ibValueRecordSetObjectRegisterReturnLine::ibValueRecordSetObjectRegisterReturnLine(ibValueRecordSetObject* ownerTable, const ibDataViewItem& line)
	: ibValueModelReturnLine(line), m_ownerTable(ownerTable)
{
	HoldOwnerModel(ownerTable);   // the row speaks through the set; see the base
}

ibValueRecordSetObject::ibValueRecordSetObjectRegisterReturnLine::~ibValueRecordSetObjectRegisterReturnLine()
{
}

void ibValueRecordSetObject::DescribeReturnLine(ibMemberTable& helper) const
{
	const ibValueMetaObjectGenericData* metaObject = GetMetaObject();
	if (metaObject != nullptr) {
		wxString objectName;
		for (const auto object : metaObject->GetGenericAttributeArrayObject()) {
			if (object->IsDeleted())
				continue;
			// ⭐ A COLUMN THE SETTINGS TURNED OFF IS NOT A FIELD OF THIS LINE.
			//
			// The attribute list is the SCHEMA's: a predefined column stays declared whatever the
			// settings say, because a column that comes and goes takes its data with it (see the note
			// on FillArrayObjectByPredefinedAttribute). What the settings decide is which of them a
			// line actually HAS — metaDisableFlag is exactly that mark, already set by an accumulation
			// register with no RecordType, an independent information register with no Recorder /
			// LineNumber / LineActive, and a correspondence register's unused account side.
			//
			// Listing them anyway offered a line fields nothing reads and nothing writes: assignment
			// went into a column that never reaches storage, and autocomplete showed both `Account`
			// and `AccountCr` on a register that has one account. Ask the mark the metatype already
			// maintains rather than teach this walk each metatype's settings.
			if (!object->IsEnabled())
				continue;
			if (!object->GetObjectNameAsString(objectName))
				continue;
			helper.AppendProp(
				objectName,
				object->GetMetaID()
			);
		}
	}
}

//////////////////////////////////////////////////////////////////////
//				       ibValueRecordSetObjectRegisterKeyValue					//
//////////////////////////////////////////////////////////////////////

ibValueRecordSetObject::ibValueRecordSetObjectRegisterKeyValue::ibValueRecordSetObjectRegisterKeyValue(ibValueRecordSetObject* recordSet) : ibValueDynamicMembers(ibValueTypes::TYPE_VALUE, true),
m_recordSet(recordSet)
{
	m_members.Bind(this, &ibValueRecordSetObjectRegisterKeyValue::FillMembers);
}

ibValueRecordSetObject::ibValueRecordSetObjectRegisterKeyValue::~ibValueRecordSetObjectRegisterKeyValue()
{
}

//////////////////////////////////////////////////////////////////////
//						ibValueRecordSetObjectRegisterKeyDescriptionValue		//
//////////////////////////////////////////////////////////////////////

ibValueRecordSetObject::ibValueRecordSetObjectRegisterKeyValue::ibValueRecordSetObjectRegisterKeyDescriptionValue::ibValueRecordSetObjectRegisterKeyDescriptionValue(ibValueRecordSetObject* recordSet, const ibMetaID& id) : ibValueDynamicMembers(ibValueTypes::TYPE_VALUE),
m_metaId(id), m_recordSet(recordSet)
{
	m_members.Bind(this, &ibValueRecordSetObjectRegisterKeyDescriptionValue::FillMembers);
}

ibValueRecordSetObject::ibValueRecordSetObjectRegisterKeyValue::ibValueRecordSetObjectRegisterKeyDescriptionValue::~ibValueRecordSetObjectRegisterKeyDescriptionValue()
{
}

//////////////////////////////////////////////////////////////////////////////////////

// The set's own columns — its attributes, laid out once when the set was made. A line added is not a walk of the
// metaobject: a payroll adds 80 008 of them.
void ibValueRecordSetObject::DescribeNewRow(ibNewRowColumns& columns) const
{
	for (const auto& column : m_recordColumnCollection->m_listColumnInfo) {
		const ibValueMetaObjectAttributeBase* attribute = column.second->GetAttribute();
		columns.push_back({ column.first, [attribute]() { return attribute->CreateValue(); } });
	}
}

long ibValueRecordSetObject::AppendRow(unsigned int before)
{
	// A copy of the set's own empty line (ibValueModelStorage::NewRow) — made once, from the set's columns.
	ibComposerNode* rowData = NewRow();

	if (before > 0)
		return ibValueModelStorage::Insert(rowData, before, !ibBackendException::IsEvalMode());

	return ibValueModelStorage::Append(rowData, !ibBackendException::IsEvalMode());
}

enum Func
{
	enAdd = 0,
	enCount,
	enClear,
	enLoad,
	enUnload,
	enWrite,
	enModified,
	enRead,
	enSelected,
	enGetMetadata,
};

enum
{
	enEmpty,
	enMetadata,
};

enum
{
	enSet,
};

//****************************************************************************
//*                              Override attribute                          *
//****************************************************************************

bool ibValueRecordKeyObject::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	return false;
}

bool ibValueRecordKeyObject::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	return false;
}

////////////////////////////////////////////////////////////////////////////

bool ibValueRecordSetObject::ibValueRecordSetObjectRegisterReturnLine::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	const ibMetaID& id = m_ownerTable->m_methodHelperReturnLine.GetPropData(lPropNum);
	if (id != wxNOT_FOUND)
		return SetValueByMetaID(id, varPropVal);
	return false;
}

bool ibValueRecordSetObject::ibValueRecordSetObjectRegisterReturnLine::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	const ibMetaID& id = m_ownerTable->m_methodHelperReturnLine.GetPropData(lPropNum);
	if (id != wxNOT_FOUND) {
		return GetValueByMetaID(id, pvarPropVal);
	}
	return false;
}

////////////////////////////////////////////////////////////////////////////

ibClassID ibValueRecordSetObject::ibValueRecordSetObjectRegisterReturnLine::GetClassType() const
{
	const ibValueMetaObject* metaTable = m_ownerTable->GetMetaObject();
	const ibMetaData* metaData = metaTable->GetMetaData();
	wxASSERT(metaData);
	const ibCtorMetaValueType* clsFactory =
		metaData->GetTypeCtor(metaTable, ibCtorObjectMetaType::ibCtorObjectMetaType_RecordSet_String);
	wxASSERT(clsFactory);
	return clsFactory->GetClassType();
}

wxString ibValueRecordSetObject::ibValueRecordSetObjectRegisterReturnLine::GetClassName() const
{
	const ibValueMetaObject* metaTable = m_ownerTable->GetMetaObject();
	const ibMetaData* metaData = metaTable->GetMetaData();
	wxASSERT(metaData);
	const ibCtorMetaValueType* clsFactory =
		metaData->GetTypeCtor(metaTable, ibCtorObjectMetaType::ibCtorObjectMetaType_RecordSet_String);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

ibString ibValueRecordSetObject::ibValueRecordSetObjectRegisterReturnLine::GetString() const
{
	const ibValueMetaObject* metaTable = m_ownerTable->GetMetaObject();
	const ibMetaData* metaData = metaTable->GetMetaData();
	wxASSERT(metaData);
	const ibCtorMetaValueType* clsFactory =
		metaData->GetTypeCtor(metaTable, ibCtorObjectMetaType::ibCtorObjectMetaType_RecordSet_String);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

////////////////////////////////////////////////////////////////////////////

bool ibValueRecordSetObject::ibValueRecordSetObjectRegisterKeyValue::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	return false;
}

bool ibValueRecordSetObject::ibValueRecordSetObjectRegisterKeyValue::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	const ibMetaID& id = m_members.GetPropData(lPropNum);
	if (id != wxNOT_FOUND) {
		pvarPropVal = new ibValueRecordSetObjectRegisterKeyDescriptionValue(m_recordSet, id);
		return true;
	}
	return false;
}

//****************************************************************************
//*                              Support methods                             *
//****************************************************************************

void ibValueRecordKeyObject::FillMembers(ibMemberTable& helper) const
{
	helper.AppendFunc(wxT("IsEmpty"), wxT("IsEmpty()"));
	helper.AppendFunc(wxT("Metadata"), wxT("Metadata()"));

	wxString objectName;

	//fill custom attributes
	for (const auto object : m_metaObject->GetGenericDimensionArrayObject()) {
		if (object->IsDeleted())
			continue;
		if (!object->GetObjectNameAsString(objectName))
			continue;
		helper.AppendProp(
			objectName,
			object->GetMetaID()
		);
	}
}

//////////////////////////////////////////////////////////////

void ibValueRecordSetObject::ibValueRecordSetObjectRegisterKeyValue::FillMembers(ibMemberTable& helper) const
{
	const ibValueMetaObjectRegisterData* metaObject = m_recordSet->GetMetaObject();
	if (metaObject != nullptr) {
		wxString objectName;
		for (const auto object : metaObject->GetGenericDimensionArrayObject()) {
			if (object->IsDeleted())
				continue;
			if (!object->GetObjectNameAsString(objectName))
				continue;
			helper.AppendProp(
				objectName,
				object->GetMetaID()
			);
		}
	}
}

//////////////////////////////////////////////////////////////

void ibValueRecordSetObject::ibValueRecordSetObjectRegisterKeyValue::ibValueRecordSetObjectRegisterKeyDescriptionValue::FillMembers(ibMemberTable& helper) const
{
	helper.AppendFunc(wxT("Set"), 1, wxT("Set(value: any)"));

	helper.AppendProp(wxT("Value"), m_metaId);
	helper.AppendProp(wxT("Use"));
}

enum Prop
{
	eValue,
	eUse
};

bool ibValueRecordSetObject::ibValueRecordSetObjectRegisterKeyValue::ibValueRecordSetObjectRegisterKeyDescriptionValue::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	const ibValueMetaObjectRegisterData* metaObject = m_recordSet->GetMetaObject();
	wxASSERT(metaObject);

	// A key the register has switched OFF (an independent register's Recorder) is not found, and a filter
	// cannot be set on it.
	const ibValueMetaObjectAttributeBase* attribute = metaObject->FindAnyAttributeObjectByFilter(m_metaId);
	if (attribute == nullptr)
		return false;

	switch (lPropNum) {
	case eValue:
		m_recordSet->SetKeyValue(m_metaId, varPropVal);
		return true;
	case eUse:
		if (varPropVal.GetBoolean())
			m_recordSet->SetKeyValue(m_metaId, attribute->CreateValue());
		else
			m_recordSet->EraseKeyValue(m_metaId);
		return true;
	}

	return false;
}

bool ibValueRecordSetObject::ibValueRecordSetObjectRegisterKeyValue::ibValueRecordSetObjectRegisterKeyDescriptionValue::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	const ibValueMetaObjectRegisterData* metaObject = m_recordSet->GetMetaObject();
	wxASSERT(metaObject);

	const ibValueMetaObjectAttributeBase* attribute = metaObject->FindAnyAttributeObjectByFilter(m_metaId);
	if (attribute == nullptr)
		return false;   // switched off — see SetPropVal

	switch (lPropNum) {
	case eValue:
		if (m_recordSet->FindKeyValue(m_metaId))
			pvarPropVal = m_recordSet->GetKeyValue(m_metaId);
		else
			pvarPropVal = attribute->CreateValue();
		return true;
	case eUse:
		pvarPropVal = m_recordSet->FindKeyValue(m_metaId);
		return true;
	}

	return false;
}

//////////////////////////////////////////////////////////////

bool ibValueRecordKeyObject::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case enEmpty:
		pvarRetValue = IsEmpty();
		return true;
	case enMetadata:
		pvarRetValue = m_metaObject;
		return true;
	}
	return false;
}

//////////////////////////////////////////////////////////////

bool ibValueRecordSetObject::ibValueRecordSetObjectRegisterKeyValue::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	return false;
}

//////////////////////////////////////////////////////////////

bool ibValueRecordSetObject::ibValueRecordSetObjectRegisterKeyValue::ibValueRecordSetObjectRegisterKeyDescriptionValue::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case enSet:
		// ⚠ THE VALUE, NOT THE POINTER TO IT. `paParams` is an array of ibValue*, and SetKeyValue is a
		// template — handed the pointer it deduced `ibValue*` and stored a value wrapping the pointer
		// instead of the reference the caller passed. `Filter.X.Set(ref)` then hung the runtime the
		// first time anything read that entry back (2026-09-05, the first call ever made from outside
		// the set's own module — the property was unreachable until then, which is why nothing had
		// tripped over it).
		if (lSizeArray > 0 && paParams[0] != nullptr)
			m_recordSet->SetKeyValue(m_metaId, *paParams[0]);
		return true;
	}
	return false;
}


//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

SYSTEM_TYPE_REGISTER(ibValueRecordDataObjectRecorderRef::ibRecorderRegister, "RecordRegister", system_to_clsid("VL_RECR"));

SYSTEM_TYPE_REGISTER(ibValueRecordSetObject::ibValueRecordSetObjectRegisterColumnCollection, "RecordSetRegisterColumn", system_to_clsid("VL_RSCL"));
SYSTEM_TYPE_REGISTER(ibValueRecordSetObject::ibValueRecordSetObjectRegisterColumnCollection::ibValueRecordSetRegisterColumnInfo, "RecordSetRegisterColumnInfo", system_to_clsid("VL_RSCI"));

SYSTEM_TYPE_REGISTER(ibValueRecordSetObject::ibValueRecordSetObjectRegisterKeyValue, "RecordSetRegisterKey", system_to_clsid("VL_RSCK"));
SYSTEM_TYPE_REGISTER(ibValueRecordSetObject::ibValueRecordSetObjectRegisterKeyValue::ibValueRecordSetObjectRegisterKeyDescriptionValue, "RecordSetRegisterKeyDescription", system_to_clsid("VL_RDVL"));
