////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : value structure and containers
////////////////////////////////////////////////////////////////////////////

#include "valueMap.h"
#include "backend/backend_exception.h"


bool ibValueContainer::ContainerComparator::operator() (const ibValue& lhs, const ibValue& rhs) const {
	if (lhs.GetType() == ibValueTypes::TYPE_STRING
		&& rhs.GetType() == ibValueTypes::TYPE_STRING) {
		return stringUtils::MakeUpper(lhs.GetString()) < stringUtils::MakeUpper(rhs.GetString());
	}
	else {
		return lhs < rhs;
	}
}

//**********************************************************************
//*                          ibValueReturnMap                           *
//**********************************************************************

void ibValueContainer::ibValueReturnContainer::FillMembers(ibMemberTable& helper) const
{
	helper.AppendProp(wxT("Key"));
	helper.AppendProp(wxT("Value"));
}

bool ibValueContainer::ibValueReturnContainer::SetPropVal(const long lPropNum, ibValue& cValue)
{
	return false;
}

bool ibValueContainer::ibValueReturnContainer::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	switch (lPropNum)
	{
	case enKey:
		pvarPropVal = m_key;
		return true;
	case enValue:
		pvarPropVal = m_value;
		return true;
	}

	return false;
}

//**********************************************************************
//*                            ibValueContainer                         *
//**********************************************************************

ibValueContainer::ibValueContainer() : ibValueDynamicMembers(ibValueTypes::TYPE_VALUE) {
	m_members.Bind(&BindContainerNames, this);
}

ibValueContainer::ibValueContainer(const std::map<ibValue, ibValue>& containerValues) : ibValueDynamicMembers(ibValueTypes::TYPE_VALUE, true) {
	m_members.Bind(&BindContainerNames, this);
	for (auto& cntVal : containerValues) {
		m_containerValues.insert_or_assign(cntVal.first, cntVal.second);
	}
}

ibValueContainer::ibValueContainer(bool readOnly) : ibValueDynamicMembers(ibValueTypes::TYPE_VALUE, readOnly) {
	m_members.Bind(&BindContainerNames, this);
}

ibValueContainer::~ibValueContainer() {
	m_containerValues.clear();
}

//������ � �������� ��� � ���������� ��������
//������������ ��������� ������
void ibValueContainer::BindContainerNames(ibMemberTable& helper, const ibValue* ctx)
{
	const ibValueContainer* self = static_cast<const ibValueContainer*>(ctx);

	helper.AppendFunc(wxT("Count"), wxT("Count()"));
	helper.AppendFunc(wxT("Property"), 2, wxT("Property(key : any, valueFound : any)"));

	if (!self->m_bReadOnly) {
		helper.AppendFunc(wxT("Clear"), wxT("Clear()"));
		helper.AppendFunc(wxT("Delete"), 1, wxT("Delete(key : any)"));
		helper.AppendFunc(wxT("Insert"), 2, wxT("Insert(key : any, value : any)"));
	}

	for (auto keyValue : self->m_containerValues) {
		const ibValue& cValKey = keyValue.first;
		if (!cValKey.IsEmpty()) {
			helper.AppendProp(cValKey.GetString());
		}
	}
}

bool ibValueContainer::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	return SetAt(
		GetPropName(lPropNum), varPropVal
	);
}

bool ibValueContainer::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	return GetAt(
		GetPropName(lPropNum), pvarPropVal
	);
}

bool ibValueContainer::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case enClear:
		Clear();
		return true;
	case enCount:
		pvarRetValue = Count();
		return true;
	case enDelete:
		Delete(*paParams[0]);
		return true;
	case enInsert:
		Insert(*paParams[0], *paParams[1]);
		return true;
	case enProperty:
	{
		ibValue defaultVal;
		pvarRetValue = Property(*paParams[0], lSizeArray > 1 ? *paParams[1] : defaultVal);
	}
		return true;
	}

	return false;
}

void ibValueContainer::Delete(const ibValue& varKeyValue)
{
	m_members.Invalidate();
	m_containerValues.erase(varKeyValue);
}

#include "backend/appData.h"

void ibValueContainer::Insert(const ibValue& varKeyValue, const ibValue& cValue)
{
	std::map<const ibValue, ibValue>::iterator it = m_containerValues.find(varKeyValue);
	if (it != m_containerValues.end()) {
		if (!appData->DesignerMode())
			ibBackendCoreException::Error(_("Key '%s' is already using!"), varKeyValue.GetString());
		return;
	}
	m_members.Invalidate();
	m_containerValues.insert_or_assign(varKeyValue, cValue);
}

bool ibValueContainer::Property(const ibValue& varKeyValue, ibValue& cValueFound)
{
	std::map<const ibValue, ibValue>::iterator itFound = m_containerValues.find(varKeyValue);
	if (itFound != m_containerValues.end()) {
		cValueFound = itFound->second;
		return true;
	}
	return false;
}

std::shared_ptr<ibValueIteratorState> ibValueContainer::CreateIterator()
{
	using MapT = std::decay_t<decltype(m_containerValues)>;
	class State : public ibValueIteratorState {
	public:
		explicit State(const MapT& m) : m_map(m), m_it(m.begin()) {}
		bool MoveNext(ibValue& current) override {
			if (m_started) ++m_it; else m_started = true;
			if (m_it == m_map.end()) return false;
			ibValue valueCopy = m_it->second;
			current = ibValue(static_cast<ibValue*>(
				new ibValueReturnContainer(
					m_it->first, valueCopy)));
			return true;
		}
		void Reset() override { m_it = m_map.begin(); m_started = false; }
		bool PeekSample(ibValue& current) const override {
			current = ibValue(static_cast<ibValue*>(
				new ibValueReturnContainer()));
			return true;
		}
	private:
		const MapT& m_map;
		MapT::const_iterator m_it;
		bool m_started = false;
	};
	return std::make_shared<State>(m_containerValues);
}

bool ibValueContainer::SetAt(const ibValue& varKeyValue, const ibValue& varValue)
{
	ibValueContainer::Insert(varKeyValue, varValue);
	return true;
}

bool ibValueContainer::GetAt(const ibValue& varKeyValue, ibValue& pvarValue)
{
	std::map<const ibValue, ibValue>::const_iterator itFound = m_containerValues.find(varKeyValue);
	if (itFound != m_containerValues.end()) {
		pvarValue = itFound->second; return true;
	}
	if (!appData->DesignerMode())
		ibBackendCoreException::Error(_("Key '%s' not found!"), varKeyValue.GetString());
	return false;
}

//**********************************************************************
//*                            ibValueStructure                         *
//**********************************************************************

#define st_error_conversion _("Error conversion value. Must be string!")

bool ibValueStructure::Init(ibValue** paParams, const long lSizeArray)
{
	// No args → empty Structure ready for Insert later.
	if (lSizeArray == 0 || paParams == nullptr)
		return true;

	// First arg must be a string with comma-separated field names.
	const ibValue* fieldsArg = paParams[0];
	if (fieldsArg == nullptr || fieldsArg->GetType() != ibValueTypes::TYPE_STRING) {
		ibBackendCoreException::Error(
			_("Structure ctor: first argument must be a comma-separated field name string"));
		return false;
	}

	const wxString fieldsStr = fieldsArg->GetString();

	// Single-pass scan: split on ',' and trim whitespace. wxStringTokenizer
	// would also work but the manual form keeps trimming inline + avoids
	// the include. Empty tokens (`,,`) are skipped.
	size_t cursor = 0;
	long valueIdx = 1;   // index into paParams for the value of the next field
	while (cursor <= fieldsStr.size()) {
		size_t comma = fieldsStr.find(wxT(','), cursor);
		if (comma == wxString::npos) comma = fieldsStr.size();

		// Trim leading whitespace.
		size_t start = cursor;
		while (start < comma
			&& (fieldsStr[start] == wxT(' ') || fieldsStr[start] == wxT('\t')))
			++start;

		// Trim trailing whitespace.
		size_t end = comma;
		while (end > start
			&& (fieldsStr[end - 1] == wxT(' ') || fieldsStr[end - 1] == wxT('\t')))
			--end;

		if (end > start) {
			const wxString fieldName = fieldsStr.Mid(start, end - start);
			ibValue value;
			if (valueIdx < lSizeArray && paParams[valueIdx] != nullptr)
				value = *paParams[valueIdx];
			ibValueStructure::Insert(fieldName, value);
			++valueIdx;
		}

		if (comma >= fieldsStr.size()) break;
		cursor = comma + 1;
	}

	return true;
}

bool ibValueStructure::GetAt(const ibValue& varKeyValue, ibValue& pvarValue)
{
	if (varKeyValue.GetType() != ibValueTypes::TYPE_STRING) {
		if (!appData->DesignerMode())
			ibBackendCoreException::Error(st_error_conversion);
		return false;
	}
	return ibValueContainer::GetAt(varKeyValue, pvarValue);
}

bool ibValueStructure::SetAt(const ibValue& varKeyValue, const ibValue& cValue)
{
	if (varKeyValue.GetType() != ibValueTypes::TYPE_STRING) {
		if (!appData->DesignerMode()) {
			ibBackendCoreException::Error(st_error_conversion);
		} return false;
	}

	return ibValueContainer::SetAt(varKeyValue, cValue);
}

void ibValueStructure::Delete(const ibValue& varKeyValue)
{
	if (varKeyValue.GetType() != ibValueTypes::TYPE_STRING) {
		if (!appData->DesignerMode()) {
			ibBackendCoreException::Error(st_error_conversion);
		} return;
	}

	ibValueContainer::Delete(varKeyValue);
}

void ibValueStructure::Insert(const ibValue& varKeyValue, const ibValue& cValue)
{
	if (varKeyValue.GetType() != ibValueTypes::TYPE_STRING) {
		if (!appData->DesignerMode()) {
			ibBackendCoreException::Error(st_error_conversion);
		} return;
	}

	ibValueContainer::Insert(varKeyValue, cValue);
}

bool ibValueStructure::Property(const ibValue& varKeyValue, ibValue& cValueFound)
{
	if (varKeyValue.GetType() != ibValueTypes::TYPE_STRING) {
		if (!appData->DesignerMode()) {
			ibBackendCoreException::Error(st_error_conversion);
		}
		return false;
	}

	return ibValueContainer::Property(varKeyValue, cValueFound);
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_TYPE_REGISTER(ibValueContainer, "Container", value_to_clsid("VL_CONTR"));
VALUE_TYPE_REGISTER(ibValueStructure, "Structure", value_to_clsid("VL_STRUT"));

SYSTEM_TYPE_REGISTER(ibValueContainer::ibValueReturnContainer, "KeyValue", system_to_clsid("VL_KEVAL"));
