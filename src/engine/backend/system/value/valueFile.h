#ifndef __VALUE_FILE_H__
#define __VALUE_FILE_H__

#include "backend/compiler/value.h"

void ibValueFile_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);

class BACKEND_API ibValueFile : public ibValueStaticMembers<&ibValueFile_BindNames> {
	public:
private:

	enum Prop {
		enBaseName,
		enExtension,
		enFullName,
		enName,
		enPath,
	};

	enum Func {
		enExist,
		//enGetHidden,
		enGetModificationTime,
		enGetReadOnly,
		enIsDirectory,
		enIsFile,
		//enSetHidden,
		//enSetModificationTime,
		//enSetReadOnly,
		enSize,
	};

public:

	// DoGetPMethods (protected) + Shared<&ibValueFile_BindNames> come from the base.
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);//method call

	virtual bool SetPropVal(const long lPropNum, const ibValue &varValue);//setting attribute
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);//attribute value

	ibValueFile();
	virtual ~ibValueFile();

	virtual bool IsEmpty() const { return false; }

	virtual bool Init() { return false; }
	virtual bool Init(ibValue **paParams, const long lSizeArray);

private:
	wxString m_fileName;
};

#endif 