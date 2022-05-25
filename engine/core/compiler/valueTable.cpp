////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : value table and key pair 
////////////////////////////////////////////////////////////////////////////

#include "valueTable.h"
#include "methods.h"
#include "functions.h"
#include "frontend/mainFrame.h"

#include "utils/stringUtils.h"

//////////////////////////////////////////////////////////////////////

wxIMPLEMENT_DYNAMIC_CLASS(CValueTable, IValueTable);

//////////////////////////////////////////////////////////////////////

CMethods CValueTable::m_methods;

CValueTable::CValueTable() : IValueTable()
{
	m_dataColumnCollection = new CValueTableColumnCollection(this);
	m_dataColumnCollection->IncrRef();

	//delete only in CValue
	//wxRefCounter::IncrRef(); 
}

CValueTable::CValueTable(const CValueTable& valueTable) : IValueTable(), 
m_dataColumnCollection(valueTable.m_dataColumnCollection)
{
	m_dataColumnCollection->IncrRef();

	//delete only in CValue
	//wxRefCounter::IncrRef();
}

CValueTable::~CValueTable()
{
	if (m_dataColumnCollection)
		m_dataColumnCollection->DecrRef();
}

// methods:
enum
{
	enAddRow = 0,
	enClone,
	enCount,
	enClear,
};

//attributes:
enum
{
	enColumns = 0,
};

void CValueTable::PrepareNames() const
{
	SEng aMethods[] =
	{
		{"add","add()"},

		{"clone","clone()"},
		{"count","count()"},
		{"clear","clear()"},
	};

	int nCountM = sizeof(aMethods) / sizeof(aMethods[0]);
	m_methods.PrepareMethods(aMethods, nCountM);

	SEng aAttributes[] =
	{
		{"columns","columns"}
	};

	int nCountA = sizeof(aAttributes) / sizeof(aAttributes[0]);
	m_methods.PrepareAttributes(aAttributes, nCountA);
}

CValue CValueTable::GetAttribute(attributeArg_t& aParams)
{
	CValue ret;

	switch (aParams.GetIndex())
	{
	case enColumns: return m_dataColumnCollection;
	}

	return ret;
}

CValue CValueTable::Method(methodArg_t& aParams)
{
	CValue ret;

	switch (aParams.GetIndex())
	{
	case enAddRow: return AddRow();
	case enClone: return Clone();
	case enCount: return (int)Count();
	case enClear: Clear(); break;
	}

	return ret;
}

#include "appData.h"

CValue CValueTable::GetAt(const CValue& cKey)
{
	unsigned int index = cKey.ToUInt();

	if (index >= m_aObjectValues.size() && !appData->DesignerMode())
		CTranslateError::Error(_("Index outside array bounds"));

	return new CValueTableReturnLine(this, index);
}

//////////////////////////////////////////////////////////////////////
//               CValueTableColumnCollection                                  //
//////////////////////////////////////////////////////////////////////

wxIMPLEMENT_DYNAMIC_CLASS(CValueTable::CValueTableColumnCollection, IValueTable::IValueTableColumnCollection);

CValueTable::CValueTableColumnCollection::CValueTableColumnCollection() : IValueTableColumnCollection(), m_methods(NULL), m_ownerTable(NULL) {}
CValueTable::CValueTableColumnCollection::CValueTableColumnCollection(CValueTable* ownerTable) : IValueTableColumnCollection(), m_methods(new CMethods()), m_ownerTable(ownerTable) {}
CValueTable::CValueTableColumnCollection::~CValueTableColumnCollection()
{
	for (auto& colInfo : m_aColumnInfo) {
		wxASSERT(colInfo);
		colInfo->DecrRef();
	}

	wxDELETE(m_methods);
}

//работа с массивом как с агрегатным объектом
//перечисление строковых ключей
enum
{
	enAddColumn = 0,
	enRemoveColumn
};

void CValueTable::CValueTableColumnCollection::PrepareNames() const
{
	std::vector<SEng> aMethods =
	{
		{"addColumn","add(name, type, caption, width)"},
		{"removeColumn","removeColumn(name)"},
	};

	m_methods->PrepareMethods(aMethods.data(), aMethods.size());
}

#include "valueType.h"

CValue CValueTable::CValueTableColumnCollection::Method(methodArg_t& aParams)
{
	switch (aParams.GetIndex())
	{
	case enAddColumn:
	{
		CValueType* m_valueType = NULL;

		if (aParams.GetParamCount() > 1)
			aParams[1].ConvertToValue(m_valueType);

		if (aParams.GetParamCount() > 3)
			return AddColumn(aParams[0].ToString(), m_valueType ? new CValueTypeDescription(m_valueType) : aParams[1].ConvertToType<CValueTypeDescription>(), aParams[2].ToString(), aParams[3].ToInt());
		else if (aParams.GetParamCount() > 2)
			return AddColumn(aParams[0].ToString(), m_valueType ? new CValueTypeDescription(m_valueType) : aParams[1].ConvertToType<CValueTypeDescription>(), aParams[2].ToString(), wxDVC_DEFAULT_WIDTH);
		else if (aParams.GetParamCount() > 1)
			return AddColumn(aParams[0].ToString(), m_valueType ? new CValueTypeDescription(m_valueType) : aParams[1].ConvertToType<CValueTypeDescription>(), aParams[0].ToString(), wxDVC_DEFAULT_WIDTH);
		else
			return AddColumn(aParams[0].ToString(), NULL, aParams[0].ToString(), wxDVC_DEFAULT_WIDTH);
		break;
	}
	case enRemoveColumn:
	{
		wxString columnName = aParams[0].ToString();
		auto itFounded = std::find_if(m_aColumnInfo.begin(), m_aColumnInfo.end(),
			[columnName](CValueTableColumnInfo* colData)
			{
				return StringUtils::CompareString(columnName, colData->GetColumnName());
			});
		if (itFounded != m_aColumnInfo.end()) {
			RemoveColumn((*itFounded)->GetColumnID());
		}
		break;
	}
	}

	return CValue();
}

void CValueTable::CValueTableColumnCollection::SetAt(const CValue& cKey, CValue& cVal)//индекс массива должен начинаться с 0
{
}

CValue CValueTable::CValueTableColumnCollection::GetAt(const CValue& cKey) //индекс массива должен начинаться с 0
{
	unsigned int index = cKey.ToUInt();

	if ((index < 0 || index >= m_aColumnInfo.size() && !appData->DesignerMode()))
		CTranslateError::Error(_("Index goes beyond array"));

	auto itFounded = m_aColumnInfo.begin();
	std::advance(itFounded, index);
	return *itFounded;
}

//////////////////////////////////////////////////////////////////////
//               CValueTableColumnInfo                              //
//////////////////////////////////////////////////////////////////////

wxIMPLEMENT_DYNAMIC_CLASS(CValueTable::CValueTableColumnCollection::CValueTableColumnInfo, IValueTable::IValueTableColumnCollection::IValueTableColumnInfo);

CValueTable::CValueTableColumnCollection::CValueTableColumnInfo::CValueTableColumnInfo() : IValueTableColumnInfo() {}
CValueTable::CValueTableColumnCollection::CValueTableColumnInfo::CValueTableColumnInfo(unsigned int colID, const wxString& colName, CValueTypeDescription* types, const wxString& caption, int width) : IValueTableColumnInfo(), m_columnID(colID), m_columnName(colName), m_columnTypes(types), m_columnCaption(caption), m_columnWidth(width) { if (m_columnTypes) m_columnTypes->IncrRef(); }
CValueTable::CValueTableColumnCollection::CValueTableColumnInfo::~CValueTableColumnInfo() { if (m_columnTypes) m_columnTypes->DecrRef(); }

//////////////////////////////////////////////////////////////////////
//               CValueTableReturnLine                              //
//////////////////////////////////////////////////////////////////////

wxIMPLEMENT_DYNAMIC_CLASS(CValueTable::CValueTableReturnLine, IValueTable::IValueTableReturnLine);

CValueTable::CValueTableReturnLine::CValueTableReturnLine() : IValueTableReturnLine(), m_methods(NULL), m_ownerTable(NULL), m_lineTable(wxNOT_FOUND) {}
CValueTable::CValueTableReturnLine::CValueTableReturnLine(CValueTable* ownerTable, int line) : IValueTableReturnLine(), m_methods(new CMethods()), m_ownerTable(ownerTable), m_lineTable(line) {}
CValueTable::CValueTableReturnLine::~CValueTableReturnLine() { if (m_methods) delete m_methods; }

void CValueTable::CValueTableReturnLine::SetValueByMetaID(const meta_identifier_t &id, const CValue& cVal)
{
	auto itFoundedByLine = m_ownerTable->m_aObjectValues.begin();
	std::advance(itFoundedByLine, m_lineTable);

	CValueTypeDescription* typeDescription = 
		m_ownerTable->m_dataColumnCollection->GetColumnType(id);

	itFoundedByLine->insert_or_assign(id, typeDescription ? typeDescription->AdjustValue(cVal) : cVal);
}

CValue CValueTable::CValueTableReturnLine::GetValueByMetaID(const meta_identifier_t &id) const
{
	auto itFoundedByLine = m_ownerTable->m_aObjectValues.begin();
	std::advance(itFoundedByLine, m_lineTable);

	auto itFoundedByIndex = itFoundedByLine->find(id);

	if (itFoundedByIndex != itFoundedByLine->end())
		return itFoundedByIndex->second;

	return CValue();
}

void CValueTable::CValueTableReturnLine::PrepareNames() const
{
	std::vector<SEng> aAttributes;

	for (auto& colInfo : m_ownerTable->m_dataColumnCollection->m_aColumnInfo)
	{
		wxASSERT(colInfo);

		SEng aAttribute;

		aAttribute.sName = colInfo->GetColumnName();
		aAttribute.sSynonym = wxT("default");
		aAttribute.iName = colInfo->GetColumnID();

		aAttributes.push_back(aAttribute);
	}

	m_methods->PrepareAttributes(aAttributes.data(), aAttributes.size());
}

void CValueTable::CValueTableReturnLine::SetAttribute(attributeArg_t& aParams, CValue& cVal)
{
	if (appData->DesignerMode())
		return;

	SetValueByMetaID(
		m_methods->GetAttributePosition(aParams.GetIndex()),
		cVal
	);
}

CValue CValueTable::CValueTableReturnLine::GetAttribute(attributeArg_t& aParams)
{
	if (appData->DesignerMode())
		return CValue();

	return GetValueByMetaID(
		m_methods->GetAttributePosition(aParams.GetIndex())
	);
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_REGISTER(CValueTable, "table", g_valueTableCLSID);

SO_VALUE_REGISTER(CValueTable::CValueTableColumnCollection, "tableValueColumn", CValueTableColumnCollection, TEXT2CLSID("VL_TAVC"));
SO_VALUE_REGISTER(CValueTable::CValueTableColumnCollection::CValueTableColumnInfo, "tableValueColumnInfo", CValueTableColumnInfo, TEXT2CLSID("VL_TVCI"));
SO_VALUE_REGISTER(CValueTable::CValueTableReturnLine, "tableValueRow", CValueTableReturnLine, TEXT2CLSID("VL_TVCR"));