////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : constants - db
////////////////////////////////////////////////////////////////////////////

#include "constant.h"
#include "metadata/metadata.h"
#include "compiler/systemObjects.h"
#include "appData.h"

//***********************************************************************
//*                           contant value                             *
//***********************************************************************

CConstantObjectValue *CMetaConstantObject::CreateObjectValue()
{
	IModuleManager *moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	CConstantObjectValue *pDataRef = NULL;

	if (appData->DesignerMode()) {
		if (!moduleManager->FindCompileModule(m_moduleObject, pDataRef))
			return new CConstantObjectValue(this);
	}
	else {
		pDataRef = new CConstantObjectValue(this);
	}

	return pDataRef;
}

//*********************************************************************************************
//*                                  CConstantObjectValue                                     *
//*********************************************************************************************

bool CConstantObjectValue::InitializeObject(const CConstantObjectValue *source)
{
	IMetadata *m_metaData = m_metaObject->GetMetadata();
	wxASSERT(m_metaData);
	IModuleManager *moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (!m_compileModule) {
		m_compileModule = new CCompileModule(m_metaObject->GetModuleObject());
		m_compileModule->SetParent(moduleManager->GetCompileModule());
	}

	if (appData->EnterpriseMode() ||
		appData->ServiceMode())
	{
		m_procUnit = new CProcUnit();
		m_procUnit->SetParent(moduleManager->GetProcUnit());

		try
		{
			m_compileModule->Compile();
		}
		catch (const CTranslateError *err)
		{
			CSystemObjects::Raise(err->what());
			return false;
		};

		m_procUnit->Execute(m_compileModule->m_cByteCode, true);
	}

	//is Ok
	return true;
}

CConstantObjectValue::CConstantObjectValue(const CConstantObjectValue &source)
	: m_metaObject(source.m_metaObject)
{
	InitializeObject(&source);
}

CConstantObjectValue::CConstantObjectValue(CMetaConstantObject *metaObject)
	: m_metaObject(metaObject)
{
	InitializeObject();
}

#include "databaseLayer/databaseLayer.h"

CValue CConstantObjectValue::GetConstValue()
{
	CValue ret;

	if (appData->EnterpriseMode()) {

		wxString tableName = m_metaObject->GetTableNameDB();
		wxString fieldName = m_metaObject->GetFieldNameDB();

		if (databaseLayer->TableExists(tableName)) {
			DatabaseResultSet *resultSet = databaseLayer->RunQueryWithResults("SELECT FIRST 1 %s FROM %s", fieldName, tableName);

			if (resultSet == NULL) {
				return ret;
			}

			if (resultSet->Next()) {
				switch (m_metaObject->GetTypeObject())
				{
				case eValueTypes::TYPE_BOOLEAN: ret = resultSet->GetResultBool(fieldName); break;
				case eValueTypes::TYPE_NUMBER: ret = resultSet->GetResultNumber(fieldName); break;
				case eValueTypes::TYPE_DATE: ret = resultSet->GetResultDate(fieldName); break;
				case eValueTypes::TYPE_STRING: ret = resultSet->GetResultString(fieldName); break;
				default:
				{
					wxMemoryBuffer bufferData;
					resultSet->GetResultBlob(fieldName, bufferData);
					if (!bufferData.IsEmpty()) {
						ret = CValueReference::CreateFromPtr(
							m_metaObject->GetMetadata(), bufferData.GetData()
						);
					}break;
				}
				}
			}
			else {
				switch (m_metaObject->GetTypeObject())
				{
				case eValueTypes::TYPE_BOOLEAN: ret = eValueTypes::TYPE_BOOLEAN; break;
				case eValueTypes::TYPE_NUMBER: ret = eValueTypes::TYPE_NUMBER; break;
				case eValueTypes::TYPE_DATE: ret = eValueTypes::TYPE_DATE; break;
				case eValueTypes::TYPE_STRING: ret = eValueTypes::TYPE_STRING; break;
				default: ret = new CValueReference(
					m_metaObject->GetMetadata(), m_metaObject->GetTypeObject()
				); break;
				}
			}

			resultSet->Close();
		}
	}

	return ret;
}

#include "databaseLayer/databaseErrorCodes.h" 

void CConstantObjectValue::SetConstValue(const CValue &cValue)
{
	if (appData->EnterpriseMode()) {

		wxString tableName = m_metaObject->GetTableNameDB();
		wxString fieldName = m_metaObject->GetFieldNameDB();

		if (databaseLayer->TableExists(tableName)) {

			databaseLayer->BeginTransaction();

			{
				CValue cancel = false;
				m_procUnit->CallFunction("BeforeWrite", cancel);

				if (cancel.GetBoolean()) {
					databaseLayer->RollBack(); CSystemObjects::Raise("failed to write object in db!"); return;
				}
			}

			CValueTypeDescription *td = new CValueTypeDescription(m_metaObject->GetClassTypeObject(),
				m_metaObject->GetNumberQualifier(),
				m_metaObject->GetDateQualifier(),
				m_metaObject->GetStringQualifier()
			);

			CValue adjustVal = td->AdjustValue(cValue);
			wxDELETE(td);

			//check fill attributes 
			bool fillCheck = true;

			if (m_metaObject->FillCheck()) {
				if (adjustVal.IsEmpty()) {
					wxString fillError =
						wxString::Format(_("""%s"" is a required field"), m_metaObject->GetSynonym());
					CSystemObjects::Message(fillError, eStatusMessage::eStatusMessage_Information);
					fillCheck = false;
				}
			}

			if (!fillCheck) {
				return;
			}

			PreparedStatement *statement = 
				databaseLayer->PrepareStatement("UPDATE OR INSERT INTO %s (%s, RECORD_KEY) VALUES (?, '6') MATCHING(RECORD_KEY);", tableName, fieldName);

			if (statement == NULL) {
				return;
			}

			switch (m_metaObject->GetTypeObject())
			{
			case eValueTypes::TYPE_BOOLEAN: statement->SetParamBool(1, adjustVal.GetBoolean()); break;
			case eValueTypes::TYPE_NUMBER: statement->SetParamNumber(1, adjustVal.GetNumber()); break;
			case eValueTypes::TYPE_DATE: statement->SetParamDate(1, adjustVal.GetDate()); break;
			case eValueTypes::TYPE_STRING: statement->SetParamString(1, adjustVal.GetString()); break;
			default:
			{
				CValueReference *reference = dynamic_cast<CValueReference *>(adjustVal.GetRef());
				if (reference) {
					reference->SetBinaryData(1, statement);
				}
			}
			}

			bool hasError = statement->RunQuery() == DATABASE_LAYER_QUERY_RESULT_ERROR;
			databaseLayer->CloseStatement(statement);

			{
				CValue cancel = false;
				m_procUnit->CallFunction("OnWrite", cancel);
				if (cancel.GetBoolean()) {
					databaseLayer->RollBack(); CSystemObjects::Raise("failed to write object in db!"); return;
				}
			}

			databaseLayer->Commit();
		}
	}
}

