////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metadata-side DDL — schema build / migrate / serialize.
//	              Pure metadata layer: Process* compares src/dst attribute
//	              shapes, CreateAndUpdateTableDB emits CREATE / ALTER /
//	              DROP, Dump/RestoreTable round-trips row data on metadata
//	              import / export. db_query (DDL channel), no ses_query.
////////////////////////////////////////////////////////////////////////////

#include "commonObject.h"

#include "backend/appData.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/databaseErrorCodes.h"

#include "backend/metaCollection/partial/tabularSection/tabularSection.h"
#include "backend/metaCollection/attribute/metaAttributeObject.h"
#include "backend/metaCollection/partial/reference/reference.h"   // ibValueReferenceDataObject (materialisation)
#include "backend/databaseLayer/databaseResultSet.h"              // GetResultString
#include "backend/objCtor.h"                                      // ibCtorMetaValueType (reference-target resolution)
#include "backend/metaData.h"                                     // ibMetaData::GetTypeCtor

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

		//attributes from dst no longer present in src → drop the column
		//(mirrors the top-level object update path; without it a removed tabular
		// attribute leaves an orphan column that is never dropped or altered)
		for (const auto object : dstTable->GetGenericAttributeArrayObject()) {
			if (srcTable->FindAnyAttributeObjectByFilter(object->GetGuid()) == nullptr) {
				retCode = ProcessAttribute(tabularName, nullptr, object);
				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
					return retCode;
			}
		}

		//attributes current → add new / alter changed
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
			// The data-reference (self-reference) predefined attribute now gets a real
			// column: its _RRRef stores the row's own ibReference[guid][metaID], byte-
			// identical to any reference TO this row — so a dot-walk JOIN matches
			// source.fldX_RRRef = target.<selfref>_RRRef. (Reads still synthesise the
			// reference value from uuid; this column is write-for-join.)
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

bool ibValueMetaObjectRecordDataMutableRef::RestoreTable(const ibReaderMemory& reader)
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

bool ibValueMetaObjectRecordDataMutableRef::DumpTable(ibWriterMemory& writer) const
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

		// Idempotency: this branch is the seed path (initial creation of a
		// table). It can fire repeatedly across Apply runs — either
		// intentionally (self-heal a table left empty by a prior partial
		// failure) or accidentally (gate above lifted in metadataConfig).
		// Cheapest reliable check is "row already there?" before the
		// INSERT — keeps the multi-column INSERT shape unchanged and
		// preserves any user edits to code/description on existing rows.
		// Two round-trips per row, but only during Apply (rare, manual).
		{
			ibPreparedStatement* probe = db_query->PrepareStatement(
				wxT("SELECT 1 FROM ") + tableName + wxT(" WHERE uuid = ?"));
			if (probe == nullptr)
				return false;
			probe->SetParamString(1, srcPredefined->GetPredefinedGuid().str());
			ibDatabaseResultSet* probeRs = probe->ExecuteQuery();
			bool rowExists = (probeRs != nullptr) && probeRs->Next();
			if (probeRs) {
				probeRs->Close();
				db_query->CloseResultSet(probeRs);
			}
			db_query->CloseStatement(probe);
			if (rowExists)
				return 1;
		}

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

		for (const auto object : GetDimensionArrayObject()) {
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
			for (const auto object : GetGenericDimensionArrayObject()) {
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
			for (const auto object : dstValue->GetDimensionArrayObject()) {
				ibValueMetaObject* foundedMeta =
					ibValueMetaObjectRegisterData::FindDimensionObjectByFilter(object->GetGuid());
				if (foundedMeta == nullptr) {
					retCode = ProcessDimension(tableName, nullptr, object);
					if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
						return false;
				}
			}

			//dimensions current
			for (const auto object : GetDimensionArrayObject()) {
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

		// Guard like the ref/enum delete paths: a register added and removed before
		// any Apply has no physical table, and an unguarded DROP would abort the Apply.
		if (db_query->TableExists(tableName))
			retCode = db_query->RunQuery("DROP TABLE %s", tableName);
	}

	if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
		return false;

	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool ibValueMetaObjectRegisterData::RestoreTable(const ibReaderMemory& reader)
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

bool ibValueMetaObjectRegisterData::DumpTable(ibWriterMemory& writer) const
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

// --- vended queryables — the L3 navigation LIVES here, not on the metaobject ---
// The metaobject vends one of these (a stable member) via GetQueryable() and provides
// only primitives (FindObjectByFilter / GetTableNameDB / IsDataReference / guidName);
// the queryable owns the query-interface logic, reaching the concrete leaf through the
// metaobject's virtual primitives. (decouple §22.4e)

// catalog / document / charts / enums — identity = the row-key column (guidName);
// the reference attribute is the row's own key.
const ibValueMetaObjectAttributeBase* ibRecordQueryable::ResolveAttribute(const wxString& name) const {
	return m_meta->FindObjectByFilter<ibValueMetaObjectAttributeBase>(name, { g_metaAttributeCLSID, g_metaPredefinedAttributeCLSID });
}
const ibValueMetaObjectAttributeBase* ibRecordQueryable::ResolveAttribute(const ibMetaID& id) const {
	return m_meta->FindObjectByFilter<ibValueMetaObjectAttributeBase>(id, { g_metaAttributeCLSID, g_metaPredefinedAttributeCLSID });
}
wxString ibRecordQueryable::GetQueryTableName() const { return m_meta->GetTableNameDB(); }
ibMetaID ibRecordQueryable::GetQueryMetaID() const { return m_meta->GetMetaID(); }
wxString ibRecordQueryable::GetRowKeyColumn() const { return guidName; }
std::vector<ibQuerySortItem> ibRecordQueryable::GetIdentitySort() const { return { ibQuerySortItem{ nullptr, true } }; }
bool ibRecordQueryable::IsReferenceAttribute(const ibMetaID& id) const { return m_meta->IsDataReference(id); }
wxString ibRecordQueryable::GetReferenceKeyColumn() const {
	// The self-reference _RRRef column (ibReference[guid][metaID] blob) the row now
	// stores — a dot-walk binds a reference attribute to this catalog/document on it:
	// source.<ref>_RRRef = target.<dataref>_RRRef, byte-identical.
	const ibValueMetaObjectAttributeBase* refAttr = m_meta->GetDataReference();
	if (refAttr == nullptr)
		return wxString();
	for (auto& field : ibValueMetaObjectAttributeBase::GetSQLFieldData(refAttr))
		if (field.m_type == ibValueMetaObjectAttributeBase::ibFieldTypes_Reference)
			return field.m_field.m_fieldRefName.m_fieldRefName;
	return wxString();
}
const ibBackendQueryable* ibRecordQueryable::ResolveReferenceTarget(const ibBackendQueryColumn* refColumn) const {
	// Resolve a single-target reference COLUMN to the queryable of the object it points
	// at. Works off the column's type (its class id) + this metaobject's metadata — the
	// column need not be a metaobject attribute (an L3 source may be a temp table), only
	// a typed reference column. Null if polymorphic / non-reference / no target queryable.
	if (refColumn == nullptr)
		return nullptr;
	const ibTypeDescription& td = refColumn->GetTypeDesc();
	if (td.GetClsidList().size() != 1)
		return nullptr;
	const ibMetaData* metaData = m_meta->GetMetaData();
	if (metaData == nullptr)
		return nullptr;
	const ibCtorMetaValueType* ctor = metaData->GetTypeCtor(td.GetFirstClsid());
	if (ctor == nullptr || ctor->GetMetaTypeCtor() != ibCtorObjectMetaType::ibCtorObjectMetaType_Reference)
		return nullptr;
	const ibBackendQueryableHolder* holder = dynamic_cast<const ibBackendQueryableHolder*>(ctor->GetMetaObject());
	return holder != nullptr ? holder->GetQueryable() : nullptr;
}
wxString ibRecordQueryable::MaterializeRowKey(ibDatabaseResultSet* rs) const {
	return rs != nullptr ? rs->GetResultString(guidName) : wxString();
}
ibValue ibRecordQueryable::MaterializeAttribute(const ibValueMetaObjectAttributeBase* attr, ibDatabaseResultSet* rs) const {
	if (rs == nullptr)
		return ibValue();
	// The reference attribute yields the row's own reference object.
	if (m_meta->IsDataReference(attr->GetMetaID())) {
		ibGuid guid = rs->GetResultString(guidName);
		return ibValue(ibValueReferenceDataObject::CreateFromResultSet(rs, m_meta, guid));
	}
	ibValue v;
	ibValueMetaObjectAttributeBase::GetValueAttribute(attr, v, rs);
	return v;
}

// registers — no single row-key; composite identity (recorder+line / period?+dims),
// carried as real attributes; the consumer assembles the row identity. No reference.
const ibValueMetaObjectAttributeBase* ibRegisterDataQueryable::ResolveAttribute(const wxString& name) const { return m_meta->FindAnyAttributeObjectByFilter(name); }
const ibValueMetaObjectAttributeBase* ibRegisterDataQueryable::ResolveAttribute(const ibMetaID& id) const { return m_meta->FindAnyAttributeObjectByFilter(id); }
wxString ibRegisterDataQueryable::GetQueryTableName() const { return m_meta->GetTableNameDB(); }
ibMetaID ibRegisterDataQueryable::GetQueryMetaID() const { return m_meta->GetMetaID(); }
wxString ibRegisterDataQueryable::GetRowKeyColumn() const { return wxEmptyString; }
bool ibRegisterDataQueryable::IsReferenceAttribute(const ibMetaID& /*id*/) const { return false; }
std::vector<ibQuerySortItem> ibRegisterDataQueryable::GetIdentitySort() const {
	std::vector<ibQuerySortItem> ret;
	if (m_meta->HasRecorder()) {
		ret.push_back({ m_meta->GetRegisterRecorder(), true });
		ret.push_back({ m_meta->GetRegisterLineNumber(), true });
		return ret;
	}
	if (m_meta->HasPeriod())
		ret.push_back({ m_meta->GetRegisterPeriod(), true });
	for (auto* dim : m_meta->GetGenericDimensionArrayObject())
		ret.push_back({ dim, true });
	return ret;
}
wxString ibRegisterDataQueryable::MaterializeRowKey(ibDatabaseResultSet* /*rs*/) const { return wxEmptyString; }
ibValue ibRegisterDataQueryable::MaterializeAttribute(const ibValueMetaObjectAttributeBase* attr, ibDatabaseResultSet* rs) const {
	ibValue v;
	ibValueMetaObjectAttributeBase::GetValueAttribute(attr, v, rs);
	return v;
}

