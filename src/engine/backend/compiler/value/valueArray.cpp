////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : array value   
////////////////////////////////////////////////////////////////////////////

#include "valueArray.h"
#include "backend/backend_exception.h"


wxIMPLEMENT_DYNAMIC_CLASS(CValueArray, CValue);

//////////////////////////////////////////////////////////////////////

CValue::CMethodHelper CValueArray::m_methodHelper;

bool CValueArray::Init()
{
	return true;
}

bool CValueArray::Init(CValue** paParams, const long lSizeArray)
{
	if (paParams[0]->GetType() == eValueTypes::TYPE_NUMBER) {
		const number_t& number = paParams[0]->GetNumber();
		if (number > 0) {
			m_arrValues.resize(number.GetUInteger());
			return true;
		}
	}
	return false;
}

#include "appdata.h"

void CValueArray::CheckIndex(unsigned int index) const //индекс массива должен начинаться с 1
{
	if ((index < 0 || index >= m_arrValues.size() && !appData->DesignerMode()))
		CBackendException::Error("Index goes beyond array");
}

//работа с массивом как с агрегатным объектом
//перечисление строковых ключей
void CValueArray::PrepareNames() const
{
	m_methodHelper.ClearHelper();
	m_methodHelper.AppendConstructor(1, "array(number)");

	m_methodHelper.AppendFunc("add", 1, "add(value)");
	m_methodHelper.AppendFunc("insert", 2, "insert(index, value)");

	m_methodHelper.AppendFunc("count", "count()");
	m_methodHelper.AppendFunc("find", 1, "find(value)");
	m_methodHelper.AppendFunc("clear", "clear()");
	m_methodHelper.AppendFunc("get", 1, "get(index)");
	m_methodHelper.AppendFunc("set", 1, "get(index)");
	m_methodHelper.AppendFunc("remove", 1, "remove(index)");
}

bool CValueArray::CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray)
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
		return GetAt(*paParams[0], pvarRetValue);;
	case enSet:  
		return SetAt(*paParams[0], *paParams[1]);;
	case enRemove: 
		Remove(paParams[0]->GetUInteger());
		return true;
	}

	return false;
}

bool CValueArray::GetAt(const CValue& varKeyValue, CValue& pvarValue) //индекс массива должен начинаться с 0
{
	CheckIndex(varKeyValue.GetUInteger());
	pvarValue = m_arrValues[varKeyValue.GetUInteger()];
	return true; 
}

bool CValueArray::SetAt(const CValue& varKeyValue, const CValue& varValue)//индекс массива должен начинаться с 0
{
	CheckIndex(varKeyValue.GetUInteger());
	m_arrValues[varKeyValue.GetUInteger()] = varValue;
	return true;
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_TYPE_REGISTER(CValueArray, "array", string_to_clsid("VL_ARR"));