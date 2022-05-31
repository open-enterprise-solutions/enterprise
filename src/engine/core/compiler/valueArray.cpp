////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : array value   
////////////////////////////////////////////////////////////////////////////

#include "valueArray.h"
#include "methods.h"
#include "functions.h"
#include "utils/stringUtils.h"

wxIMPLEMENT_DYNAMIC_CLASS(CValueArray, CValue);

//////////////////////////////////////////////////////////////////////

CMethods CValueArray::m_methods;

CValueArray::CValueArray() : CValue(eValueTypes::TYPE_VALUE) {}

CValueArray::CValueArray(const std::vector <CValue> &arr) : CValue(eValueTypes::TYPE_VALUE, true), m_aValuesArray(arr) {}

CValueArray::~CValueArray() { Clear(); }

bool CValueArray::Init(CValue **aParams)
{
	if (aParams[0]->GetType() == eValueTypes::TYPE_NUMBER)
	{
		number_t number = aParams[0]->GetNumber();
		if (number > 0) { m_aValuesArray.resize(number.ToUInt()); return true; }
	}

	return false;
}

#include "appdata.h"

void CValueArray::CheckIndex(unsigned int index) //индекс массива должен начинаться с 1
{
	if ((index < 0 || index >= m_aValuesArray.size() && !appData->DesignerMode()))
		CTranslateError::Error(_("Index goes beyond array"));
}

//работа с массивом как с агрегатным объектом
//перечисление строковых ключей
enum
{
	enAdd = 0,
	enInsert,
	enCount,
	enFind,
	enClear,
	enGet,
	enSet,
	enRemove
};

void CValueArray::PrepareNames() const
{
	m_methods.AppendConstructor("array", "array(number)"); 

	SEng aMethods[] =
	{
		{"add","add(value)"},
		{"insert","insert(index, value)"},
		{"count","count()"},
		{"find","find(value)"},
		{"clear","clear()"},
		{"get","get(index)"},
		{"set","get(index)"},
		{"remove","remove(index)"},
	};

	int nCountM = sizeof(aMethods) / sizeof(aMethods[0]);
	m_methods.PrepareMethods(aMethods, nCountM);
}

CValue CValueArray::Method(methodArg_t &aParams)
{
	switch (aParams.GetIndex())
	{
	case enAdd: Add(aParams[0]); break;
	case enInsert: Insert(aParams[0].ToUInt(), aParams[1]); break;
	case enCount: return Count();
	case enFind: return Find(aParams[0]);
	case enClear: Clear(); break;
	case enGet: return GetAt(aParams[0]);
	case enSet: SetAt(aParams[0], aParams[1]); break;
	case enRemove: Remove(aParams[0].ToUInt()); break;
	}

	return CValue();
}

void CValueArray::Add(CValue &cVal)
{
	m_aValuesArray.push_back(cVal);
}

void CValueArray::Insert(unsigned int index, CValue &cVal)
{
	CheckIndex(index);
	m_aValuesArray.insert(m_aValuesArray.begin() + index, cVal);
}

void CValueArray::Remove(unsigned int index)
{
	CheckIndex(index);
	m_aValuesArray.erase(m_aValuesArray.begin() + index);
}

CValue CValueArray::Find(CValue &cVal)
{
	auto foundedIt = std::find(m_aValuesArray.begin(), m_aValuesArray.end(), cVal);
	if (foundedIt != m_aValuesArray.end()) return std::distance(m_aValuesArray.begin(), foundedIt);
	return CValue();
}

void CValueArray::SetAt(const CValue &cKey, CValue &cVal)//индекс массива должен начинаться с 0
{
	CheckIndex(cKey.ToUInt());
	m_aValuesArray[cKey.ToUInt()] = cVal;
}

CValue CValueArray::GetAt(const CValue &cKey) //индекс массива должен начинаться с 0
{
	CheckIndex(cKey.ToUInt());
	return m_aValuesArray[cKey.ToUInt()];
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_REGISTER(CValueArray, "array", TEXT2CLSID("VL_ARR")); 