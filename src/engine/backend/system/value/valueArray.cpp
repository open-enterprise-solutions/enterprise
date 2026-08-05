////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : array value   
////////////////////////////////////////////////////////////////////////////

#include "valueArray.h"
#include "backend/backend_exception.h"
#include "backend/compiler/procUnit.h"        // InvokeLambdaWithArg for selector overloads
#include "backend/compiler/procUnitValues.h"  // CopyValue / SetTypeNumber / SetTypeBoolean / IsHasValue



//////////////////////////////////////////////////////////////////////

bool ibValueArray::Init()
{
	return true;
}

bool ibValueArray::Init(ibValue** paParams, const long lSizeArray)
{
	if (lSizeArray < 1)
		return false;

	if (paParams[0]->GetType() == ibValueTypes::TYPE_NUMBER) {
		const ibNumber& number = paParams[0]->GetNumber();
		if (number > 0) {
			m_listValue.resize(number.ToUInt());
			return true;
		}
	}
	return false;
}

#include "appData.h"

void ibValueArray::CheckIndex(unsigned int index) const //array index must start from 1
{
	// `index` is unsigned, so `index < 0` was dead code, and && binds tighter than ||
	// — the condition already meant "out of range AND not in the designer". Spelled out;
	// the designer-mode exemption is preserved, not introduced (see docs/portability.md).
	if (index >= m_listValue.size() && !appData->DesignerMode())
		ibBackendCoreException::Error(_("Index goes beyond array"));
}

//working with an array as an aggregate object
//listing string keys
// Type-invariant contributor (Shared<>). Build() Clear()s first; ctx unused.
void ibValueArray_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	helper.AppendConstructor(1, wxT("Array(num : number)"));

	helper.AppendFunc(wxT("Add"), 1, wxT("Add(value : any)"));
	helper.AppendFunc(wxT("Insert"), 2, wxT("Insert(index, value : any)"));

	helper.AppendFunc(wxT("Count"), wxT("Count()"));
	helper.AppendFunc(wxT("Find"), 1, wxT("Find(value : any)"));
	helper.AppendFunc(wxT("Clear"), wxT("Clear()"));
	helper.AppendFunc(wxT("Get"), 1, wxT("Get(index)"));
	helper.AppendFunc(wxT("Set"), 2, wxT("Set(index, value : any)"));
	helper.AppendFunc(wxT("Remove"), 1, wxT("Remove(index : number)"));
	helper.AppendFunc(wxT("Contains"), 1, wxT("Contains(value : any)"));
	helper.AppendFunc(wxT("Sort"), 1, wxT("Sort(descending : bool)"));
	helper.AppendFunc(wxT("SortByKeys"), 2, wxT("SortByKeys(keys : Array, descending : bool)"));

	// Aggregations — no-arg form operates element-wise via ibValue
	// arithmetic / comparison; optional selector lambda projects
	// each element through λ(x) → numeric before accumulation.
	// Empty array returns TYPE_EMPTY.
	helper.AppendFunc(wxT("Sum"),     1, wxT("Sum(selector? : Function)"));
	helper.AppendFunc(wxT("Min"),     1, wxT("Min(selector? : Function)"));
	helper.AppendFunc(wxT("Max"),     1, wxT("Max(selector? : Function)"));
	helper.AppendFunc(wxT("Average"), 1, wxT("Average(selector? : Function)"));
}

bool ibValueArray::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case enAdd: 
		Add(*paParams[0]);
		return true;
	case enInsert: 
		Insert(paParams[0]->GetUInteger(), *paParams[1]); 
		return true;
	case enCount:
		pvarRetValue = Count();
		return true;
	case enFind: 
		pvarRetValue = Find(*paParams[0]);
		return true;
	case enClear: 
		Clear(); 
		return true;
	case enGet:
		return GetAt(*paParams[0], pvarRetValue);
	case enSet:  
		return SetAt(*paParams[0], *paParams[1]);
	case enRemove:
		Remove(paParams[0]->GetUInteger());
		return true;
	case enContains:
		pvarRetValue = ibValue(Contains(*paParams[0]));
		return true;
	case enSort:
		Sort(paParams[0]->GetBoolean());
		return true;
	case enSortByKeys:
	{
		ibValueArray* keys = paParams[0]->ConvertToType<ibValueArray>();
		if (keys != nullptr) {
			SortByKeys(*keys, paParams[1]->GetBoolean());
		}
		return true;
	}
	case enSum:
		pvarRetValue = (lSizeArray > 0 && paParams[0] != nullptr)
			? SumWithSelector(*paParams[0])
			: Sum();
		return true;
	case enMin:
		pvarRetValue = (lSizeArray > 0 && paParams[0] != nullptr)
			? MinWithSelector(*paParams[0])
			: Min();
		return true;
	case enMax:
		pvarRetValue = (lSizeArray > 0 && paParams[0] != nullptr)
			? MaxWithSelector(*paParams[0])
			: Max();
		return true;
	case enAverage:
		pvarRetValue = (lSizeArray > 0 && paParams[0] != nullptr)
			? AverageWithSelector(*paParams[0])
			: Average();
		return true;
	}

	return false;
}

// Selector-lambda aggregation variants. Each element is projected
// through λ(x), result accumulates as in the no-selector overload.
// Empty array → TYPE_EMPTY. Throws via InvokeLambdaWithArg if
// `selector` isn't a lambda value.

ibValue ibValueArray::SumWithSelector(ibValue& selector) const
{
	if (m_listValue.empty()) return ibValue();
	ibValue acc, projected;
	bool first = true;
	for (const auto& v : m_listValue) {
		ibValue elem = v;   // CallLambdaWithArg expects mutable ref
		if (!InvokeLambdaWithArg(selector, elem, projected))
			ibBackendCoreException::Error(_("Sum(selector): expected a function value as selector"));
		if (first) { acc = projected; first = false; }
		else       { acc = acc + projected; }
	}
	return acc;
}

ibValue ibValueArray::MinWithSelector(ibValue& selector) const
{
	if (m_listValue.empty()) return ibValue();
	ibValue best, projected;
	bool first = true;
	for (const auto& v : m_listValue) {
		ibValue elem = v;
		if (!InvokeLambdaWithArg(selector, elem, projected))
			ibBackendCoreException::Error(_("Min(selector): expected a function value as selector"));
		if (first || projected < best) { best = projected; first = false; }
	}
	return best;
}

ibValue ibValueArray::MaxWithSelector(ibValue& selector) const
{
	if (m_listValue.empty()) return ibValue();
	ibValue best, projected;
	bool first = true;
	for (const auto& v : m_listValue) {
		ibValue elem = v;
		if (!InvokeLambdaWithArg(selector, elem, projected))
			ibBackendCoreException::Error(_("Max(selector): expected a function value as selector"));
		if (first || best < projected) { best = projected; first = false; }
	}
	return best;
}

ibValue ibValueArray::AverageWithSelector(ibValue& selector) const
{
	if (m_listValue.empty()) return ibValue();
	const ibNumber count(static_cast<int64_t>(m_listValue.size()));
	const ibValue total = SumWithSelector(selector);
	// No display trim — see no-selector Average() comment.
	return ibValue(total.GetNumber() / count);
}

// LINQ virtual-dispatch override — see header. Intercepted ops
// short-circuit to direct vector access; rest delegates to base.
void ibValueArray::DispatchLinqMethod(ibLinqMethod method, ibValue& ret,
                                       ibValue** args, long n)
{
	using M = ibLinqMethod;
	switch (method) {
		case M::Count: // O(1) via m_listValue.size()
		{
			SetTypeNumber(ret, (long)m_listValue.size());
			return;
		}
		case M::ToArray: // array already materialised; return fresh shallow copy
		{
			ibValueArray* dst = new ibValueArray();
			for (const auto& v : m_listValue) dst->Add(v);
			CopyValue(ret, ibValue(dst));
			return;
		}
		case M::ElementAt:          // O(1) direct index
		case M::ElementAtOrDefault:
		{
			if (n < 1 || args == nullptr || args[0] == nullptr)
				ibBackendCoreException::Error(_("LINQ: ElementAt requires an index"));
			const long idx = (long)args[0]->GetNumber().ToInt64();
			if (idx < 0 || idx >= (long)m_listValue.size()) {
				if (method == M::ElementAt)
					ibBackendCoreException::Error(_("LINQ: ElementAt - index out of range"));
				CopyValue(ret, ibValue());
				return;
			}
			CopyValue(ret, m_listValue[idx]);
			return;
		}
		case M::Contains: // linear via ibValue::operator==
		{
			if (n < 1 || args == nullptr || args[0] == nullptr)
				ibBackendCoreException::Error(_("LINQ: Contains requires a value"));
			const ibValue& target = *args[0];
			for (const auto& v : m_listValue) {
				if (v == target) { SetTypeBoolean(ret, true); return; }
			}
			SetTypeBoolean(ret, false);
			return;
		}
		case M::Last:
		case M::LastOrDefault:
		{
			if (m_listValue.empty()) {
				if (method == M::Last)
					ibBackendCoreException::Error(_("LINQ: Last() on empty sequence"));
				CopyValue(ret, ibValue());
				return;
			}
			CopyValue(ret, m_listValue.back());
			return;
		}
		case M::First:
		case M::FirstOrDefault:
		{
			if (m_listValue.empty()) {
				if (method == M::First)
					ibBackendCoreException::Error(_("LINQ: First() on empty sequence"));
				CopyValue(ret, ibValue());
				return;
			}
			CopyValue(ret, m_listValue.front());
			return;
		}
		case M::Any: // O(1) empty check
		{
			SetTypeBoolean(ret, !m_listValue.empty());
			return;
		}
		default:
			// Fall through to base impl — handles Where/Select/OrderBy/
			// GroupBy/Join/Skip/Take/Aggregate/etc via CreateIterator
			// + state-class machinery.
			ibValue::DispatchLinqMethod(method, ret, args, n);
			return;
	}
}

bool ibValueArray::GetAt(const ibValue& varKeyValue, ibValue& pvarValue) //array index must start from 0
{
	CheckIndex(varKeyValue.GetUInteger());
	pvarValue = m_listValue[varKeyValue.GetUInteger()];
	return true;
}

bool ibValueArray::SetAt(const ibValue& varKeyValue, const ibValue& varValue)//array index must start from 0
{
	CheckIndex(varKeyValue.GetUInteger());
	m_listValue[varKeyValue.GetUInteger()] = varValue;
	return true;
}



////////////////////////////////////////////////////////////////////////////
// Serialization — the array packs its elements, and they pack themselves
////////////////////////////////////////////////////////////////////////////
//
// The header (the type) is written by the base; this fills in the contents. Each
// element becomes a CHILD NODE and is asked the same question — so the walk
// continues by itself, one class deep at a time, and this class never learns
// what its elements contain.
//
// No lengths, no separators: the tree carries the structure that a flat string
// would have had to encode by hand.

#include "backend/serialize/dataBuilder.h"


bool ibValueArray::DoSerialize(ibDataNode& node) const
{
	node.SetValue(wxT("n"), (s32)m_listValue.size());

	for (const ibValue& element : m_listValue) {
		ibDataNode& child = node.AddChild(element.GetClassType(), 0);
		// AN ELEMENT THAT CANNOT TRAVEL VOIDS THE ARRAY. Writing a hole instead
		// would restore into something that looks whole and quietly lost a row —
		// the failure nobody notices until a report comes up short.
		if (!element.Serialize(child))
			return false;
	}

	return true;
}

bool ibValueArray::DoDeserialize(const ibDataNode& node)
{
	m_listValue.clear();

	// The declared count is a CROSS-CHECK, not the loop bound: the children are
	// what actually exist, and trusting a number over the structure is how a
	// truncated blob turns into a silently shorter array.
	const s32 declared = node.GetValue<s32>(wxT("n"));

	for (const ibDataNode& child : node.Children())
		m_listValue.push_back(ibValue::FromNode(child));

	return (s32)m_listValue.size() == declared;
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_TYPE_REGISTER(ibValueArray, "Array", value_to_clsid("VL_ARR"));
