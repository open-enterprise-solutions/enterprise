////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : ibValueRecordManagerObject — manager-style operations
//	              over a record-set (Exist / Read / Save / Delete). Thin
//	              wrappers over m_recordSet that adapt the unique-key API
//	              to the manager surface. Session-bound conn via
//	              ses_query.
////////////////////////////////////////////////////////////////////////////

#include "commonObject.h"

#include "backend/appData.h"
#include "backend/session/session.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/databaseErrorCodes.h"

#include "backend/metaCollection/attribute/metaAttributeObject.h"

#include "backend/system/systemManager.h"

bool ibValueRecordManagerObject::ExistData()
{
	ibConnectionScope scope = ibSession::Current()->OpenConnectionScope();

	if (!scope || !scope->IsOpen())
		ibBackendCoreException::Error(_("Database is not open!"));

	bool success = false;

	if (m_recordLine != nullptr) {

		scope.SafeBeginTransaction();

		wxString tableName = m_metaObject->GetTableNameDB(); int position = 1;
		wxString queryText = "SELECT * FROM " + tableName; bool firstWhere = true;

		for (const auto object : m_metaObject->GetGenericDimensionArrayObject()) {
			if (firstWhere) {
				queryText = queryText + " WHERE ";
			}
			queryText = queryText +
				(firstWhere ? " " : " AND ") + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(object);
			if (firstWhere) {
				firstWhere = false;
			}
		}

		ibPreparedStatement* statement = scope->PrepareStatement(queryText);

		if (statement != nullptr) {
			for (const auto object : m_metaObject->GetGenericDimensionArrayObject()) {
				ibValue retValue; m_recordLine->GetValueByMetaID(object->GetMetaID(), retValue);
				ibValueMetaObjectAttributeBase::SetValueAttribute(
					object,
					retValue,
					statement,
					position
				);
			}

			ibDatabaseResultSet* resultSet = statement->RunQueryWithResults();
			if (resultSet != nullptr) {
				success = resultSet->Next();
				scope->CloseResultSet(resultSet);
			}

			scope->CloseStatement(statement);
		}

		scope.SafeCommitTransaction();
	}

	return success;
}

bool ibValueRecordManagerObject::ReadData(const ibUniqueKeyPair& key)
{
	const auto db = ses_query;

	if (m_recordSet->ReadData(key)) {
		if (m_recordLine == nullptr) {
			m_recordLine = m_recordSet->GetRowAt(
				m_recordSet->GetItem(0)
			);
		}
		return true;
	}

	return false;
}

bool ibValueRecordManagerObject::SaveData(bool replace)
{
	const auto db = ses_query;

	if (m_recordSet->Selected()
		&& !DeleteData())
		return false;

	if (ExistData()) {
		wxString fillError =
			wxString::Format(_("This entry already exists. It is not possible to write a new value!"));
		ibValueSystemFunction::Message(fillError, ibStatusMessage::ibStatusMessage_Information);
		return false;
	}

	m_recordSet->m_keyValues.clear();
	wxASSERT(m_recordLine);
	for (const auto object : m_metaObject->GetGenericDimensionArrayObject()) {
		ibValue retValue; m_recordLine->GetValueByMetaID(object->GetMetaID(), retValue);
		m_recordSet->m_keyValues.insert_or_assign(
			object->GetMetaID(), retValue
		);
	}
	if (m_recordSet->WriteRecordSet(replace, false)) {
		m_objGuid.SetKeyValues(m_recordSet->m_keyValues);
		return true;
	}
	return false;
}

bool ibValueRecordManagerObject::DeleteData()
{
	const auto db = ses_query;
	return m_recordSet->DeleteRecordSet();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
