#ifndef __VALUE_SQL_H__
#define __VALUE_SQL_H__

#include "backend/compiler/value.h"

class BACKEND_API ibValueDatabaseLayer : public ibValue {
	public:

	ibValueDatabaseLayer();
	virtual ~ibValueDatabaseLayer();

	virtual ibValueMethodHelper* GetPMethods() const { // get a reference to the class helper for parsing attribute and method names
		//PrepareNames(); 
		return &m_methodHelper;
	}

	virtual void PrepareNames() const; // this method is automatically called to initialize attribute and method names.

	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);       //function call
	virtual bool CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray);       //procudre call

private:
	static ibValueMethodHelper m_methodHelper;
};

class BACKEND_API ibValuePreparedStatement : public ibValue {
	public:

	ibValuePreparedStatement(class ibPreparedStatement* preparedStatement = nullptr);
	virtual ~ibValuePreparedStatement();

	virtual ibValueMethodHelper* GetPMethods() const { // get a reference to the class helper for parsing attribute and method names
		//PrepareNames(); 
		return &m_methodHelper;
	}

	virtual void PrepareNames() const; // this method is automatically called to initialize attribute and method names.

	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);       //function call
	virtual bool CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray);       //procudre call

private:
	class ibPreparedStatement* m_preparedStatement;
	static ibValueMethodHelper m_methodHelper;
};

class BACKEND_API ibValueResultSet : public ibValue {
	public:

	ibValueResultSet(class ibDatabaseResultSet* resultSet = nullptr);
	virtual ~ibValueResultSet();

	virtual ibValueMethodHelper* GetPMethods() const { // get a reference to the class helper for parsing attribute and method names
		//PrepareNames(); 
		return m_methodHelper;
	}

	virtual void PrepareNames() const; // this method is automatically called to initialize attribute and method names.

	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);        //setting attribute
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);                   //attribute value

	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);       //function call
	virtual bool CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray);       //procudre call

private:
	class ibDatabaseResultSet* m_resultSet;
	ibValueMethodHelper* m_methodHelper;
};

#endif