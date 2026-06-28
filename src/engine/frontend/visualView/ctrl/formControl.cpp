////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : form - controls 
////////////////////////////////////////////////////////////////////////////

#include "form.h"


//////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////

ibValueForm::ibValueFormCollectionControl::ibValueFormCollectionControl() : ibValueDynamicMembers(ibValueTypes::TYPE_VALUE, true),
m_formOwner(nullptr)
{
}

ibValueForm::ibValueFormCollectionControl::ibValueFormCollectionControl(ibValueForm* ownerFrame) : ibValueDynamicMembers(ibValueTypes::TYPE_VALUE, true),
m_formOwner(ownerFrame)
{
	m_members.Bind(this, &ibValueFormCollectionControl::FillMembers);
}

#include "backend/system/value/valueMap.h"

ibValueForm::ibValueFormCollectionControl::~ibValueFormCollectionControl()
{
}

// Walk the control hierarchy (m_children), collecting controls and skipping
// sizer-items — the source of truth for the form's "Controls" collection.
// Replaces the former maintained m_listControl set.
static void CollectFormControls(const ibValueFrame* node, std::vector<ibValueControl*>& list)
{
	for (unsigned int idx = 0; idx < node->GetChildCount(); idx++) {
		ibValueFrame* child = node->GetChild(idx);
		if (child == nullptr)
			continue;
		if (child->GetComponentType() != COMPONENT_TYPE_SIZERITEM) {
			if (ibValueControl* control = dynamic_cast<ibValueControl*>(child))
				list.push_back(control);
		}
		CollectFormControls(child, list);
	}
}

std::vector<ibValueControl*> ibValueForm::GetControlList() const
{
	std::vector<ibValueControl*> list;
	CollectFormControls(this, list);
	return list;
}

std::shared_ptr<ibValueIteratorState> ibValueForm::ibValueFormCollectionControl::CreateIterator()
{
	using ListT = std::vector<ibValueControl*>;
	class State : public ibValueIteratorState {
	public:
		explicit State(ListT list) : m_list(std::move(list)), m_it(m_list.begin()) {}
		bool MoveNext(ibValue& current) override {
			if (m_started) ++m_it; else m_started = true;
			if (m_it == m_list.end()) return false;
			ibValue controlValue(*m_it);
			current = ibValue(static_cast<ibValue*>(
				new ibValueContainer::ibValueReturnContainer(
					(*m_it)->GetControlName(), controlValue)));
			return true;
		}
		void Reset() override { m_it = m_list.begin(); m_started = false; }
		bool PeekSample(ibValue& current) const override {
			current = ibValue(static_cast<ibValue*>(
				new ibValueContainer::ibValueReturnContainer()));
			return true;
		}
	private:
		ListT m_list;
		ListT::const_iterator m_it;
		bool m_started = false;
	};
	return std::make_shared<State>(m_formOwner->GetControlList());
}

bool ibValueForm::ibValueFormCollectionControl::GetAt(const ibValue& varKeyValue, ibValue& pvarValue)
{
	const std::vector<ibValueControl*> list = m_formOwner->GetControlList();
	const ibNumber& number = varKeyValue.GetNumber();
	if (number.ToUInt() >= list.size())
		return false;

	pvarValue = list[number.ToUInt()];
	return true;
}

bool ibValueForm::ibValueFormCollectionControl::Property(const ibValue& varKeyValue, ibValue& cValueFound)
{
	const wxString& key = varKeyValue.GetString();
	const std::vector<ibValueControl*> list = m_formOwner->GetControlList();
	auto it = std::find_if(list.begin(), list.end(),
		[key](ibValueControl* control) {
			return stringUtils::CompareString(key, control->GetControlName());
		}
	);

	if (it != list.end()) {
		cValueFound = *it;
		return true;
	}

	return false;
}

enum
{
	enControlCreate,
	enControlFind,
	enControlRemove,
	enControlProperty,
	enControlCount
};

void ibValueForm::ibValueFormCollectionControl::FillMembers(ibMemberTable& helper) const
{
	helper.AppendFunc(wxT("CreateControl"), 2, wxT("CreateControl(typeControl : type, parentElement : frame)"));
	helper.AppendFunc(wxT("FindControl"), 1, wxT("FindControl(controlName : string)"));
	helper.AppendProc(wxT("RemoveControl"), 1, wxT("RemoveControl(controlElement : frame)"));
	helper.AppendFunc(wxT("Property"), 2, wxT("Property(key : string, valueFound : frame)"));
	helper.AppendFunc(wxT("Count"), wxT("Count()"));

	wxString controlName;

	for (auto control : m_formOwner->GetControlList()) {

		if (!control->GetControlNameAsString(controlName))
			continue;

		helper.AppendProp(
			controlName,
			true,
			false,
			control->GetControlID()
		);
	}
}

bool ibValueForm::ibValueFormCollectionControl::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	wxASSERT(m_formOwner);
	pvarPropVal = m_formOwner->FindControlByID(
		m_members.GetPropData(lPropNum)
	);
	return !pvarPropVal.IsEmpty();
}

#include "backend/system/value/valueType.h"

bool ibValueForm::ibValueFormCollectionControl::CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case enControlRemove:
		m_formOwner->RemoveControl(paParams[0]->ConvertToType<ibValueFrame>());
		return true;
	}
	return false;
}

bool ibValueForm::ibValueFormCollectionControl::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case enControlCreate:
		pvarRetValue = m_formOwner->CreateControl(paParams[0]->ConvertToType<ibValueType>(), lSizeArray > 1 ? paParams[1]->ConvertToType<ibValueFrame>() : ibValue());
		return true;
	case enControlFind:
		pvarRetValue = m_formOwner->FindControl(paParams[0]->GetString());
		return true;
	case enControlProperty:
	{	ibValue defaultVal;
		pvarRetValue = Property(*paParams[0], lSizeArray > 1 ? *paParams[1] : defaultVal); }
		return true;
	case enControlCount:
		pvarRetValue = Count();
		return true;
	}

	return false;
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

SYSTEM_TYPE_REGISTER(ibValueForm::ibValueFormCollectionControl, "FormControl", system_to_clsid("VL_CNTR"));
