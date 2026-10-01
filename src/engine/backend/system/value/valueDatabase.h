#ifndef __VALUE_SQL_H__
#define __VALUE_SQL_H__

#include "backend/compiler/value.h"

void ibValueDatabaseLayer_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);

class BACKEND_API ibValueDatabaseLayer : public ibValueStaticMembers<&ibValueDatabaseLayer_BindNames> {
	public:

	ibValueDatabaseLayer();
	virtual ~ibValueDatabaseLayer();

	// DoGetPMethods (protected) + Shared<&ibValueDatabaseLayer_BindNames> come from the base.
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);       //function call
	virtual bool CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray);       //procudre call

};

void ibValuePreparedStatement_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);

class BACKEND_API ibValuePreparedStatement : public ibValueStaticMembers<&ibValuePreparedStatement_BindNames> {
	public:

	ibValuePreparedStatement(class ibPreparedStatement* preparedStatement = nullptr);
	virtual ~ibValuePreparedStatement();

	// DoGetPMethods (protected) + Shared<&ibValuePreparedStatement_BindNames> come from the base.
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);       //function call
	virtual bool CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray);       //procudre call

private:
	class ibPreparedStatement* m_preparedStatement;
};

class BACKEND_API ibValueResultSet : public ibValueDynamicMembers {
	public:

	ibValueResultSet(class ibDatabaseResultSet* resultSet = nullptr);
	virtual ~ibValueResultSet();

	void FillMembers(ibMemberTable& helper) const;   // bound in ctor (was PrepareNames)

	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);        //setting attribute
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);                   //attribute value

	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);       //function call
	virtual bool CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray);       //procudre call

private:
	class ibDatabaseResultSet* m_resultSet;
};

#endif