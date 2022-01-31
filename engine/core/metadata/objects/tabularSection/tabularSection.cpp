////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : tabular sections
////////////////////////////////////////////////////////////////////////////

#include "tabularSection.h"
#include "appData.h"
#include "databaseLayer/databaseLayer.h"
#include "databaseLayer/databaseErrorCodes.h"
#include "metadata/objects/baseObject.h"
#include "utils/stringUtils.h"

wxIMPLEMENT_ABSTRACT_CLASS(IValueTabularSection, IValueTable);
wxIMPLEMENT_DYNAMIC_CLASS(IValueTabularSection::CValueTabularSectionReturnLine, IValueTable::IValueTableReturnLine);
wxIMPLEMENT_DYNAMIC_CLASS(CValueTabularSection, IValueTabularSection);
wxIMPLEMENT_DYNAMIC_CLASS(CValueTabularRefSection, IValueTabularSection);

//////////////////////////////////////////////////////////////////////
//               IValueTabularSection                               //
//////////////////////////////////////////////////////////////////////

#include "metadata/singleMetaTypes.h"

CValue IValueTabularSection::GetAt(const CValue & cKey)
{
	unsigned int index = cKey.ToUInt();

	if (index >= m_aObjectValues.size() && !appData->DesignerMode())
		CTranslateError::Error(_("Index outside array bounds"));

	return new CValueTabularSectionReturnLine(this, index);
}

CLASS_ID IValueTabularSection::GetTypeID() const
{
	IMetaObjectValue *metaObject = m_dataObject->GetMetaObject();
	wxASSERT(metaObject);
	IMetaTypeObjectValueSingle *clsFactory =
		metaObject->GetTypeObject(eMetaObjectType::enTabularSection);
	wxASSERT(clsFactory);
	return clsFactory->GetTypeID();
}

wxString IValueTabularSection::GetTypeString() const
{
	IMetaObjectValue *metaObject = m_dataObject->GetMetaObject();
	wxASSERT(metaObject);
	IMetaTypeObjectValueSingle *clsFactory =
		metaObject->GetTypeObject(eMetaObjectType::enTabularSection);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

wxString IValueTabularSection::GetString() const
{
	IMetaObjectValue *metaObject = m_dataObject->GetMetaObject();
	wxASSERT(metaObject);
	IMetaTypeObjectValueSingle *clsFactory =
		metaObject->GetTypeObject(eMetaObjectType::enTabularSection);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

#include "frontend/visualView/controls/form.h"

void IValueTabularSection::SetValueByMetaID(long line, meta_identifier_t id, const CValue &cVal)
{
	if (m_metaTable->IsNumberLine(id)) {
		return;
	}

	IMetaAttributeObject *metaAttribute = m_metaTable->FindAttribute(id);
	wxASSERT(metaAttribute);
	CValueTypeDescription *td = new CValueTypeDescription(metaAttribute->GetClassTypeObject(),
		metaAttribute->GetNumberQualifier(), metaAttribute->GetDateQualifier(), metaAttribute->GetStringQualifier());

	m_aObjectValues[line][id] = td->AdjustValue(cVal);
	delete td;

	IValueTabularSection::Cleared();
}

CValue IValueTabularSection::GetValueByMetaID(long line, meta_identifier_t id) const
{
	if (m_metaTable->IsNumberLine(id)) {
		return line + 1;
	}

	return m_aObjectValues[line].at(id);
}

//////////////////////////////////////////////////////////////////////
//               CValueTabularSection                               //
//////////////////////////////////////////////////////////////////////

CValueTabularSection::CValueTabularSection() {}

CValueTabularSection::CValueTabularSection(IObjectValueInfo *dataObject, CMetaTableObject *tableObject) :
	IValueTabularSection(dataObject, tableObject)
{
}

//////////////////////////////////////////////////////////////////////
//               CValueTabularRefSection                            //
//////////////////////////////////////////////////////////////////////

CValueTabularRefSection::CValueTabularRefSection() {}

CValueTabularRefSection::CValueTabularRefSection(IObjectValueInfo *dataObject, CMetaTableObject *tableObject) :
	IValueTabularSection(dataObject, tableObject)
{
}

bool CValueTabularRefSection::LoadDataFromDB()
{
	if (m_dataObject->IsNewObject())
		return false;

	m_aObjectValues.clear();
	IMetaObjectValue *metaObject = m_dataObject->GetMetaObject();
	wxASSERT(metaObject);

	wxString tableName = m_metaTable->GetTableNameDB();
	wxString sql = "SELECT * FROM " + tableName + " WHERE UUID = '" + m_dataObject->GetGuid().str() + "'";

	DatabaseResultSet *resultSet = databaseLayer->RunQueryWithResults(sql);
	if (!resultSet) {
		return false;
	}
	while (resultSet->Next()) {
		std::map<meta_identifier_t, CValue> aRowTable;
		for (auto attribute : m_metaTable->GetObjectAttributes()) {
			wxString nameAttribute = attribute->GetFieldNameDB();
			if (m_metaTable->IsNumberLine(attribute->GetMetaID())) {
				aRowTable[attribute->GetMetaID()] = CValue(); //numberline is special field
				continue;
			}
			switch (attribute->GetTypeObject())
			{
			case eValueTypes::TYPE_BOOLEAN: aRowTable[attribute->GetMetaID()] = resultSet->GetResultBool(nameAttribute); break;
			case eValueTypes::TYPE_NUMBER:aRowTable[attribute->GetMetaID()] = resultSet->GetResultDouble(nameAttribute); break;
			case eValueTypes::TYPE_DATE:aRowTable[attribute->GetMetaID()] = resultSet->GetResultDate(nameAttribute); break;
			case eValueTypes::TYPE_STRING:aRowTable[attribute->GetMetaID()] = resultSet->GetResultString(nameAttribute); break;
			default:
			{
				wxMemoryBuffer bufferData;
				resultSet->GetResultBlob(nameAttribute, bufferData);
				if (!bufferData.IsEmpty()) {
					aRowTable[attribute->GetMetaID()] = CValueReference::CreateFromPtr(
						m_metaTable->GetMetadata(), bufferData.GetData()
					);
				}
				else {
					aRowTable[attribute->GetMetaID()] = new CValueReference(
						m_metaTable->GetMetadata(), attribute->GetTypeObject()
					);
				}
				break;
			}
			}
		}
		m_aObjectValues.push_back(aRowTable);
	}
	resultSet->Close();
	if (!CTranslateError::IsSimpleMode()) {
		IValueTable::Reset(m_aObjectValues.size());
	}
	return true;
}

#include "compiler/systemObjects.h"

bool CValueTabularRefSection::SaveDataInDB()
{
	bool hasError = false;

	//check fill attributes 
	bool fillCheck = true; int nLine = 1;
	for (auto objectValue : m_aObjectValues) {
		for (auto attribute : m_metaTable->GetObjectAttributes()) {
			if (attribute->FillCheck()) {
				if (objectValue[attribute->GetMetaID()].IsEmpty()) {
					wxString fillError =
						wxString::Format(_("The %s is required on line %i of the %s list"), attribute->GetSynonym(), nLine, m_metaTable->GetSynonym());
					CSystemObjects::Message(fillError, eStatusMessage::eStatusMessage_Information);
					fillCheck = false;
				}
			}
		}
		nLine++;
	}

	if (!fillCheck) {
		return false;
	}

	if (!CValueTabularRefSection::DeleteDataInDB()) {
		return false;
	}

	CMetaDefaultAttributeObject *numLine = m_metaTable->GetNumberLine();
	wxASSERT(numLine);
	IMetaObjectValue *metaObject = m_dataObject->GetMetaObject();
	wxASSERT(metaObject);
	reference_t *m_reference_impl = new reference_t(metaObject->GetMetaID(), m_dataObject->GetGuid());
	wxString tableName = m_metaTable->GetTableNameDB();
	wxString queryText = "UPDATE OR INSERT INTO " + tableName + " (";
	queryText += "UUID, UUIDREF";
	for (auto attribute : m_metaTable->GetObjectAttributes()) {
		queryText = queryText + ", " + attribute->GetFieldNameDB();
	}
	queryText += ") VALUES (?, ?";
	for (auto attribute : m_metaTable->GetObjectAttributes()) {
		queryText += ", ?";
	}
	queryText += ") MATCHING (UUID, " + numLine->GetFieldNameDB() + ");";
	PreparedStatement *statement = databaseLayer->PrepareStatement(queryText);
	wxASSERT(statement);
	number_t numberLine = 1;
	for (auto objectValue : m_aObjectValues) {
		if (hasError) {
			break;
		}
		int position = 3;
		statement->SetParamString(1, m_dataObject->GetGuid().str());
		statement->SetParamBlob(2, m_reference_impl, sizeof(reference_t));
		for (auto attribute : m_metaTable->GetObjectAttributes()) {
			if (!m_metaTable->IsNumberLine(attribute->GetMetaID())) {
				objectValue[attribute->GetMetaID()].SetBinaryData(position++, statement);
			}
			else {
				statement->SetParamNumber(position++, numberLine);
			}
		}
		numberLine = numberLine + 1;
		hasError = statement->RunQuery() == DATABASE_LAYER_QUERY_RESULT_ERROR;
	}
	databaseLayer->CloseStatement(statement);
	delete m_reference_impl;

	return !hasError;
}

bool CValueTabularRefSection::DeleteDataInDB()
{
	if (m_dataObject->IsNewObject())
		return true;

	IMetaObjectValue *metaObject = m_dataObject->GetMetaObject();
	wxASSERT(metaObject);
	wxString tableName = m_metaTable->GetTableNameDB();
	databaseLayer->RunQuery("DELETE FROM " + tableName + " WHERE UUID = '" + m_dataObject->GetGuid().str() + "';");
	return true;
}

#include "compiler/valueTable.h"

bool IValueTabularSection::LoadDataFromTable(IValueTable *srcTable)
{
	IValueTableColumns *colData = srcTable->GetColumns();

	if (colData == NULL)
		return false;

	wxArrayString m_aColumnsName; 
	for (unsigned int idx = 0; idx < colData->GetColumnCount() - 1; idx++) {
		IValueTableColumns::IValueTableColumnsInfo *colInfo = colData->GetColumnInfo(idx);
		wxASSERT(colInfo);
		if (m_aDataColumns->GetColumnByName(colInfo->GetColumnName()) != NULL) {
			m_aColumnsName.push_back(colInfo->GetColumnName());
		}
	}

	unsigned int rowsCount = srcTable->GetCount();
	for (unsigned int row = 0; row < rowsCount; row++) {
		IValueTableReturnLine *retLine = srcTable->GetRowAt(row); 
		IValueTableReturnLine *newRetLine = new CValueTabularSectionReturnLine(this, AppenRow());
		newRetLine->PrepareNames();
		for (auto colName : m_aColumnsName) {
			newRetLine->SetAttribute(colName, retLine->GetAttribute(colName));
		}
		wxDELETE(newRetLine);
	}

	return true;
}

IValueTable *IValueTabularSection::SaveDataToTable() const
{
	CValueTable *valueTable = new CValueTable;
	IValueTableColumns *colData = valueTable->GetColumns();

	for (unsigned int idx = 0; idx < m_aDataColumns->GetColumnCount() - 1; idx++) {
		IValueTableColumns::IValueTableColumnsInfo *colInfo = m_aDataColumns->GetColumnInfo(idx);
		wxASSERT(colInfo);
		IValueTableColumns::IValueTableColumnsInfo *newColInfo = colData->AddColumn(
			colInfo->GetColumnName(), colInfo->GetColumnTypes(), colInfo->GetColumnCaption(), colInfo->GetColumnWidth()
		);
		newColInfo->SetColumnID(colInfo->GetColumnID());
	}

	valueTable->PrepareNames();

	for (auto row : m_aObjectValues) {
		CValueTable::CValueTableReturnLine *retLine = valueTable->AddRow();
		wxASSERT(retLine);
		for (auto cols : row) {
			if (m_metaTable->IsNumberLine(cols.first))
				continue;
			retLine->SetValueByMetaID(cols.first, cols.second);
		}
	}

	return valueTable;
}

void CValueTabularRefSection::SetValueByMetaID(long line, meta_identifier_t id, const CValue &cVal)
{
	CValueForm *foundedForm = CValueForm::FindFormByGuid(
		m_dataObject->GetGuid()
	);

	IValueTabularSection::SetValueByMetaID(line, id, cVal);

	if (foundedForm) {
		foundedForm->Modify(true);
	}
}

CValue CValueTabularRefSection::GetValueByMetaID(long line, meta_identifier_t id) const
{
	return IValueTabularSection::GetValueByMetaID(line, id);
}

//////////////////////////////////////////////////////////////////////
//               CValueTabularSectionColumns                        //
//////////////////////////////////////////////////////////////////////

#include "compiler/methods.h"

wxIMPLEMENT_DYNAMIC_CLASS(IValueTabularSection::CValueTabularSectionColumns, IValueTable::IValueTableColumns);

IValueTabularSection::CValueTabularSectionColumns::CValueTabularSectionColumns() : IValueTableColumns(), m_methods(NULL), m_ownerTable(NULL)
{
}

IValueTabularSection::CValueTabularSectionColumns::CValueTabularSectionColumns(IValueTabularSection *ownerTable) : IValueTableColumns(), m_methods(new CMethods()), m_ownerTable(ownerTable)
{
	CMetaTableObject *metaTable = m_ownerTable->GetMetaObject();
	wxASSERT(metaTable);

	for (auto attributes : metaTable->GetObjectAttributes()) {
		if (metaTable->IsNumberLine(attributes->GetMetaID()))
			continue;
		CValueTabularSectionColumnInfo *columnInfo = new CValueTabularSectionColumnInfo(attributes);
		m_aColumnInfo.insert_or_assign(attributes->GetMetaID(), columnInfo);
		columnInfo->IncrRef();
	}
}

IValueTabularSection::CValueTabularSectionColumns::~CValueTabularSectionColumns()
{
	for (auto& colInfo : m_aColumnInfo) {
		CValueTabularSectionColumnInfo *columnInfo = colInfo.second;
		wxASSERT(columnInfo);
		columnInfo->DecrRef();
	}

	wxDELETE(m_methods);
}

void IValueTabularSection::CValueTabularSectionColumns::SetAt(const CValue &cKey, CValue &cVal)//индекс массива должен начинаться с 0
{
}

CValue IValueTabularSection::CValueTabularSectionColumns::GetAt(const CValue &cKey) //индекс массива должен начинаться с 0
{
	unsigned int index = cKey.ToUInt();

	if ((index < 0 || index >= m_aColumnInfo.size() && !appData->DesignerMode()))
		CTranslateError::Error(_("Index goes beyond array"));

	auto itFounded = m_aColumnInfo.begin();
	std::advance(itFounded, index);
	return itFounded->second;
}

//////////////////////////////////////////////////////////////////////
//               CValueTabularSectionColumnInfo                     //
//////////////////////////////////////////////////////////////////////

wxIMPLEMENT_DYNAMIC_CLASS(IValueTabularSection::CValueTabularSectionColumns::CValueTabularSectionColumnInfo, IValueTable::IValueTableColumns::IValueTableColumnsInfo);

IValueTabularSection::CValueTabularSectionColumns::CValueTabularSectionColumnInfo::CValueTabularSectionColumnInfo() : IValueTableColumnsInfo(),
m_methods(NULL), m_metaAttribute(NULL)
{
}

IValueTabularSection::CValueTabularSectionColumns::CValueTabularSectionColumnInfo::CValueTabularSectionColumnInfo(IMetaAttributeObject *metaAttribute) : IValueTableColumnsInfo(),
m_methods(new CMethods()), m_metaAttribute(metaAttribute)
{
}

IValueTabularSection::CValueTabularSectionColumns::CValueTabularSectionColumnInfo::~CValueTabularSectionColumnInfo()
{
	wxDELETE(m_methods);
}

enum
{
	enColumnName,
	enColumnTypes,
	enColumnCaption,
	enColumnWidth
};

void IValueTabularSection::CValueTabularSectionColumns::CValueTabularSectionColumnInfo::PrepareNames() const
{
	std::vector<SEng> aAttributes;

	{
		SEng aAttribute;
		aAttribute.sName = wxT("name");
		aAttribute.sSynonym = wxT("default");
		aAttributes.push_back(aAttribute);
	}
	{
		SEng aAttribute;
		aAttribute.sName = wxT("types");
		aAttribute.sSynonym = wxT("default");
		aAttributes.push_back(aAttribute);
	}
	{
		SEng aAttribute;
		aAttribute.sName = wxT("caption");
		aAttribute.sSynonym = wxT("default");
		aAttributes.push_back(aAttribute);
	}
	{
		SEng aAttribute;
		aAttribute.sName = wxT("width");
		aAttribute.sSynonym = wxT("default");
		aAttributes.push_back(aAttribute);
	}

	m_methods->PrepareAttributes(aAttributes.data(), aAttributes.size());
}

CValue IValueTabularSection::CValueTabularSectionColumns::CValueTabularSectionColumnInfo::GetAttribute(attributeArg_t &aParams)
{
	switch (aParams.GetIndex())
	{
	case enColumnName: return GetColumnName();
	case enColumnTypes: return GetColumnTypes();
	case enColumnCaption: return GetColumnCaption();
	case enColumnWidth: return GetColumnWidth();
	}

	return CValue();
}

//////////////////////////////////////////////////////////////////////
//               CValueTabularSectionReturnLine                     //
//////////////////////////////////////////////////////////////////////

IValueTabularSection::CValueTabularSectionReturnLine::CValueTabularSectionReturnLine()
	: IValueTableReturnLine(), m_methods(new CMethods())
{
}

IValueTabularSection::CValueTabularSectionReturnLine::CValueTabularSectionReturnLine(IValueTabularSection *ownerTable, int line)
	: IValueTableReturnLine(), m_ownerTable(ownerTable), m_lineTable(line), m_methods(new CMethods())
{
}

IValueTabularSection::CValueTabularSectionReturnLine::~CValueTabularSectionReturnLine()
{
	wxDELETE(m_methods);
}

void IValueTabularSection::CValueTabularSectionReturnLine::PrepareNames() const
{
	std::vector<SEng> aAttributes;

	for (auto attribute : m_ownerTable->m_metaTable->GetObjectAttributes())
	{
		SEng aAttribute;
		aAttribute.sName = attribute->GetName();
		aAttribute.iName = attribute->GetMetaID();
		aAttribute.sSynonym = wxT("default");
		aAttributes.push_back(aAttribute);
	}

	m_methods->PrepareAttributes(aAttributes.data(), aAttributes.size());
}


void IValueTabularSection::CValueTabularSectionReturnLine::SetValueByMetaID(meta_identifier_t id, const CValue &cVal)
{
	m_ownerTable->SetValueByMetaID(m_lineTable, id, cVal);
}

CValue IValueTabularSection::CValueTabularSectionReturnLine::GetValueByMetaID(meta_identifier_t id) const
{
	return m_ownerTable->GetValueByMetaID(m_lineTable, id);
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

SO_VALUE_REGISTER(IValueTabularSection::CValueTabularSectionColumns, "tabularSectionColumn", CValueTabularSectionColumns, TEXT2CLSID("VL_TSCL"));
SO_VALUE_REGISTER(IValueTabularSection::CValueTabularSectionColumns::CValueTabularSectionColumnInfo, "tabularSectionColumnInfo", CValueTabularSectionColumnInfo, TEXT2CLSID("VL_CI"));
SO_VALUE_REGISTER(IValueTabularSection::CValueTabularSectionReturnLine, "tabularSectionRow", CValueTabularSectionReturnLine, TEXT2CLSID("VL_TBCR"));