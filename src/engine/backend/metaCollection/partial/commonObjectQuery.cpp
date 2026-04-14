////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : base db reader/saver
////////////////////////////////////////////////////////////////////////////

#include "commonObject.h"

#include "backend/appData.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/databaseLayer/databaseErrorCodes.h"

#include "backend/metaCollection/partial/tabularSection/tabularSection.h"
#include "backend/metaCollection/attribute/metaAttributeObject.h"

//**********************************************************************************************************
//*                                          Common functions                                              *
//**********************************************************************************************************

wxString ibValueMetaObjectRecordDataRef::GetTableNameDB() const
{
	const wxString& className = GetClassName();
	wxASSERT(m_metaId != 0);
	return wxString::Format(wxT("%s%i"),
		className, GetMetaID());
}

int ibValueMetaObjectRecordDataMutableRef::ProcessAttribute(const wxString& tableName, const ibValueMetaObjectAttributeBase* srcAttr, const ibValueMetaObjectAttributeBase* dstAttr)
{
	//is null - create
	if (dstAttr == nullptr) {
		s_restructureInfo.AppendInfo(_("Create attribute ") + srcAttr->GetFullName());
	}
	// update 
	else if (srcAttr != nullptr) {
		if (!srcAttr->CompareObject(dstAttr))
			s_restructureInfo.AppendInfo(_("Changing attribute ") + srcAttr->GetFullName());
	}
	//delete 
	else if (srcAttr == nullptr) {
		s_restructureInfo.AppendInfo(_("Removed attribute ") + dstAttr->GetFullName());
	}

	return ibValueMetaObjectAttributeBase::ProcessAttribute(tableName, srcAttr, dstAttr);
}

int ibValueMetaObjectRecordDataMutableRef::ProcessTable(const wxString& tabularName, const ibValueMetaObjectTableData* srcTable, const ibValueMetaObjectTableData* dstTable)
{
	int retCode = 1;
	//is null - create
	if (dstTable == nullptr) {

		s_restructureInfo.AppendInfo(_("Create tabular section ") + srcTable->GetFullName());

		if (db_query->GetDatabaseLayerType() == DATABASELAYER_POSTGRESQL)
			retCode = db_query->RunQuery("CREATE TABLE %s (uuid uuid NOT NULL);", tabularName);
		else
			retCode = db_query->RunQuery("CREATE TABLE %s (uuid VARCHAR(36) NOT NULL);", tabularName);

		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
			return retCode;

		//default attributes
		for (const auto object : srcTable->GetGenericAttributeArrayObject()) {
			retCode = ProcessAttribute(tabularName,
				object, nullptr);
		}

		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
			return retCode;

		retCode = db_query->RunQuery("CREATE INDEX %s_INDEX ON %s (uuid);", tabularName, tabularName);
	}
	// update 
	else if (srcTable != nullptr) {

		if (!srcTable->CompareObject(dstTable))
			s_restructureInfo.AppendInfo(_("Changing tabular section ") + srcTable->GetFullName());

		for (const auto object : srcTable->GetGenericAttributeArrayObject()) {
			retCode = ProcessAttribute(tabularName,
				object, dstTable->FindAnyAttributeObjectByFilter(object->GetGuid())
			);
			if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
				return retCode;
		}
	}
	//delete 
	else if (srcTable == nullptr) {

		s_restructureInfo.AppendInfo(_("Removed tabular section ") + dstTable->GetFullName());

		retCode = db_query->RunQuery("DROP TABLE %s", tabularName);
	}

	return retCode;
}

bool ibValueMetaObjectRecordDataMutableRef::CreateAndUpdateTableDB(ibMetaDataConfiguration* srcMetaData, ibValueMetaObject* srcMetaObject, int flags)
{
	const wxString& tableName = GetTableNameDB(); int retCode = 1;

	if ((flags & createMetaTable) != 0) {

		s_restructureInfo.AppendInfo(_("Create ") + GetFullName());

		if (db_query->GetDatabaseLayerType() == DATABASELAYER_POSTGRESQL)
			retCode = db_query->RunQuery("CREATE TABLE %s (uuid uuid NOT NULL PRIMARY KEY);", tableName);
		else
			retCode = db_query->RunQuery("CREATE TABLE %s (uuid VARCHAR(36) NOT NULL PRIMARY KEY);", tableName);

		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
			return false;

		for (const auto object : GetPredefinedAttributeArrayObject()) {
			if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
				return false;
			if (ibValueMetaObjectRecordDataRef::IsDataReference(object->GetMetaID()))
				continue;
			retCode = ProcessAttribute(tableName,
				object, nullptr);
		}

		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
			return false;

		for (const auto object : GetAttributeArrayObject()) {
			if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
				return false;
			retCode = ProcessAttribute(tableName,
				object, nullptr);
		}

		for (const auto object : GetTableArrayObject()) {
			if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
				return false;
			retCode = ProcessTable(object->GetTableNameDB(),
				object, nullptr);
		}

		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
			return false;

		retCode = db_query->RunQuery("CREATE INDEX %s_INDEX ON %s (uuid);", tableName, tableName);

	}
	else if ((flags & updateMetaTable) != 0) {

		//if src is null then delete
		ibValueMetaObjectRecordDataRef* dstValue = nullptr;
		if (srcMetaObject->ConvertToValue(dstValue)) {

			if (!dstValue->CompareObject(this))
				s_restructureInfo.AppendInfo(_("Changed ") + GetFullName());

			//attributes from dst 
			for (const auto object : dstValue->GetPredefinedAttributeArrayObject()) {
				ibValueMetaObject* foundedMeta =
					ibValueMetaObjectRecordDataRef::FindPredefinedAttributeObjectByFilter(object->GetGuid());
				if (dstValue->IsDataReference(object->GetMetaID()))
					continue;
				if (foundedMeta == nullptr) {
					retCode = ProcessAttribute(tableName, nullptr, object);
					if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
						return false;
				}
			}

			//attributes current
			for (const auto object : GetPredefinedAttributeArrayObject()) {
				if (ibValueMetaObjectRecordDataRef::IsDataReference(object->GetMetaID()))
					continue;
				retCode = ProcessAttribute(tableName,
					object, dstValue->FindPredefinedAttributeObjectByFilter(object->GetGuid())
				);
				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
					return false;
			}

			//attributes from dst 
			for (const auto object : dstValue->GetAttributeArrayObject()) {
				ibValueMetaObject* foundedMeta =
					ibValueMetaObjectRecordDataRef::FindAttributeObjectByFilter(object->GetGuid());
				if (foundedMeta == nullptr) {
					retCode = ProcessAttribute(tableName, nullptr, object);
					if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
						return false;
				}
			}

			//attributes current
			for (const auto object : GetAttributeArrayObject()) {
				retCode = ProcessAttribute(tableName,
					object, dstValue->FindAttributeObjectByFilter(object->GetGuid())
				);
				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
					return false;
			}

			//tables from dst 
			for (const auto object : dstValue->GetTableArrayObject()) {
				ibValueMetaObject* foundedMeta =
					ibValueMetaObjectRecordDataRef::FindTableObjectByFilter(object->GetGuid());
				if (foundedMeta == nullptr) {
					retCode = ProcessTable(object->GetTableNameDB(), nullptr, object);
					if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
						return false;
				}
			}

			//tables current 
			for (const auto object : GetTableArrayObject()) {
				retCode = ProcessTable(object->GetTableNameDB(),
					object, dstValue->FindTableObjectByFilter(object->GetGuid())
				);
				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
					return false;
			}
		}
	}
	else if ((flags & deleteMetaTable) != 0) {

		s_restructureInfo.AppendInfo(_("Removed ") + GetFullName());

		if (db_query->TableExists(tableName)) {
			retCode = db_query->RunQuery("DROP TABLE %s", tableName);
		}
		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
			return false;

		for (const auto object : GetTableArrayObject()) {
			if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
				return false;
			const wxString& tabularName = object->GetTableNameDB();
			if (db_query->TableExists(tabularName)) {
				retCode = db_query->RunQuery("DROP TABLE %s", tabularName);
			}
		}
	}

	if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
		return false;

	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool ibValueMetaObjectRecordDataMutableRef::LoadTableData(const ibReaderMemory& reader)
{
	wxMemoryBuffer objectBuffer;

	if (reader.r_chunk(1, objectBuffer)) {

		const wxString& tableName = GetTableNameDB();
		
		wxString queryText;

		if (db_query->GetDatabaseLayerType() != DATABASELAYER_FIREBIRD)
			queryText = wxT("INSERT INTO ") + tableName + wxT(" (uuid");
		else
			queryText = wxT("UPDATE OR INSERT INTO ") + tableName + wxT(" (uuid");

		std::map<ibMetaID, int> assoc; int position = 2;

		for (const auto object : GetGenericAttributeArrayObject()) {
			if (IsDataReference(object->GetMetaID()))
				continue;
			queryText = queryText + ", " + ibValueMetaObjectAttributeBase::GetSQLFieldName(object);
			assoc.insert_or_assign(object->GetMetaID(),
				position);
			position += ibValueMetaObjectAttributeBase::GetSQLFieldCount(object);
		}
		queryText += ") VALUES (?";
		for (const auto object : GetGenericAttributeArrayObject()) {
			if (IsDataReference(object->GetMetaID()))
				continue;
			unsigned int fieldCount = ibValueMetaObjectAttributeBase::GetSQLFieldCount(object);
			for (unsigned int i = 0; i < fieldCount; i++) {
				queryText += ", ?";
			}
		}

		queryText += ") ";

		if (db_query->GetDatabaseLayerType() != DATABASELAYER_FIREBIRD)
			queryText += wxT("ON CONFLICT(uuid) DO UPDATE SET uuid = excluded.uuid;");
		else
			queryText += wxT("MATCHING(uuid);");

		ibPreparedStatement* statement = db_query->PrepareStatement(queryText);
		if (statement == nullptr)
			return false;

		ibReaderMemory* rowReaderPrev = nullptr;
		ibReaderMemory objectReader(objectBuffer);

		while (true) {

			ibReaderMemory* colReaderPrev = nullptr;

			u64 row = 0;
			ibReaderMemory* rowReader(objectReader.open_chunk_iterator(row, rowReaderPrev));

			if (rowReader == nullptr)
				break;

			while (!rowReader->eof()) {

				u64 col = 0;
				ibReaderMemory* colReader(rowReader->open_chunk_iterator(col, colReaderPrev));

				if (colReader == nullptr)
					break;

				ibValueMetaObjectAttributeBase* attribute = FindAnyObjectByFilter<ibValueMetaObjectAttributeBase, ibMetaID>(col);
				while (!colReader->eof()) {
					if (col > 0) {
						wxASSERT(attribute);
						int position = assoc[col];
						ibValueMetaObjectAttributeBase::SetBinaryData(attribute, *colReader, statement, position);
					}
					else {
						statement->SetParamString(1, colReader->r_stringZ());
					}
				}

				colReaderPrev = colReader;
			}

			statement->RunQuery();
			rowReaderPrev = rowReader;
		}

		statement->Close();
	}

	wxMemoryBuffer tableBuffer;
	if (reader.r_chunk(2, tableBuffer)) {

		ibReaderMemory* tableReaderPrev = nullptr;
		ibReaderMemory objectReader(tableBuffer);

		while (true) {

			ibReaderMemory* rowReaderPrev = nullptr;

			u64 table = 0;
			ibReaderMemory* tableReader(objectReader.open_chunk_iterator(table, tableReaderPrev));

			if (tableReader == nullptr)
				break;

			ibValueMetaObjectTableData* object = FindAnyObjectByFilter<ibValueMetaObjectTableData, ibMetaID>(table);
			if (object != nullptr) {

				const wxString& tableName = object->GetTableNameDB();
				wxString queryText = "INSERT INTO " + tableName + " (";
				queryText += "uuid";

				std::map<ibMetaID, int> assoc; int position = 2;

				for (const auto object : object->GetGenericAttributeArrayObject()) {
					queryText = queryText + ", " + ibValueMetaObjectAttributeBase::GetSQLFieldName(object);
					assoc.insert_or_assign(object->GetMetaID(),
						position);
					position += ibValueMetaObjectAttributeBase::GetSQLFieldCount(object);
				}
				queryText += ") VALUES (?";
				for (const auto object : object->GetGenericAttributeArrayObject()) {
					unsigned int fieldCount = ibValueMetaObjectAttributeBase::GetSQLFieldCount(object);
					for (unsigned int i = 0; i < fieldCount; i++) {
						queryText += ", ?";
					}
				}

				queryText += ");";

				ibPreparedStatement* statement = db_query->PrepareStatement(queryText);
				if (statement == nullptr)
					return false;

				while (!tableReader->eof()) {

					ibReaderMemory* colReaderPrev = nullptr;

					u64 row = 0;
					ibReaderMemory* rowReader(tableReader->open_chunk_iterator(row, rowReaderPrev));

					if (rowReader == nullptr)
						break;

					while (!rowReader->eof()) {

						u64 col = 0;
						ibReaderMemory* colReader(rowReader->open_chunk_iterator(col, colReaderPrev));

						if (colReader == nullptr)
							break;

						ibValueMetaObjectAttributeBase* attribute = object->FindAnyObjectByFilter<ibValueMetaObjectAttributeBase, ibMetaID>(col);
						while (!colReader->eof()) {
							if (col > 0) {
								wxASSERT(attribute);
								int position = assoc[col];
								ibValueMetaObjectAttributeBase::SetBinaryData(attribute, *colReader, statement, position);
							}
							else {
								statement->SetParamString(1, colReader->r_stringZ());
							}
						}

						colReaderPrev = colReader;
					}

					statement->RunQuery();
					rowReaderPrev = rowReader;
				}

				statement->Close();
			}

			tableReaderPrev = tableReader;
		}
	}

	return true;
}

bool ibValueMetaObjectRecordDataMutableRef::SaveTableData(ibWriterMemory& writer) const
{
	ibDatabaseResultSet* dbResultSet = db_query->RunQueryWithResults(wxT("SELECT * FROM %s"), GetTableNameDB());
	if (dbResultSet == nullptr)
		return false;

	unsigned int row = 0;

	ibWriterMemory objectWriter;

	while (dbResultSet->Next()) {

		ibWriterMemory rowWriter;

		ibWriterMemory rowGuidWriter;
		rowGuidWriter.w_stringZ(dbResultSet->GetResultString(wxT("uuid")));
		rowWriter.w_chunk(0, rowGuidWriter.buffer());

		for (const auto object : GetGenericAttributeArrayObject()) {

			if (IsDataReference(object->GetMetaID()))
				continue;

			ibWriterMemory attrWriter;
			ibValueMetaObjectAttributeBase::GetBinaryData(object, attrWriter, dbResultSet);
			rowWriter.w_chunk(object->GetMetaID(), attrWriter.buffer());
		}

		objectWriter.w_chunk(row++, rowWriter.buffer());
	}

	writer.w_chunk(1, objectWriter.buffer());

	db_query->CloseResultSet(dbResultSet);

	ibWriterMemory tableObjectWriter;
	for (const auto table : GetTableArrayObject()) {

		ibDatabaseResultSet* dbResultSet = db_query->RunQueryWithResults(wxT("SELECT * FROM %s"), table->GetTableNameDB());
		if (dbResultSet == nullptr)
			return false;

		unsigned int row = 0;

		ibWriterMemory tableWriter;

		while (dbResultSet->Next()) {

			ibWriterMemory rowWriter;
			ibWriterMemory rowGuidWriter;
			rowGuidWriter.w_stringZ(dbResultSet->GetResultString(wxT("uuid")));
			rowWriter.w_chunk(0, rowGuidWriter.buffer());

			for (const auto object : table->GetGenericAttributeArrayObject()) {
				ibWriterMemory attrWriter;
				ibValueMetaObjectAttributeBase::GetBinaryData(object, attrWriter, dbResultSet);
				rowWriter.w_chunk(object->GetMetaID(), attrWriter.buffer());
			}

			tableWriter.w_chunk(row++, rowWriter.buffer());
		}

		tableObjectWriter.w_chunk(table->GetMetaID(), tableWriter.buffer());

		db_query->CloseResultSet(dbResultSet);
	}

	writer.w_chunk(2, tableObjectWriter.buffer());
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////

int ibValueMetaObjectRecordDataEnumRef::ProcessEnumeration(const wxString& tableName, const ibValueMetaObjectEnum* srcEnum, const ibValueMetaObjectEnum* dstEnum)
{
	int retCode = 1;

	//is null - create
	if (dstEnum == nullptr) {

		s_restructureInfo.AppendInfo(_("Create enumeration ") + srcEnum->GetFullName());

		if (db_query->GetDatabaseLayerType() != DATABASELAYER_FIREBIRD)
			retCode = db_query->RunQuery("INSERT INTO %s (uuid) VALUES ('%s') ON CONFLICT(uuid) DO UPDATE SET uuid = excluded.uuid;", tableName, srcEnum->GetGuid().str());
		else
			retCode = db_query->RunQuery("UPDATE OR INSERT INTO %s (uuid) VALUES ('%s') MATCHING(uuid);", tableName, srcEnum->GetGuid().str());
	}
	// update
	else if (srcEnum != nullptr) {

		if (!srcEnum->CompareObject(dstEnum))
			s_restructureInfo.AppendInfo(_("Changing enumeration ") + srcEnum->GetFullName());

		if (db_query->GetDatabaseLayerType() != DATABASELAYER_FIREBIRD)
			retCode = db_query->RunQuery("INSERT INTO %s (uuid) VALUES ('%s') ON CONFLICT(uuid) DO UPDATE SET uuid = excluded.uuid;", tableName, srcEnum->GetGuid().str());
		else
			retCode = db_query->RunQuery("UPDATE OR INSERT INTO %s (uuid) VALUES ('%s') MATCHING(uuid);", tableName, srcEnum->GetGuid().str());
	}
	//delete 
	else if (srcEnum == nullptr) {

		s_restructureInfo.AppendInfo(_("Removed enumeration ") + dstEnum->GetFullName());

		retCode = db_query->RunQuery("DELETE FROM %s WHERE uuid = '%s' ;", tableName, dstEnum->GetGuid().str());
	}

	return retCode;
}

bool ibValueMetaObjectRecordDataEnumRef::CreateAndUpdateTableDB(ibMetaDataConfiguration* srcMetaData, ibValueMetaObject* srcMetaObject, int flags)
{
	const wxString& tableName = GetTableNameDB(); int retCode = 1;

	if ((flags & createMetaTable) != 0 || (flags & repairMetaTable) != 0) {

		if ((flags & createMetaTable) != 0) {

			s_restructureInfo.AppendInfo(_("Create ") + GetFullName());

			if (db_query->GetDatabaseLayerType() == DATABASELAYER_POSTGRESQL)
				retCode = db_query->RunQuery("CREATE TABLE %s (uuid uuid NOT NULL PRIMARY KEY);", tableName);
			else
				retCode = db_query->RunQuery("CREATE TABLE %s (uuid VARCHAR(36) NOT NULL PRIMARY KEY);", tableName);

			if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
				return false;

			if (db_query->GetDatabaseLayerType() != DATABASELAYER_FIREBIRD) {

				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
					return false;

				for (const auto object : GetEnumObjectArray()) {
					if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
						return false;
					retCode = ProcessEnumeration(tableName,
						object, nullptr);
				}
			}

			if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
				return false;

			retCode = db_query->RunQuery("CREATE INDEX %s_INDEX ON %s (uuid);", tableName, tableName);
		}
		else if ((flags & repairMetaTable) != 0) {

			if (db_query->GetDatabaseLayerType() == DATABASELAYER_FIREBIRD) {
				retCode = 1;
				for (const auto object : GetEnumObjectArray()) {
					if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
						return false;
					retCode = ProcessEnumeration(tableName,
						object, nullptr);
				}
			}
		}
	}
	else if ((flags & updateMetaTable) != 0) {

		//if src is null then delete
		ibValueMetaObjectRecordDataEnumRef* dstValue = nullptr;
		if (srcMetaObject->ConvertToValue(dstValue)) {

			if (!dstValue->CompareObject(this))
				s_restructureInfo.AppendInfo(_("Changed ") + GetFullName());

			//enums from dst 
			for (const auto object : dstValue->GetEnumObjectArray()) {
				ibValueMetaObject* foundedMeta =
					ibValueMetaObjectRecordDataEnumRef::FindEnumObjectByFilter(object->GetGuid());
				if (foundedMeta == nullptr) {
					retCode = ProcessEnumeration(tableName,
						nullptr, object);
					if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
						return false;
				}
			}
			//enums current
			for (const auto object : GetEnumObjectArray()) {
				retCode = ProcessEnumeration(tableName,
					object, dstValue->FindEnumObjectByFilter(object->GetGuid())
				);
				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
					return false;
			}
		}
	}
	else if ((flags & deleteMetaTable) != 0) {

		s_restructureInfo.AppendInfo(_("Removed ") + GetFullName());

		if (db_query->TableExists(tableName)) {
			retCode = db_query->RunQuery("DROP TABLE %s", tableName);
		}

		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
			return false;
	}

	if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
		return false;

	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////

int ibValueMetaObjectRecordDataHierarchyMutableRef::ProcessPredefinedValue(const wxString& tableName,
	const wxObjectDataPtr<ibPredefinedValueObject>& srcPredefined, const wxObjectDataPtr<ibPredefinedValueObject>& dstPredefined)
{
	int retCode = 1;

	//is null - create
	if (dstPredefined == nullptr) {

		wxString queryText;

		queryText = wxT("INSERT INTO ") + tableName + wxT(" (uuid");

		queryText = queryText + ", " + ibValueMetaObjectAttributeBase::GetSQLFieldName(m_propertyAttributePredefined->GetMetaObject());
		queryText = queryText + ", " + ibValueMetaObjectAttributeBase::GetSQLFieldName(m_propertyAttributeCode->GetMetaObject());
		queryText = queryText + ", " + ibValueMetaObjectAttributeBase::GetSQLFieldName(m_propertyAttributeDescription->GetMetaObject());
		queryText = queryText + ", " + ibValueMetaObjectAttributeBase::GetSQLFieldName(m_propertyAttributeIsFolder->GetMetaObject());
		queryText = queryText + ", " + ibValueMetaObjectAttributeBase::GetSQLFieldName(m_propertyAttributeParent->GetMetaObject());

		queryText += wxT(") VALUES (?");

		for (unsigned int i = 0; i < ibValueMetaObjectAttributeBase::GetSQLFieldCount(m_propertyAttributePredefined->GetMetaObject()); i++) queryText += wxT(", ?");
		for (unsigned int i = 0; i < ibValueMetaObjectAttributeBase::GetSQLFieldCount(m_propertyAttributeCode->GetMetaObject()); i++) queryText += wxT(", ?");
		for (unsigned int i = 0; i < ibValueMetaObjectAttributeBase::GetSQLFieldCount(m_propertyAttributeDescription->GetMetaObject()); i++) queryText += wxT(", ?");
		for (unsigned int i = 0; i < ibValueMetaObjectAttributeBase::GetSQLFieldCount(m_propertyAttributeIsFolder->GetMetaObject()); i++) queryText += wxT(", ?");
		for (unsigned int i = 0; i < ibValueMetaObjectAttributeBase::GetSQLFieldCount(m_propertyAttributeParent->GetMetaObject()); i++) queryText += wxT(", ?");

		queryText += wxT(");");

		ibPreparedStatement* dbStatement = db_query->PrepareStatement(queryText);

		if (dbStatement == nullptr)
			return false;

		dbStatement->SetParamString(1, srcPredefined->GetPredefinedGuid().str());

		const wxObjectDataPtr<ibPredefinedValueObject>& predefinedParentValue = srcPredefined->GetPredefinedParent();
		ibValuePtr<ibValueReferenceDataObject> referenceValue(
			ibValueReferenceDataObject::Create(this, predefinedParentValue != nullptr ? predefinedParentValue->GetPredefinedGuid() : wxNullGuid));

		int position = 2;

		ibValueMetaObjectAttributeBase::SetValueAttribute(m_propertyAttributePredefined->GetMetaObject(), srcPredefined->GetPredefinedName(), dbStatement, position);
		ibValueMetaObjectAttributeBase::SetValueAttribute(m_propertyAttributeCode->GetMetaObject(), srcPredefined->GetPredefinedCode(), dbStatement, position);
		ibValueMetaObjectAttributeBase::SetValueAttribute(m_propertyAttributeDescription->GetMetaObject(), srcPredefined->GetPredefinedDescription(), dbStatement, position);
		ibValueMetaObjectAttributeBase::SetValueAttribute(m_propertyAttributeIsFolder->GetMetaObject(), srcPredefined->IsPredefinedFolder(), dbStatement, position);
		ibValueMetaObjectAttributeBase::SetValueAttribute(m_propertyAttributeParent->GetMetaObject(), referenceValue, dbStatement, position);

		retCode =
			dbStatement->RunQuery();

		db_query->CloseStatement(dbStatement);
	}
	// update 
	else if (srcPredefined != nullptr) {

		wxString queryText;

		if (db_query->GetDatabaseLayerType() != DATABASELAYER_FIREBIRD)
			queryText = wxT("INSERT INTO ") + tableName + wxT(" (uuid");
		else
			queryText = wxT("UPDATE OR INSERT INTO ") + tableName + wxT(" (uuid");

		queryText = queryText + ", " + ibValueMetaObjectAttributeBase::GetSQLFieldName(m_propertyAttributePredefined->GetMetaObject());
		queryText = queryText + ", " + ibValueMetaObjectAttributeBase::GetSQLFieldName(m_propertyAttributeParent->GetMetaObject());

		queryText += wxT(") VALUES (?");

		for (unsigned int i = 0; i < ibValueMetaObjectAttributeBase::GetSQLFieldCount(m_propertyAttributePredefined->GetMetaObject()); i++) queryText += wxT(", ?");
		for (unsigned int i = 0; i < ibValueMetaObjectAttributeBase::GetSQLFieldCount(m_propertyAttributeParent->GetMetaObject()); i++) queryText += wxT(", ?");

		queryText += wxT(") ");

		if (db_query->GetDatabaseLayerType() != DATABASELAYER_FIREBIRD)
			queryText += wxT("ON CONFLICT(uuid) DO UPDATE SET uuid = excluded.uuid;");
		else
			queryText += wxT("MATCHING(uuid);");

		ibPreparedStatement* dbStatement = db_query->PrepareStatement(queryText);

		if (dbStatement == nullptr)
			return false;

		dbStatement->SetParamString(1, srcPredefined->GetPredefinedGuid().str());

		const wxObjectDataPtr<ibPredefinedValueObject>& predefinedParentValue = srcPredefined->GetPredefinedParent();
		ibValuePtr<ibValueReferenceDataObject> referenceValue(
			ibValueReferenceDataObject::Create(this, predefinedParentValue != nullptr ? predefinedParentValue->GetPredefinedGuid() : wxNullGuid));

		int position = 2;

		ibValueMetaObjectAttributeBase::SetValueAttribute(m_propertyAttributePredefined->GetMetaObject(), srcPredefined->GetPredefinedName(), dbStatement, position);
		ibValueMetaObjectAttributeBase::SetValueAttribute(m_propertyAttributeParent->GetMetaObject(), referenceValue, dbStatement, position);

		retCode =
			dbStatement->RunQuery();

		db_query->CloseStatement(dbStatement);
	}
	//delete
	else if (srcPredefined == nullptr) {

		wxString queryText;

		if (db_query->GetDatabaseLayerType() != DATABASELAYER_FIREBIRD)
			queryText = wxT("INSERT INTO ") + tableName + wxT(" (uuid");
		else
			queryText = wxT("UPDATE OR INSERT INTO ") + tableName + wxT(" (uuid");

		queryText = queryText + ", " + ibValueMetaObjectAttributeBase::GetSQLFieldName(m_propertyAttributePredefined->GetMetaObject());
		queryText = queryText + ", " + ibValueMetaObjectAttributeBase::GetSQLFieldName(m_propertyAttributeDeletionMark->GetMetaObject());

		queryText += wxT(") VALUES (?");

		for (unsigned int i = 0; i < ibValueMetaObjectAttributeBase::GetSQLFieldCount(m_propertyAttributePredefined->GetMetaObject()); i++) queryText += wxT(", ?");
		for (unsigned int i = 0; i < ibValueMetaObjectAttributeBase::GetSQLFieldCount(m_propertyAttributeDeletionMark->GetMetaObject()); i++) queryText += wxT(", ?");

		queryText += wxT(") ");

		if (db_query->GetDatabaseLayerType() != DATABASELAYER_FIREBIRD)
			queryText += wxT("ON CONFLICT(uuid) DO UPDATE SET uuid = excluded.uuid;");
		else
			queryText += wxT("MATCHING(uuid);");

		ibPreparedStatement* dbStatement = db_query->PrepareStatement(queryText);

		if (dbStatement == nullptr)
			return false;

		dbStatement->SetParamString(1, dstPredefined->GetPredefinedGuid().str());

		int position = 2;

		ibValueMetaObjectAttributeBase::SetValueAttribute(m_propertyAttributePredefined->GetMetaObject(), wxT(""), dbStatement, position);
		ibValueMetaObjectAttributeBase::SetValueAttribute(m_propertyAttributeDeletionMark->GetMetaObject(), true, dbStatement, position);

		retCode =
			dbStatement->RunQuery();

		db_query->CloseStatement(dbStatement);
	}

	return retCode;
}

bool ibValueMetaObjectRecordDataHierarchyMutableRef::CreateAndUpdateTableDB(ibMetaDataConfiguration* srcMetaData, ibValueMetaObject* srcMetaObject, int flags)
{
	if (!ibValueMetaObjectRecordDataMutableRef::CreateAndUpdateTableDB(srcMetaData, srcMetaObject, flags))
		return false;

	const wxString& tableName = GetTableNameDB(); int retCode = 1;

	if ((flags & createMetaTable) != 0 || (flags & repairMetaTable) != 0) {

		if ((flags & createMetaTable) != 0) {

			if (db_query->GetDatabaseLayerType() != DATABASELAYER_FIREBIRD) {

				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
					return false;

				for (const auto& object : m_predefinedObjectVector) {
					if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
						return false;
					retCode = ProcessPredefinedValue(tableName,
						object, wxObjectDataPtr<ibPredefinedValueObject>(nullptr));
				}
			}

			if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
				return false;
		}
		else if ((flags & repairMetaTable) != 0) {

			if (db_query->GetDatabaseLayerType() == DATABASELAYER_FIREBIRD) {
				retCode = 1;
				for (const auto object : m_predefinedObjectVector) {
					if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
						return false;
					retCode = ProcessPredefinedValue(tableName,
						object, wxObjectDataPtr<ibPredefinedValueObject>(nullptr));
				}
			}
		}
	}
	else if ((flags & updateMetaTable) != 0) {

		//if src is null then delete
		ibValueMetaObjectRecordDataHierarchyMutableRef* dstValue = nullptr;
		if (srcMetaObject->ConvertToValue(dstValue)) {

			//values from dst 
			for (const auto object : dstValue->m_predefinedObjectVector) {
				const wxObjectDataPtr<ibPredefinedValueObject>& predefinedValue =
					ibValueMetaObjectRecordDataHierarchyMutableRef::FindPredefinedValue(object->GetPredefinedGuid());
				if (predefinedValue == nullptr) {
					retCode = ProcessPredefinedValue(tableName,
						wxObjectDataPtr<ibPredefinedValueObject>(nullptr), object);
					if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
						return false;
				}
			}
			//values current
			for (const auto object : m_predefinedObjectVector) {
				retCode = ProcessPredefinedValue(tableName,
					object, dstValue->FindPredefinedValue(object->GetPredefinedGuid())
				);
				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
					return false;
			}
		}
	}

	if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
		return false;

	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////

wxString ibValueMetaObjectRegisterData::GetTableNameDB() const
{
	const wxString& className = GetClassName();
	wxASSERT(m_metaId != 0);
	return wxString::Format(wxT("%s%i"),
		className, GetMetaID());
}

int ibValueMetaObjectRegisterData::ProcessDimension(const wxString& tableName, const ibValueMetaObjectAttributeBase* srcAttr, const ibValueMetaObjectAttributeBase* dstAttr)
{
	//is null - create
	if (dstAttr == nullptr) {
		s_restructureInfo.AppendInfo(_("Create dimension ") + srcAttr->GetFullName());
	}
	// update 
	else if (srcAttr != nullptr) {
		if (!srcAttr->CompareObject(dstAttr))
			s_restructureInfo.AppendInfo(_("Changing dimension ") + srcAttr->GetFullName());
	}
	//delete 
	else if (srcAttr == nullptr) {
		s_restructureInfo.AppendInfo(_("Removed dimension ") + dstAttr->GetFullName());
	}

	return ibValueMetaObjectAttributeBase::ProcessAttribute(tableName, srcAttr, dstAttr);
}

int ibValueMetaObjectRegisterData::ProcessResource(const wxString& tableName, const ibValueMetaObjectAttributeBase* srcAttr, const ibValueMetaObjectAttributeBase* dstAttr)
{
	//is null - create
	if (dstAttr == nullptr) {
		s_restructureInfo.AppendInfo(_("Create resource ") + srcAttr->GetFullName());
	}
	// update 
	else if (srcAttr != nullptr) {
		if (!srcAttr->CompareObject(dstAttr))
			s_restructureInfo.AppendInfo(_("Changing resource ") + srcAttr->GetFullName());
	}
	//delete 
	else if (srcAttr == nullptr) {
		s_restructureInfo.AppendInfo(_("Removed resource ") + dstAttr->GetFullName());
	}

	return ibValueMetaObjectAttributeBase::ProcessAttribute(tableName, srcAttr, dstAttr);
}

int ibValueMetaObjectRegisterData::ProcessAttribute(const wxString& tableName, const ibValueMetaObjectAttributeBase* srcAttr, const ibValueMetaObjectAttributeBase* dstAttr)
{
	//is null - create
	if (dstAttr == nullptr) {
		s_restructureInfo.AppendInfo(_("Create attribute ") + srcAttr->GetFullName());
	}
	// update 
	else if (srcAttr != nullptr) {
		if (!srcAttr->CompareObject(dstAttr))
			s_restructureInfo.AppendInfo(_("Changing attribute ") + srcAttr->GetFullName());
	}
	//delete 
	else if (srcAttr == nullptr) {
		s_restructureInfo.AppendInfo(_("Removed attribute ") + dstAttr->GetFullName());
	}

	return ibValueMetaObjectAttributeBase::ProcessAttribute(tableName, srcAttr, dstAttr);
}

////////////////////////////////////////////////////////////////////////////////////////

bool ibValueMetaObjectRegisterData::UpdateCurrentRecords(const wxString& tableName, ibValueMetaObjectRegisterData* dst)
{
	if (HasRecorder()) {
		ibValueMetaObjectAttributePredefined* metaRec = dst->GetRegisterRecorder();
		if (metaRec == nullptr)
			return false;
		const ibTypeDescription& typeDesc = metaRec->GetTypeDesc();
		for (auto& clsid : typeDesc.GetClsidList()) {
			if (!(*m_propertyAttributeRecorder)->ContainType(clsid)) {
				int retCode = DATABASE_LAYER_QUERY_RESULT_ERROR; wxString clsStr; clsStr << static_cast<wxLongLong_t>(clsid);
				retCode = db_query->RunQuery(wxT("DELETE FROM %s WHERE %s_RTRef = ") + clsStr, tableName, metaRec->GetFieldNameDB());
				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
					return false;
			}
		}
	}

	return true;
}

////////////////////////////////////////////////////////////////////////////////////////

bool ibValueMetaObjectRegisterData::CreateAndUpdateTableDB(ibMetaDataConfiguration* srcMetaData, ibValueMetaObject* srcMetaObject, int flags)
{
	const wxString& tableName = GetTableNameDB();

	int retCode = 1;

	if ((flags & createMetaTable) != 0) {

		s_restructureInfo.AppendInfo(_("Append register ") + GetFullName());

		if (db_query->GetDatabaseLayerType() == DATABASELAYER_POSTGRESQL)
			retCode = db_query->RunQuery(wxT("CREATE TABLE %s (rowData BYTEA);"), tableName);
		else
			retCode = db_query->RunQuery(wxT("CREATE TABLE %s (rowData BLOB);"), tableName);

		for (const auto object : GetPredefinedAttributeArrayObject()) {
			if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
				return false;
			retCode = ProcessAttribute(tableName,
				object, nullptr);
		}

		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
			return false;

		for (const auto object : GetDimentionArrayObject()) {
			if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
				return false;
			retCode = ProcessDimension(tableName,
				object, nullptr);
		}

		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
			return false;

		for (const auto object : GetResourceArrayObject()) {
			if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
				return false;
			retCode = ProcessResource(tableName,
				object, nullptr);
		}

		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
			return false;

		for (const auto object : GetAttributeArrayObject()) {
			if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
				return false;
			retCode = ProcessAttribute(tableName,
				object, nullptr);
		}

		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
			return false;

		wxString queryText;
		if (HasRecorder()) {
			ibValueMetaObjectAttributePredefined* attributeRecorder = GetRegisterRecorder();
			wxASSERT(attributeRecorder);
			queryText += ibValueMetaObjectAttributeBase::GetSQLFieldName(attributeRecorder);
			ibValueMetaObjectAttributePredefined* attributeNumberLine = GetRegisterLineNumber();
			wxASSERT(attributeNumberLine);
			queryText += "," + ibValueMetaObjectAttributeBase::GetSQLFieldName(attributeNumberLine);
		}
		else {
			bool firstMatching = true;
			for (const auto object : GetGenericDimentionArrayObject()) {
				queryText += (firstMatching ? "" : ",") + ibValueMetaObjectAttributeBase::GetSQLFieldName(object);
				if (firstMatching) {
					firstMatching = false;
				}
			}
		}

		if (!queryText.IsEmpty()) {
			retCode = db_query->RunQuery(
				wxT("CREATE INDEX %s_INDEX ON %s (") + queryText + wxT(");"), tableName, tableName);
		}
	}
	else if ((flags & updateMetaTable) != 0) {

		//if src is null then delete
		ibValueMetaObjectRegisterData* dstValue = nullptr;
		if (srcMetaObject->ConvertToValue(dstValue)) {

			if (!UpdateCurrentRecords(tableName, dstValue))
				return false;

			if (!dstValue->CompareObject(this))
				s_restructureInfo.AppendInfo(_("Changed register ") + GetFullName());

			//attributes from dst 
			for (const auto object : dstValue->GetPredefinedAttributeArrayObject()) {
				ibValueMetaObject* foundedMeta =
					ibValueMetaObjectRegisterData::FindPredefinedAttributeObjectByFilter(object->GetGuid());
				if (foundedMeta == nullptr) {
					retCode = ProcessAttribute(tableName, nullptr, object);
					if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
						return false;
				}
			}

			//attributes current
			for (const auto object : GetPredefinedAttributeArrayObject()) {
				retCode = ProcessAttribute(tableName,
					object, dstValue->FindPredefinedAttributeObjectByFilter(object->GetGuid())
				);
				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
					return false;
			}

			//dimensions from dst 
			for (const auto object : dstValue->GetDimentionArrayObject()) {
				ibValueMetaObject* foundedMeta =
					ibValueMetaObjectRegisterData::FindDimensionObjectByFilter(object->GetGuid());
				if (foundedMeta == nullptr) {
					retCode = ProcessDimension(tableName, nullptr, object);
					if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
						return false;
				}
			}

			//dimensions current
			for (const auto object : GetDimentionArrayObject()) {
				retCode = ProcessDimension(tableName,
					object, dstValue->FindDimensionObjectByFilter(object->GetGuid())
				);
				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
					return false;
			}

			//resources from dst 
			for (const auto object : dstValue->GetResourceArrayObject()) {
				ibValueMetaObject* foundedMeta =
					ibValueMetaObjectRegisterData::FindResourceObjectByFilter(object->GetGuid());
				if (foundedMeta == nullptr) {
					retCode = ProcessResource(tableName, nullptr, object);
					if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
						return false;
				}
			}

			//resources current
			for (const auto object : GetResourceArrayObject()) {
				retCode = ProcessResource(tableName,
					object, dstValue->FindResourceObjectByFilter(object->GetGuid())
				);
				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
					return false;
			}

			//attributes from dst 
			for (const auto object : dstValue->GetAttributeArrayObject()) {
				ibValueMetaObject* foundedMeta =
					ibValueMetaObjectRegisterData::FindAttributeObjectByFilter(object->GetGuid());
				if (foundedMeta == nullptr) {
					retCode = ProcessAttribute(tableName, nullptr, object);
					if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
						return false;
				}
			}

			//attributes current
			for (const auto object : GetAttributeArrayObject()) {
				retCode = ProcessAttribute(tableName,
					object, dstValue->FindAttributeObjectByFilter(object->GetGuid())
				);
				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
					return false;
			}
		}
	}
	else if ((flags & deleteMetaTable) != 0) {

		s_restructureInfo.AppendInfo(_("Remove register ") + GetFullName());

		retCode = db_query->RunQuery("DROP TABLE %s", tableName);
	}

	if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
		return false;

	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool ibValueMetaObjectRegisterData::LoadTableData(const ibReaderMemory& reader)
{
	wxMemoryBuffer objectBuffer;

	if (reader.r_chunk(1, objectBuffer)) {

		const wxString& tableName = GetTableNameDB();
		wxString queryText = "INSERT INTO " + tableName + " (";

		std::map<ibMetaID, int> assoc; int position = 1;
		bool firstInsert = true, firstValue = true;
		for (const auto object : GetGenericAttributeArrayObject()) {
			queryText = queryText + (firstInsert ? wxT(" ") : wxT(", ")) + ibValueMetaObjectAttributeBase::GetSQLFieldName(object);
			assoc.insert_or_assign(object->GetMetaID(),
				position);
			position += ibValueMetaObjectAttributeBase::GetSQLFieldCount(object);
			firstInsert = false;
		}
		queryText += ") VALUES (";
		for (const auto object : GetGenericAttributeArrayObject()) {
			unsigned int fieldCount = ibValueMetaObjectAttributeBase::GetSQLFieldCount(object);
			for (unsigned int i = 0; i < fieldCount; i++) {
				queryText += (firstValue ? wxT("?") : wxT(", ?"));
				firstValue = false;
			}
		}

		queryText += ");";

		ibPreparedStatement* statement = db_query->PrepareStatement(queryText);
		if (statement == nullptr)
			return false;

		ibReaderMemory* rowReaderPrev = nullptr;
		ibReaderMemory objectReader(objectBuffer);

		while (true) {

			ibReaderMemory* colReaderPrev = nullptr;

			u64 row = 0;
			ibReaderMemory* rowReader(objectReader.open_chunk_iterator(row, rowReaderPrev));

			if (rowReader == nullptr)
				break;

			while (!rowReader->eof()) {

				u64 col = 0;
				ibReaderMemory* colReader(rowReader->open_chunk_iterator(col, colReaderPrev));

				if (colReader == nullptr)
					break;

				ibValueMetaObjectAttributeBase* attribute = FindAnyObjectByFilter<ibValueMetaObjectAttributeBase, ibMetaID>(col);
				while (!colReader->eof()) {
					wxASSERT(attribute);
					int position = assoc[col];
					ibValueMetaObjectAttributeBase::SetBinaryData(attribute, *colReader, statement, position);
				}

				colReaderPrev = colReader;
			}

			statement->RunQuery();
			rowReaderPrev = rowReader;
		}

		statement->Close();
	}

	return true;
}

bool ibValueMetaObjectRegisterData::SaveTableData(ibWriterMemory& writer) const
{
	ibDatabaseResultSet* dbResultSet = db_query->RunQueryWithResults(wxT("SELECT * FROM %s"), GetTableNameDB());
	if (dbResultSet == nullptr)
		return false;

	unsigned int row = 0;

	ibWriterMemory objectWriter;

	while (dbResultSet->Next()) {

		ibWriterMemory rowWriter;

		for (const auto object : GetGenericAttributeArrayObject()) {

			ibWriterMemory attrWriter;
			ibValueMetaObjectAttributeBase::GetBinaryData(object, attrWriter, dbResultSet);
			rowWriter.w_chunk(object->GetMetaID(), attrWriter.buffer());
		}

		objectWriter.w_chunk(row++, rowWriter.buffer());
	}

	writer.w_chunk(1, objectWriter.buffer());

	db_query->CloseResultSet(dbResultSet);
	return true;
}

//**********************************************************************************************************
//*                                          Query functions                                               *
//**********************************************************************************************************

#include "backend/system/systemManager.h"

bool ibValueRecordDataObjectRef::ReadData()
{
	return ReadData(m_objGuid);
}

bool ibValueRecordDataObjectRef::ReadData(const ibGuid& srcGuid)
{
	if (db_query != nullptr && !db_query->IsOpen())
		ibBackendCoreException::Error(_("Database is not open!"));
	else if (db_query == nullptr)
		ibBackendCoreException::Error(_("Database is not open!"));

	if (m_newObject && !srcGuid.isValid())
		return false;
	wxASSERT(m_metaObject);
	wxString tableName = m_metaObject->GetTableNameDB();
	if (db_query->TableExists(tableName)) {

		ibDatabaseResultSet* resultSet = nullptr;
		if (db_query->GetDatabaseLayerType() != DATABASELAYER_FIREBIRD)
			resultSet = db_query->RunQueryWithResults("SELECT * FROM " + tableName + " WHERE uuid = '" + srcGuid.str() + "' LIMIT 1;");
		else
			resultSet = db_query->RunQueryWithResults("SELECT FIRST 1 * FROM " + tableName + " WHERE uuid = '" + srcGuid.str() + "';");

		if (resultSet == nullptr) return false;
		bool succes = false;
		if (resultSet->Next()) {
			succes = true;
			//load other attributes 
			for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
				if (!m_metaObject->IsDataReference(object->GetMetaID())) {
					ibValueMetaObjectAttributeBase::GetValueAttribute(object, m_listObjectValue[object->GetMetaID()], resultSet);
				}
			}
			for (const auto object : m_metaObject->GetGenericTableArrayObject()) {
				ibValueTabularSectionDataObjectRef* tabularSection = new ibValueTabularSectionDataObjectRef(this, object);
				if (!tabularSection->LoadData(srcGuid)) succes = false;
				m_listObjectValue.insert_or_assign(object->GetMetaID(), tabularSection);
			}
		}
		db_query->CloseResultSet(resultSet);
		return succes;
	}
	return false;
}

bool ibValueRecordDataObjectRef::SaveData()
{
	if (db_query != nullptr && !db_query->IsOpen())
		ibBackendCoreException::Error(_("Database is not open!"));
	else if (db_query == nullptr)
		ibBackendCoreException::Error(_("Database is not open!"));

	//check fill attributes 
	bool fillCheck = true;
	wxASSERT(m_metaObject);
	for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
		if (object->FillCheck()) {
			if (m_listObjectValue[object->GetMetaID()].IsEmpty()) {
				wxString fillError =
					wxString::Format(_("""%s"" is a required field"), object->GetSynonym());
				ibValueSystemFunction::Message(fillError, ibStatusMessage::ibStatusMessage_Information);
				fillCheck = false;
			}
		}
	}

	if (!fillCheck)
		return false;

	if (!ibValueRecordDataObjectRef::DeleteData())
		return false;

	const wxString& tableName = m_metaObject->GetTableNameDB();
	wxString queryText = "INSERT INTO " + tableName + " (";
	queryText += "uuid";
	for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
		if (m_metaObject->IsDataReference(object->GetMetaID()))
			continue;
		queryText = queryText + ", " + ibValueMetaObjectAttributeBase::GetSQLFieldName(object);
	}
	queryText += ") VALUES (?";
	for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
		if (m_metaObject->IsDataReference(object->GetMetaID()))
			continue;
		unsigned int fieldCount = ibValueMetaObjectAttributeBase::GetSQLFieldCount(object);
		for (unsigned int i = 0; i < fieldCount; i++) {
			queryText += ", ?";
		}
	}
	queryText += ");";

	ibPreparedStatement* statement = db_query->PrepareStatement(queryText);
	if (statement == nullptr)
		return false;

	m_objGuid = m_reference_impl->m_guid;
	statement->SetParamString(1, m_objGuid.str());

	int position = 2;

	for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
		if (m_metaObject->IsDataReference(object->GetMetaID()))
			continue;
		ibValueMetaObjectAttributeBase::SetValueAttribute(
			object,
			m_listObjectValue.at(object->GetMetaID()),
			statement,
			position
		);
	}

	bool hasError =
		statement->RunQuery() == DATABASE_LAYER_QUERY_RESULT_ERROR;

	db_query->CloseStatement(statement);

	//table parts
	if (!hasError) {
		for (const auto object : m_metaObject->GetGenericTableArrayObject()) {
			ibValueTabularSectionDataObjectBase* tabularSection = nullptr;
			if (m_listObjectValue[object->GetMetaID()].ConvertToValue(tabularSection)) {
				if (!tabularSection->SaveData()) {
					hasError = true;
					break;
				}
			}
			else if (m_listObjectValue[object->GetMetaID()].GetType() != TYPE_NULL) {
				hasError = true;
				break;
			}
		}
	}

	if (!hasError) {
		m_newObject = false;
	}

	return !hasError;
}

bool ibValueRecordDataObjectRef::DeleteData()
{
	if (db_query != nullptr && !db_query->IsOpen())
		ibBackendCoreException::Error(_("Database is not open!"));
	else if (db_query == nullptr)
		ibBackendCoreException::Error(_("Database is not open!"));

	if (m_newObject)
		return true;
	const wxString& tableName = m_metaObject->GetTableNameDB();
	//table parts
	for (const auto object : m_metaObject->GetTableArrayObject()) {
		ibValueTabularSectionDataObjectBase* tabularSection = nullptr;
		if (m_listObjectValue[object->GetMetaID()].ConvertToValue(tabularSection)) {
			if (!tabularSection->DeleteData())
				return false;
		}
		else if (m_listObjectValue[object->GetMetaID()].GetType() != TYPE_NULL)
			return false;

	}
	db_query->RunQuery("DELETE FROM " + tableName + " WHERE uuid = '" + m_objGuid.str() + "';");
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool ibValueRecordDataObjectRef::IsSetUniqueIdentifier() const
{
	const auto object = m_metaObject->GetAttributeForCode();
	if (object != nullptr)
		return !m_listObjectValue.at(object->GetMetaID()).IsEmpty();
	return false;
}

bool ibValueRecordDataObjectRef::GenerateUniqueIdentifier(const wxString& strPrefix)
{
	const auto object = m_metaObject->GetAttributeForCode();
	if (object != nullptr && !IsSetUniqueIdentifier()) {
		m_listObjectValue.insert_or_assign(object->GetMetaID(),
			GenerateNextIdentifier(object, strPrefix));
		return true;
	}
	return false;
}

bool ibValueRecordDataObjectRef::ResetUniqueIdentifier()
{
	const auto object = m_metaObject->GetAttributeForCode();
	if (object != nullptr && IsSetUniqueIdentifier()) {
		m_listObjectValue.insert_or_assign(object->GetMetaID(), object->CreateValue());
		return true;
	}
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool ibValueRecordDataObjectHierarchyRef::ReadData()
{
	if (ibValueRecordDataObjectRef::ReadData()) {
		ibValueMetaObjectRecordDataHierarchyMutableRef* metaFolder = GetMetaObject();
		wxASSERT(metaFolder);
		ibValue isFolder; ibValueRecordDataObjectHierarchyRef::GetValueByMetaID(*metaFolder->GetDataIsFolder(), isFolder);
		if (isFolder.GetBoolean())
			m_objMode = ibObjectMode::OBJECT_FOLDER;
		else
			m_objMode = ibObjectMode::OBJECT_ITEM;
		return true;
	}
	return false;
}

bool ibValueRecordDataObjectHierarchyRef::ReadData(const ibGuid& srcGuid)
{
	if (ibValueRecordDataObjectRef::ReadData(srcGuid)) {
		ibValueMetaObjectRecordDataHierarchyMutableRef* metaFolder = GetMetaObject();
		wxASSERT(metaFolder);
		ibValue isFolder; ibValueRecordDataObjectHierarchyRef::GetValueByMetaID(*metaFolder->GetDataIsFolder(), isFolder);
		if (isFolder.GetBoolean())
			m_objMode = ibObjectMode::OBJECT_FOLDER;
		else
			m_objMode = ibObjectMode::OBJECT_ITEM;
		return true;
	}
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ibValueRecordDataObjectRef::SetDeletionMark(bool deletionMark)
{
	if (m_newObject)
		return;

	if (m_metaObject != nullptr) {
		ibValueMetaObjectAttributePredefined* attributeDeletionMark = m_metaObject->GetDataDeletionMark();
		wxASSERT(attributeDeletionMark);
		ibValueRecordDataObjectRef::SetValueByMetaID(*attributeDeletionMark, deletionMark);
	}

	SaveModify();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool ibValueRecordManagerObject::ExistData()
{
	if (db_query != nullptr && !db_query->IsOpen())
		ibBackendCoreException::Error(_("Database is not open!"));
	else if (db_query == nullptr)
		ibBackendCoreException::Error(_("Database is not open!"));

	bool success = false;

	if (m_recordLine != nullptr) {

		db_query->BeginTransaction();

		wxString tableName = m_metaObject->GetTableNameDB(); int position = 1;
		wxString queryText = "SELECT * FROM " + tableName; bool firstWhere = true;

		for (const auto object : m_metaObject->GetGenericDimentionArrayObject()) {
			if (firstWhere) {
				queryText = queryText + " WHERE ";
			}
			queryText = queryText +
				(firstWhere ? " " : " AND ") + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(object);
			if (firstWhere) {
				firstWhere = false;
			}
		}

		ibPreparedStatement* statement = db_query->PrepareStatement(queryText);

		if (statement != nullptr) {
			for (const auto object : m_metaObject->GetGenericDimentionArrayObject()) {
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
				db_query->CloseResultSet(resultSet);
			}

			db_query->CloseStatement(statement);
		}

		db_query->Commit();
	}

	return success;
}

bool ibValueRecordManagerObject::ReadData(const ibUniqueKeyPair& key)
{
	if (db_query != nullptr && !db_query->IsOpen())
		ibBackendCoreException::Error(_("Database is not open!"));
	else if (db_query == nullptr)
		ibBackendCoreException::Error(_("Database is not open!"));

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
	if (db_query != nullptr && !db_query->IsOpen())
		ibBackendCoreException::Error(_("Database is not open!"));
	else if (db_query == nullptr)
		ibBackendCoreException::Error(_("Database is not open!"));

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
	for (const auto object : m_metaObject->GetGenericDimentionArrayObject()) {
		ibValue retValue; m_recordLine->GetValueByMetaID(object->GetMetaID(), retValue);
		m_recordSet->m_keyValues.insert_or_assign(
			object->GetMetaID(), retValue
		);
	}
	if (m_recordSet->WriteRecordSet(replace, false)) {
		m_objGuid.SetKeyPair(m_metaObject, m_recordSet->m_keyValues);
		return true;
	}
	return false;
}

bool ibValueRecordManagerObject::DeleteData()
{
	if (db_query != nullptr && !db_query->IsOpen())
		ibBackendCoreException::Error(_("Database is not open!"));
	else if (db_query == nullptr)
		ibBackendCoreException::Error(_("Database is not open!"));
	return m_recordSet->DeleteRecordSet();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool ibValueRecordSetObject::ExistData()
{
	if (db_query != nullptr && !db_query->IsOpen())
		ibBackendCoreException::Error(_("Database is not open!"));
	else if (db_query == nullptr)
		ibBackendCoreException::Error(_("Database is not open!"));

	wxString tableName = m_metaObject->GetTableNameDB(); int position = 1;
	wxString queryText = "SELECT * FROM " + tableName; bool firstWhere = true;

	for (const auto object : m_metaObject->GetGenericDimentionArrayObject()) {
		if (!ibValueRecordSetObject::FindKeyValue(object->GetMetaID()))
			continue;
		if (firstWhere) {
			queryText = queryText + " WHERE ";
		}
		queryText = queryText +
			(firstWhere ? " " : " AND ") + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(object);
		if (firstWhere) {
			firstWhere = false;
		}
	}

	ibPreparedStatement* statement = db_query->PrepareStatement(queryText);
	if (statement == nullptr)
		return false;

	for (const auto object : m_metaObject->GetGenericDimentionArrayObject()) {
		if (!ibValueRecordSetObject::FindKeyValue(object->GetMetaID()))
			continue;
		ibValueMetaObjectAttributeBase::SetValueAttribute(
			object,
			m_keyValues.at(object->GetMetaID()),
			statement,
			position
		);
	}

	ibDatabaseResultSet* resultSet = statement->RunQueryWithResults();
	if (resultSet == nullptr)
		return false;
	bool founded = false;
	if (resultSet->Next())
		founded = true;
	db_query->CloseResultSet(resultSet);
	db_query->CloseStatement(statement);
	return founded;
}

bool ibValueRecordSetObject::ExistData(ibNumber& lastNum)
{
	if (db_query != nullptr && !db_query->IsOpen())
		ibBackendCoreException::Error(_("Database is not open!"));
	else if (db_query == nullptr)
		ibBackendCoreException::Error(_("Database is not open!"));

	wxString tableName = m_metaObject->GetTableNameDB(); int position = 1;
	wxString queryText = "SELECT * FROM " + tableName; bool firstWhere = true;

	for (const auto object : m_metaObject->GetGenericDimentionArrayObject()) {
		if (!ibValueRecordSetObject::FindKeyValue(object->GetMetaID()))
			continue;
		if (firstWhere) {
			queryText = queryText + " WHERE ";
		}
		queryText = queryText +
			(firstWhere ? " " : " AND ") + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(object);
		if (firstWhere) {
			firstWhere = false;
		}
	}

	ibPreparedStatement* statement = db_query->PrepareStatement(queryText);
	if (statement == nullptr)
		return false;

	for (const auto object : m_metaObject->GetGenericDimentionArrayObject()) {
		if (!ibValueRecordSetObject::FindKeyValue(object->GetMetaID()))
			continue;
		ibValueMetaObjectAttributeBase::SetValueAttribute(
			object,
			m_keyValues.at(object->GetMetaID()),
			statement,
			position
		);
	}

	ibDatabaseResultSet* resultSet = statement->RunQueryWithResults();
	if (resultSet == nullptr)
		return false;
	bool founded = false; lastNum = 1;
	while (resultSet->Next()) {
		ibValue numLine; ibValueMetaObjectAttributeBase::GetValueAttribute(m_metaObject->GetRegisterLineNumber(), numLine, resultSet);
		if (numLine > lastNum) {
			lastNum = numLine.GetNumber();
		}
		founded = true;
	}
	db_query->CloseResultSet(resultSet);
	db_query->CloseStatement(statement);
	return founded;
}

bool ibValueRecordSetObject::ReadData(const ibUniqueKeyPair& key)
{
	if (db_query != nullptr && !db_query->IsOpen())
		ibBackendCoreException::Error(_("Database is not open!"));
	else if (db_query == nullptr)
		ibBackendCoreException::Error(_("Database is not open!"));

	ibValueModelTableBase::Clear(); int position = 1;

	wxString tableName = m_metaObject->GetTableNameDB();
	wxString queryText = "SELECT * FROM " + tableName; bool firstWhere = true;

	for (const auto object : m_metaObject->GetGenericDimentionArrayObject()) {
		if (!key.FindKey(object->GetMetaID()))
			continue;
		if (firstWhere) {
			queryText = queryText + " WHERE ";
		}
		queryText = queryText +
			(firstWhere ? " " : " AND ") + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(object);
		if (firstWhere) {
			firstWhere = false;
		}
	}
	ibPreparedStatement* statement = db_query->PrepareStatement(queryText);
	if (statement == nullptr)
		return false;
	for (const auto object : m_metaObject->GetGenericDimentionArrayObject()) {
		if (!key.FindKey(object->GetMetaID()))
			continue;
		ibValueMetaObjectAttributeBase::SetValueAttribute(
			object,
			key.GetKey(object->GetMetaID()),
			statement,
			position
		);
	}
	ibDatabaseResultSet* resultSet = statement->RunQueryWithResults();
	if (resultSet == nullptr)
		return false;
	while (resultSet->Next()) {
		ibValueTableRow* rowData = new ibValueTableRow();
		for (const auto object : m_metaObject->GetGenericDimentionArrayObject()) {
			ibValueMetaObjectAttributeBase::GetValueAttribute(object, rowData->AppendTableValue(object->GetMetaID()), resultSet);
		}
		for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
			ibValueMetaObjectAttributeBase::GetValueAttribute(object, rowData->AppendTableValue(object->GetMetaID()), resultSet);
		}
		ibValueModelTableBase::Append(rowData, !ibBackendException::IsEvalMode());
		m_selected = true;
	}

	db_query->CloseResultSet(resultSet);
	db_query->CloseStatement(statement);

	return GetRowCount() > 0;
}

bool ibValueRecordSetObject::ReadData()
{
	if (db_query != nullptr && !db_query->IsOpen())
		ibBackendCoreException::Error(_("Database is not open!"));
	else if (db_query == nullptr)
		ibBackendCoreException::Error(_("Database is not open!"));

	ibValueModelTableBase::Clear(); int position = 1;

	wxString tableName = m_metaObject->GetTableNameDB();
	wxString queryText = "SELECT * FROM " + tableName; bool firstWhere = true;

	for (const auto object : m_metaObject->GetGenericDimentionArrayObject()) {
		if (!ibValueRecordSetObject::FindKeyValue(object->GetMetaID()))
			continue;
		if (firstWhere) {
			queryText = queryText + " WHERE ";
		}
		queryText = queryText +
			(firstWhere ? " " : " AND ") + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(object);
		if (firstWhere) {
			firstWhere = false;
		}
	}
	ibPreparedStatement* statement = db_query->PrepareStatement(queryText);
	if (statement == nullptr)
		return false;
	for (const auto object : m_metaObject->GetGenericDimentionArrayObject()) {
		if (!ibValueRecordSetObject::FindKeyValue(object->GetMetaID()))
			continue;
		ibValueMetaObjectAttributeBase::SetValueAttribute(
			object,
			m_keyValues.at(object->GetMetaID()),
			statement,
			position
		);
	}
	ibDatabaseResultSet* resultSet = statement->RunQueryWithResults();
	if (resultSet == nullptr)
		return false;
	while (resultSet->Next()) {
		ibValueTableRow* rowData = new ibValueTableRow();
		for (const auto object : m_metaObject->GetGenericDimentionArrayObject()) {
			ibValueMetaObjectAttributeBase::GetValueAttribute(object, rowData->AppendTableValue(object->GetMetaID()), resultSet);
		}
		for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
			ibValueMetaObjectAttributeBase::GetValueAttribute(object, rowData->AppendTableValue(object->GetMetaID()), resultSet);
		}
		ibValueModelTableBase::Append(rowData, !ibBackendException::IsEvalMode());
		m_selected = true;
	}

	db_query->CloseResultSet(resultSet);
	db_query->CloseStatement(statement);

	return GetRowCount() > 0;
}

bool ibValueRecordSetObject::SaveData(bool replace, bool clearTable)
{
	if (db_query != nullptr && !db_query->IsOpen())
		ibBackendCoreException::Error(_("Database is not open!"));
	else if (db_query == nullptr)
		ibBackendCoreException::Error(_("Database is not open!"));

	//check fill attributes 
	bool fillCheck = true; long currLine = 1;
	for (long row = 0; row < GetRowCount(); row++) {
		for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
			if (object->FillCheck()) {
				ibValueTableRow* node = GetViewData<ibValueTableRow>(GetItem(row));
				wxASSERT(node);
				if (node->IsEmptyValue(object->GetMetaID())) {
					wxString fillError =
						wxString::Format(_("The %s is required on line %i of the %s"), object->GetSynonym(), currLine, m_metaObject->GetSynonym());
					ibValueSystemFunction::Message(fillError, ibStatusMessage::ibStatusMessage_Information);
					fillCheck = false;
				}
			}
		}
		currLine++;
	}

	if (!fillCheck)
		return false;

	ibNumber numberLine = 1, oldNumberLine = 1;

	if (m_metaObject->HasRecorder() &&
		ibValueRecordSetObject::ExistData(oldNumberLine)) {
		if (replace && !ibValueRecordSetObject::DeleteData())
			return false;
		if (!replace) {
			numberLine = oldNumberLine;
		}
	}
	else if (ibValueRecordSetObject::ExistData()) {
		if (replace && !ibValueRecordSetObject::DeleteData())
			return false;
		if (!replace) {
			numberLine = oldNumberLine;
		}
	}

	wxString tableName = m_metaObject->GetTableNameDB(); wxString queryText; bool firstUpdate = true;
	if (db_query->GetDatabaseLayerType() != DATABASELAYER_FIREBIRD) {
		queryText = "INSERT INTO " + tableName + " (";
	}
	else {
		queryText = "UPDATE OR INSERT INTO " + tableName + " (";
	}
	for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
		queryText += (firstUpdate ? "" : ",") + ibValueMetaObjectAttributeBase::GetSQLFieldName(object);
		if (firstUpdate) {
			firstUpdate = false;
		}
	}
	queryText += ") VALUES ("; bool firstInsert = true;
	for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
		unsigned int fieldCount = ibValueMetaObjectAttributeBase::GetSQLFieldCount(object);
		for (unsigned int i = 0; i < fieldCount; i++) {
			queryText += (firstInsert ? "?" : ",?");
			if (firstInsert) {
				firstInsert = false;
			}
		}
	}

	if (db_query->GetDatabaseLayerType() != DATABASELAYER_FIREBIRD) {
		queryText += ")";
	}
	else {
		queryText += ") MATCHING (";
		if (m_metaObject->HasRecorder()) {
			ibValueMetaObjectAttributePredefined* attributeRecorder = m_metaObject->GetRegisterRecorder();
			wxASSERT(attributeRecorder);
			queryText += ibValueMetaObjectAttributeBase::GetSQLFieldName(attributeRecorder);
			ibValueMetaObjectAttributePredefined* attributeNumberLine = m_metaObject->GetRegisterLineNumber();
			wxASSERT(attributeNumberLine);
			queryText += "," + ibValueMetaObjectAttributeBase::GetSQLFieldName(attributeNumberLine);
		}
		else
		{
			bool firstMatching = true;
			for (const auto object : m_metaObject->GetGenericDimentionArrayObject()) {
				queryText += (firstMatching ? "" : ",") + ibValueMetaObjectAttributeBase::GetSQLFieldName(object);
				if (firstMatching) {
					firstMatching = false;
				}
			}
		}
		queryText += ");";
	}

	ibPreparedStatement* statement = db_query->PrepareStatement(queryText);
	if (statement == nullptr)
		return false;

	bool hasError = false;

	for (long row = 0; row < GetRowCount(); row++) {
		if (hasError)
			break;
		int position = 1;
		for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
			auto foundedKey = m_keyValues.find(object->GetMetaID());
			if (foundedKey != m_keyValues.end()) {
				ibValueMetaObjectAttributeBase::SetValueAttribute(
					object,
					foundedKey->second,
					statement,
					position
				);
			}
			else if (m_metaObject->IsRegisterLineNumber(object->GetMetaID())) {
				ibValueMetaObjectAttributeBase::SetValueAttribute(
					object,
					numberLine++,
					statement,
					position
				);
			}
			else {
				ibValueTableRow* node = GetViewData< ibValueTableRow>(GetItem(row));
				wxASSERT(node);
				ibValueMetaObjectAttributeBase::SetValueAttribute(
					object,
					node->GetTableValue(object->GetMetaID()),
					statement,
					position
				);
			}
		}

		hasError = statement->RunQuery() == DATABASE_LAYER_QUERY_RESULT_ERROR;
	}

	db_query->CloseStatement(statement);

	if (!hasError && !SaveVirtualTable())
		return false;

	if (!hasError && clearTable)
		ibValueModelTableBase::Clear();
	else if (!clearTable)
		m_selected = true;

	return !hasError;
}

bool ibValueRecordSetObject::DeleteData()
{
	if (db_query != nullptr && !db_query->IsOpen())
		ibBackendCoreException::Error(_("Database is not open!"));
	else if (db_query == nullptr)
		ibBackendCoreException::Error(_("Database is not open!"));

	wxString tableName = m_metaObject->GetTableNameDB();
	wxString queryText = "DELETE FROM " + tableName; bool firstWhere = true;
	for (const auto object : m_metaObject->GetGenericDimentionArrayObject()) {
		if (!ibValueRecordSetObject::FindKeyValue(object->GetMetaID()))
			continue;
		if (firstWhere) {
			queryText = queryText + " WHERE ";
		}
		queryText = queryText +
			(firstWhere ? " " : " AND ") + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(object);
		if (firstWhere) {
			firstWhere = false;
		}
	}

	ibPreparedStatement* statement = db_query->PrepareStatement(queryText); int position = 1;

	if (statement == nullptr)
		return false;

	for (const auto object : m_metaObject->GetGenericDimentionArrayObject()) {
		if (!ibValueRecordSetObject::FindKeyValue(object->GetMetaID()))
			continue;
		ibValueMetaObjectAttributeBase::SetValueAttribute(
			object,
			m_keyValues.at(object->GetMetaID()),
			statement,
			position
		);
	}

	statement->RunQuery();
	db_query->CloseStatement(statement);
	return DeleteVirtualTable();
}

//**********************************************************************************************************
//*                                          Code generator												   *
//**********************************************************************************************************

ibValue ibValueRecordDataObjectRef::GenerateNextIdentifier(ibValueMetaObjectAttributeBase* attribute, const wxString& strPrefix)
{
	if (db_query != nullptr && !db_query->IsOpen())
		ibBackendCoreException::Error(_("Database is not open!"));
	else if (db_query == nullptr)
		ibBackendCoreException::Error(_("Database is not open!"));

	wxASSERT(attribute);

	const int interval = 20000101;

	ibNumber resultCode = 1;
	const ibTypeDescription& typeDesc = attribute->GetTypeDesc();

	if (db_query->GetDatabaseLayerType() != DATABASELAYER_FIREBIRD) {

		ibDatabaseResultSet* resultSet = db_query->RunQueryWithResults(
			wxT("SELECT interval, meta_guid, prefix, number FROM %s WHERE interval = %s AND meta_guid = '%s' AND prefix = '%s';"),
			sequence_table,
			stringUtils::IntToStr(interval),
			m_metaObject->GetDocPath(),
			strPrefix
		);

		if (resultSet == nullptr)
			return attribute->CreateValue();

		if (resultSet->Next())
			resultCode = resultSet->GetResultNumber(wxT("number")) + 1;

		db_query->RunQuery(
			wxT("INSERT INTO %s (interval, meta_guid, prefix, number) VALUES (%s, '%s', '%s', %s) ON CONFLICT(interval, meta_guid, prefix) DO UPDATE SET interval = excluded.interval, meta_guid = excluded.meta_guid, prefix = excluded.prefix, number = excluded.number;"),
			sequence_table,
			stringUtils::IntToStr(interval),
			m_metaObject->GetDocPath(),
			strPrefix, //prefix
			resultCode.ToString()
		);

		db_query->CloseResultSet(resultSet);
	}
	else {

		ibDatabaseResultSet* resultSet = db_query->RunQueryWithResults(
			wxT("SELECT interval, meta_guid, prefix, number FROM %s WHERE interval = %s AND meta_guid = '%s' AND prefix = '%s' FOR UPDATE WITH LOCK;"),
			sequence_table,
			stringUtils::IntToStr(interval),
			m_metaObject->GetDocPath(),
			strPrefix
		);

		if (resultSet == nullptr)
			return attribute->CreateValue();

		if (resultSet->Next())
			resultCode = resultSet->GetResultNumber(wxT("number")) + 1;

		db_query->RunQuery(
			wxT("UPDATE OR INSERT INTO %s (interval, meta_guid, prefix, number) VALUES (%s, '%s', '%s', %s) MATCHING (interval, meta_guid, prefix);"),
			sequence_table,
			stringUtils::IntToStr(interval),
			m_metaObject->GetDocPath(),
			strPrefix, //prefix
			resultCode.ToString()
		);

		db_query->CloseResultSet(resultSet);
	}

	if (attribute->ContainType(ibValueTypes::TYPE_NUMBER)) {
		return resultCode;
	}
	else if (attribute->ContainType(ibValueTypes::TYPE_STRING)) {

		wxString strNumber;

		std::stringstream strStreamNumber;
		if (strPrefix.Length() < typeDesc.GetLength()) {
			strStreamNumber << strPrefix <<
				std::setw(typeDesc.GetLength() - strPrefix.Length()) << std::setfill('0') << resultCode;
		}
		else {
			strStreamNumber <<
				std::setw(strPrefix.Length()) << std::setfill('0') << resultCode;
		}

		strNumber = strStreamNumber.str();
		return strNumber;
	}

	wxASSERT_MSG(false, "m_metaAttribute->GetClsidList() != ibValueTypes::TYPE_NUMBER"
		"|| m_metaAttribute->GetClsidList() != ibValueTypes::TYPE_STRING");

	return wxEmptyValue;
}