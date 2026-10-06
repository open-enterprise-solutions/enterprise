#include "typeControl.h"
#include "backend/metaCollection/partial/commonObject.h"
#include "backend/objCtor.h"
#include "backend/metaData.h"

#include <algorithm>   // std::find — a type offered once, whichever "any" named it

#include "sfrontend/visualView/ctrl/frame.h"
#include "sfrontend/visualView/choiceRequest.h"                      // ibRequestChoice — "pick one of these"
#include "backend/backend_mainFrame.h"                                    // the frame a request to the person goes through
#include "backend/session/session.h"
#include "backend/system/value/valueType.h"          // ibValueTypeDescription / g_valueTypeDescriptionCLSID
#include "backend/choiceLinkResolver.h"               // what narrows this choice — type and conditions
#include "backend/metaCollection/attribute/metaAttributeObject.h"   // the bound attribute holds both

bool ibTypeControlFactory::ChooseValue(ibControlFrame* ownerValue, const ibValueMetaObject* choiceForm)
{
	ibTypeControlFactory* factory = dynamic_cast<ibTypeControlFactory*>(ownerValue);
	if (ownerValue == nullptr || factory == nullptr)
		return false;

	ibValue current;
	ownerValue->GetControlValue(current);

	// UNDEFINED = the type is not settled yet. GetDataType answers it — from the metadata by default, asking the
	// person only when the cell admits more than one type, and overridden outright by a cell that already knows
	// (a filter's left side is always a field). Settled, the cell stands on it, and the value of that type is
	// chosen right after, in the same press.
	if (current.GetType() == ibValueTypes::TYPE_EMPTY) {
		const ibClassID clsid = factory->GetDataType();
		const ibMetaData* metaData = factory->GetMetaData();
		if (clsid == 0 || metaData == nullptr || !metaData->IsRegisterCtor(clsid))
			return false;   // the person closed the type choice
		current = metaData->CreateObject(clsid);
		ownerValue->SetControlValue(current);   // the cell now stands on its settled type
	}

	// THE VALUE OF THAT TYPE: the built-in quick choice first (it knows a boolean,
	// an enumeration, a reference), then the metaobject's own selection form.
	const ibClassID clsid = current.GetClassType();

	// A TYPE DESCRIPTION is edited by the type picker, an editor of its own — a protocol service beside the
	// runtime, as every editor outside it is.
	if (clsid == g_valueTypeDescriptionCLSID)
		return false;

	const ibMetaData* metaData = factory->GetMetaData();

	if (ibTypeControlFactory::QuickChoice(ownerValue, clsid))
		return true;

	const ibCtorMetaValueType* so = metaData != nullptr ? metaData->GetTypeCtor(clsid) : nullptr;
	if (so != nullptr && so->GetMetaTypeCtor() == ibCtorObjectMetaType_Reference) {
		if (const ibValueMetaObject* metaObject = so->GetMetaObject()) {
			// ⭐ WHAT NARROWS THIS CHOICE — two things the control supplies and nothing it works out:
			// the FIELD being filled, and WHERE the values its link names are read. The second is the
			// control's own answer — the form's source for a control on a form, the row being edited
			// for a table column — so the list is narrowed by exactly what the person can see beside
			// the field they are filling.
			const ibChoiceHolder holder = factory->GetChoiceHolder();
			const ibChoiceCondition condition = ibChoiceLinkResolver::Resolve(holder,
				ibChoiceLinkResolver::FieldOf(holder, factory));

			// ⭐ WHAT THE FORM IS MADE WITH: which form the author picked, and — as one named part of
			// it — the choice. The condition goes in WHOLE, empty or not: "nothing narrows this" is a
			// condition with nothing in it, and the list asks the same question of both.
			const ibFormRequest request(
				choiceForm != nullptr ? choiceForm->GetName() : wxString(),
				ibCreateRequest(factory->GetSelectMode(), condition));
			return metaObject->ProcessChoice(ownerValue, request);
		}
	}
	return false;
}

bool ibTypeControlFactory::QuickChoice(ibControlFrame* ownerValue, const ibClassID& clsid)
{
	if (ownerValue == nullptr || !ownerValue->HasQuickChoice())
		return false;

	ibValue current;
	ownerValue->GetControlValue(current);

	std::vector<ibValue> values;
	if (!current.FindValue(wxEmptyString, values))
		return false;

	std::vector<ibChoiceItem> items;
	for (size_t idx = 0; idx < values.size(); idx++)
		items.push_back(ibChoiceItem{ static_cast<s32>(idx), values[idx].GetString(), wxIcon(), values[idx] == current });

	s32 chosen = 0;
	if (ibRequestChoice(wxString(), items, chosen))
		ownerValue->ChoiceProcessing(values[static_cast<size_t>(chosen)]);
	return true;   // the type has a quick choice — offered, whatever the person did with it
}

void ibTypeControlFactory::QuickChoice(ibControlFrame* controlValue, ibValue& newValue, const wxString& strData)
{
	if (controlValue == nullptr)
		return;

	if (strData.Length() == 0) {
		controlValue->ChoiceProcessing(newValue);
		return;
	}

	std::vector<ibValue> values;
	if (!newValue.FindValue(strData, values) || values.empty())
		return;

	if (values.size() == 1) {
		controlValue->ChoiceProcessing(values.front());
		return;
	}

	std::vector<ibChoiceItem> items;
	for (size_t idx = 0; idx < values.size(); idx++)
		items.push_back(ibChoiceItem{ static_cast<s32>(idx), values[idx].GetString(), wxIcon(), values[idx] == newValue });

	ibBackendDocFrame* const frame = ibSession::CurrentFrame();
	if (frame == nullptr)
		return;

	// Several match what was typed: the person picks one — or, closing the list, is asked whether to give the
	// typed text up (the field goes back to what it held) or to pick after all.
	for (;;) {
		s32 chosen = 0;
		if (ibRequestChoice(wxString(), items, chosen)) {
			controlValue->ChoiceProcessing(values[static_cast<size_t>(chosen)]);
			return;
		}
		const int answer = frame->ShowModalMessage(
			_("Incorrect data entered into field. Do you want to cancel?"), wxString(),
			wxYES_NO | wxICON_QUESTION);
		if (answer != wxNO) {
			ibValue retValue;
			if (controlValue->GetControlValue(retValue))
				controlValue->ChoiceProcessing(retValue);
			return;
		}
	}
}

/////////////////////////////////////////////////////////////////////

ibSelectMode ibTypeControlFactory::GetSelectMode() const
{
	// Select mode is a metaobject-attribute concern — a single reference to a HIERARCHICAL catalog
	// carries Items / Folders / FoldersAndItems. Resolve the bound leaf and, WHEN it is a metadata
	// attribute, read its mode; a plain column (a dynamic list's queryable column) has none →
	// default to item selection.
	const ibValueMetaObjectAttributeBase* attr =
		ibChoiceLinkResolver::FieldOf(GetChoiceHolder(), this);
	if (attr != nullptr) return attr->GetSelectMode();
	return ibSelectMode::ibSelectMode_Items;
}

ibValue ibTypeControlFactory::CreateValue() const
{
	// Value creation is the FACTORY's job — it knows its bound Type (GetTypeDesc); delegating to
	// the source attribute was a duplicate of exactly this.
	return ibBackendTypeSourceFactory::CreateValue();
}

ibClassID ibTypeControlFactory::GetDataType() const
{
	// Type + metadata come from the factory itself — its bound source property already reflects
	// the resolved field's Type.
	return ShowSelectType(GetMetaData(), GetTypeValueDesc());
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

	// Several: the person picks one — each type by the name and picture its constructor gives it.
	std::vector<ibChoiceItem> items;
	for (size_t idx = 0; idx < offered.size(); idx++) {
		const ibCtorAbstractType* ctor = metaData != nullptr ? metaData->GetAvailableCtor(offered[idx]) : nullptr;
		items.push_back(ibChoiceItem{ static_cast<s32>(idx),
			ctor != nullptr ? ctor->GetClassName() : wxString(),
			ctor != nullptr ? ctor->GetClassIcon() : wxIcon() });
	}

	s32 chosen = 0;
	return ibRequestChoice(_("Select data type"), items, chosen) ? offered[static_cast<size_t>(chosen)] : 0;
}
