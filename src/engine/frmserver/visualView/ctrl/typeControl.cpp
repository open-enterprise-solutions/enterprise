#include "typeControl.h"
#include "backend/metaCollection/partial/commonObject.h"
#include "backend/objCtor.h"
#include "backend/metaData.h"
#include "backend/metadataSerialization.h"

#include <algorithm>   // std::find — a type offered once, whichever "any" named it

#include "frmserver/visualView/ctrl/frame.h"
#include "frmserver/visualView/ctrl/form.h"                           // the form — the source a control's own links are read in
#include "frmserver/visualView/choiceRequest.h"                      // ibRequestChoice, ibRequestQuickChoice — "pick one of these"
#include "backend/backend_mainFrame.h"                                    // the frame a request to the person goes through
#include "core/serialize/dataBuilder.h"                                // the request and its answer
#include "protocol/protocol.h"                                            // ibProtocol::ibRequest::ibKind::SimpleChoice
#include "backend/session/session.h"
#include "backend/system/value/valueType.h"          // ibValueTypeDescription / g_valueTypeDescriptionCLSID
#include "backend/choiceLinkResolver.h"               // what narrows this choice — type and conditions
#include "backend/metaCollection/metaObject.h"                      // the metaobject a choice opens
#include "frmserver/win/dlgs/selectPredefined.h"   // ibShowPredefinedSelector — the designer's declared values
#include "backend/appData.h"                        // DesignerMode — the two roads part here

bool ibTypeControlFactory::ChooseValue(ibControlFrame* ownerValue, const ibValueMetaObject* choiceForm)
{
	ibTypeControlFactory* factory = dynamic_cast<ibTypeControlFactory*>(ownerValue);
	if (ownerValue == nullptr || factory == nullptr)
		return false;

	ibValue current;
	ownerValue->GetControlValue(current);
	const ibMetaData* metaData = factory->GetMetaData();

	// UNDEFINED = the type is not settled yet. The control's own Type answers it (ShowSelectType over what a value of it
	// may be), asking the person only when the cell admits more than one type. Settled, the cell stands on it, and the
	// value of that type is chosen right after, in the same press.
	if (current.GetType() == ibValueTypes::TYPE_EMPTY) {
		// THE DESIGNER'S OWN WINDOW asks both — the type and a declared value of it — so an empty cell reaches a value
		// in one click, as the desktop's. Whether anything referenceable is admitted is the window's question: it
		// answers `false` for a declaration with none, and the road below carries on unchanged.
		if (appData->DesignerMode() && ibShowPredefinedSelector(ownerValue, factory->GetTypeDesc(), metaData))
			return true;

		const ibClassID settled = ShowSelectType(metaData, factory->GetTypeValueDesc());
		if (settled == 0 || metaData == nullptr || !metaData->IsRegisterCtor(settled))
			return false;   // the person closed the type choice
		current = metaData->CreateObject(settled);
		ownerValue->SetControlValue(current);   // the cell now stands on its settled type
	}

	// THE VALUE OF THAT TYPE: the built-in quick choice first (it knows a boolean,
	// an enumeration, a reference), then the metaobject's own selection form.
	const ibClassID clsid = current.GetClassType();

	// A TYPE DESCRIPTION is edited by the type picker, an editor of its own — a protocol service beside the
	// runtime, as every editor outside it is.
	if (clsid == g_valueTypeDescriptionCLSID)
		return false;

	// IN THE DESIGNER EVERY REFERENCE ASKS ITS DECLARED VALUES — a catalog, a document and an enumeration, one window,
	// one habit (Max, 2026-08-28). A cell that declares nothing is standing on the type it already holds.
	if (appData->DesignerMode()) {
		const ibTypeDescription& declared = factory->GetTypeDesc();
		if (ibShowPredefinedSelector(ownerValue,
				declared.GetClsidList().empty() ? ibTypeDescription(clsid) : declared, metaData))
			return true;
	}

	if (QuickChoice(ownerValue, current, metaData))
		return true;

	const ibCtorMetaValueType* so = metaData != nullptr ? metaData->GetTypeCtor(clsid) : nullptr;
	if (so != nullptr && so->GetMetaTypeCtor() == ibCtorObjectMetaType_Reference) {
		if (const ibValueMetaObject* metaObject = so->GetMetaObject()) {
			// ⭐ WHAT NARROWS THIS CHOICE — two things the control supplies and nothing it works out:
			// the FIELD being filled, and WHERE the values its link names are read. The second is the
			// control's own answer — the form's source for a control on a form, the row being edited
			// for a table column — so the list is narrowed by exactly what the person can see beside
			// the field they are filling; the control's own rows over it are read in the form.
			const ibChoiceCondition condition = ibChoiceLinkResolver::Resolve(factory->GetChoiceHolder(),
				factory->GetSourceDesc().GetLeaf(), factory->GetChoiceParameters(),
				ibChoiceHolder(ownerValue->GetOwnerForm()));

			// ⭐ WHAT THE FORM IS MADE WITH: which form the author picked, and — as one named part of
			// it — the choice. The condition goes in WHOLE, empty or not: "nothing narrows this" is a
			// condition with nothing in it, and the list asks the same question of both.
			const ibFormRequest request(
				choiceForm != nullptr ? choiceForm->GetName() : ibString(),
				ibCreateRequest(factory->GetSelectMode(), condition));
			return metaObject->ProcessChoice(ownerValue, request);
		}
	}
	return false;
}

bool ibTypeControlFactory::FindValue(ibControlFrame* ownerValue, const ibValue& value, const ibString& input,
	std::vector<ibValue>& out)
{
	const ibTypeControlFactory* factory = dynamic_cast<const ibTypeControlFactory*>(ownerValue);
	if (factory == nullptr)
		return false;

	// The same condition the choice form is made with (ChooseValue) — typing finds no more than the form shows.
	const ibChoiceCondition condition = ibChoiceLinkResolver::Resolve(factory->GetChoiceHolder(),
		factory->GetSourceDesc().GetLeaf(), factory->GetChoiceParameters(), ibChoiceHolder(ownerValue->GetOwnerForm()));
	return value.FindValue(input, condition, out);
}

void ibTypeControlFactory::ClearLinked(ibControlFrame* changed, const ibValue& before)
{
	const ibTypeControlFactory* factory = dynamic_cast<const ibTypeControlFactory*>(changed);
	ibValueForm* const form = changed != nullptr ? changed->GetOwnerForm() : nullptr;
	if (factory == nullptr || form == nullptr)
		return;

	// On a CHANGE only — what the control shows now, after its own adjustment; writing what is already there makes
	// nothing stale, and is where a chain that names itself in a circle stops.
	ibValue after;
	changed->GetControlValue(after);
	if (after.GetClassType() == before.GetClassType() && after == before)
		return;

	const ibSourceDescription& edited = factory->GetSourceDesc();
	for (ibValueControl* control : form->GetControlList()) {
		ibTypeControlFactory* linked = dynamic_cast<ibTypeControlFactory*>(control);
		if (linked != nullptr && linked != factory
			&& ibChoiceLinkResolver::IsLinked(linked->GetTypeLink(), linked->GetChoiceParameters(), edited))
			control->SetControlValue(linked->AdjustValue());
	}
}

bool ibTypeControlFactory::SimpleChoice(ibControlFrame* ownerValue, const ibValue& current, const ibMetaData* metaData)
{
	const ibValueTypes valType = ibValue::GetVTByID(current.GetClassType());
	if (ownerValue == nullptr || (valType != ibValueTypes::TYPE_NUMBER && valType != ibValueTypes::TYPE_DATE))
		return false;

	ibBackendDocFrame* const frame = ibSession::CurrentFrame();
	if (frame == nullptr)
		return false;

	ibDataNode request;
	request.SetValue(ibProtocol::ibRequest::Kind, static_cast<std::int32_t>(ibProtocol::ibRequest::ibKind::SimpleChoice));
	current.Serialize(request.Child(ibProtocol::ibRequest::ibSimpleChoice::Value));

	ibDataNode response;
	if (frame->Request(request, response)) {
		if (const auto chosen = response.FindChild(ibProtocol::ibRequest::ibSimpleChoice::Value)) {
			ibValue value = ibDeserializeValue(*chosen, metaData);
			ownerValue->ChoiceProcessing(value);
		}
	}
	return true;   // the type has its own window — shown, whatever the person did with it
}

bool ibTypeControlFactory::QuickChoice(ibControlFrame* ownerValue, const ibValue& current, const ibMetaData* metaData)
{
	if (ownerValue == nullptr || !ownerValue->HasQuickChoice())
		return false;

	// A number and a date first, on their own windows — the desktop's order.
	if (SimpleChoice(ownerValue, current, metaData))
		return true;

	// The short list, among what the choice is made under — the form's list would show no more.
	std::vector<ibValue> values;
	if (!FindValue(ownerValue, current, ibEmptyString, values))
		return false;

	std::vector<ibChoiceItem> items;
	for (size_t idx = 0; idx < values.size(); idx++)
		items.push_back(ibChoiceItem{ static_cast<std::int32_t>(idx), values[idx].GetString(), ibPictureDescription(), values[idx] == current });

	std::int32_t chosen = 0;
	if (ibRequestQuickChoice(items, chosen))
		ownerValue->ChoiceProcessing(values[static_cast<size_t>(chosen)]);
	return true;   // the type has a quick choice — offered, whatever the person did with it
}

/////////////////////////////////////////////////////////////////////

ibSelectMode ibTypeControlFactory::GetSelectMode() const
{
	// A single reference to a HIERARCHICAL catalog carries Items / Folders / FoldersAndItems — asked of the
	// source holding the bound field, by the binding's leaf; a field it does not answer for (a dynamic list's
	// queryable column) chooses items.
	return GetChoiceHolder().GetSelectMode(GetSourceDesc().GetLeaf());
}

const ibChoiceTypeLinkDescription& ibTypeControlFactory::GetTypeLink() const
{
	static const ibChoiceTypeLinkDescription s_none;
	return s_none;
}

const ibChoiceParametersDescription& ibTypeControlFactory::GetChoiceParameters() const
{
	static const ibChoiceParametersDescription s_none;
	return s_none;
}

ibClassID ibTypeControlFactory::ShowSelectType(const ibMetaData* metaData, const ibTypeDescription& typeDescription)
{
	// WHAT CAN BE CHOSEN: an "any" among the types — `CatalogRef`, `AnyRef` — offers its facade, every reference
	// its bits admit (clsid_admits): nothing is ever "a CatalogRef".
	std::vector<ibClassID> offered;
	const auto offer = [&offered](const ibClassID& clsid) {
		if (std::find(offered.begin(), offered.end(), clsid) == offered.end())
			offered.push_back(clsid);
	};
	for (const ibClassID& clsid : typeDescription.GetClsidList()) {
		if (!::IsReference(clsid) || !clsid_is_any(clsid) || metaData == nullptr) {
			offer(clsid);
			continue;
		}
		for (const ibCtorMetaValueType* member : metaData->GetListCtorsByType(ibCtorObjectMetaType::ibCtorObjectMetaType_Reference))
			if (clsid_admits(clsid, member->GetClassType()))
				offer(member->GetClassType());
	}
	if (offered.size() < 2) return offered.empty() ? 0 : offered.front();

	// Several: the person picks one — each type by the name its constructor gives it, and its class's picture.
	std::vector<ibChoiceItem> items;
	for (size_t idx = 0; idx < offered.size(); idx++) {
		const ibCtorAbstractType* ctor = metaData != nullptr ? metaData->GetAvailableCtor(offered[idx]) : nullptr;
		items.push_back(ibChoiceItem{ static_cast<std::int32_t>(idx),
			ctor != nullptr ? ctor->GetClassName() : ibString(),
			ctor != nullptr ? ibPictureDescription(offered[idx]) : ibPictureDescription() });
	}

	std::int32_t chosen = 0;
	return ibRequestChoice(_("Select data type"), items, chosen) ? offered[static_cast<size_t>(chosen)] : 0;
}
