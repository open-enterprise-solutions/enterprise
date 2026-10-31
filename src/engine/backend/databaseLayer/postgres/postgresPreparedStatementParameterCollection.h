#ifndef __POSTGRESQL_PREPARED_STATEMENT_PARAMETER_COLLECTION_H__
#define __POSTGRESQL_PREPARED_STATEMENT_PARAMETER_COLLECTION_H__

#include <vector>

#include "postgresParameter.h"

typedef std::vector<ibDatabaseParameterPostgres> ArrayOfPostgresParameters;

class ibPreparedStatementPostgresParameterCollection
{
public:
	// dtor
	virtual ~ibPreparedStatementPostgresParameterCollection();

	int GetSize();
	char** GetParamValues();
	int* GetParamLengths();
	int* GetParamFormats();

	void SetParam(int nPosition, int nValue);
	void SetParam(int nPosition, double dblValue);
	void SetParam(int nPosition, const ibNumber& strValue);
	void SetParam(int nPosition, const ibString& strValue);
	void SetParam(int nPosition);
	void SetParam(int nPosition, const void* pData, long nDataLength);
	void SetParam(int nPosition, const ibDateTimeParts& date);
	void SetParam(int nPosition, bool bValue);
	void SetParam(int nPosition, ibDatabaseParameterPostgres& Parameter);

private:
	ArrayOfPostgresParameters m_Parameters;
};

#endif // __POSTGRESQL_PREPARED_STATEMENT_PARAMETER_COLLECTION_H__

