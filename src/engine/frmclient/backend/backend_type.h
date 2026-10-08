#ifndef __BACKEND_TYPE_H__
#define __BACKEND_TYPE_H__

// THE TYPE FACTORIES — the engine's (backend/backend_type.h) as a value cell of the client's settings windows asks them:
// what type the cell holds, the value it makes and adjusts, the format a type is shown through. What the engine asks of
// the configuration — a characteristic's types, the types a filter offers — is the server's.

#include <vector>

#include "frmclient/backend/typeDescription.h"
#include "frmclient/backend/sourceDescription.h"   // ibSourceDescription — control's bound source path (GetSourceDesc)

class FRMCLIENT_API ibMetaData;
class FRMCLIENT_API ibFormatString;
class ibBackendSourceColumn;

class FRMCLIENT_API ibBackendTypeFactory {
public:

	virtual ~ibBackendTypeFactory() = default;

	ibClassID GetFirstClsid() const { return GetTypeDesc().GetFirstClsid(); }
	ibClassID GetByIdx(unsigned int idx) const { return GetTypeDesc().GetByIdx(idx); }
	unsigned int GetClsidCount() const { return GetTypeDesc().GetClsidCount(); }

	virtual ibValue CreateValue() const;
	virtual ibValue AdjustValue() const;
	virtual ibValue AdjustValue(const ibValue& varValue) const;
	virtual ibValue AdjustValue(const ibValue& varValue, const ibTypeDescription& limit) const;

	virtual ibTypeDescription& GetTypeDesc() const = 0;
	// What may be STORED, compared, offered and adjusted — the declared type on the client (a characteristic's types are
	// the configuration's).
	virtual ibTypeDescription& GetTypeValueDesc() const { return GetTypeDesc(); }

	bool IsEmptyTypeDesc() const { return !GetTypeDesc().IsOk(); }
};

enum ibSelectorDataType {
	ibSelectorDataType_any,
	ibSelectorDataType_boolean,
	ibSelectorDataType_reference,
	ibSelectorDataType_table,
	ibSelectorDataType_resource,
	ibSelectorDataType_eventSource,
};

class FRMCLIENT_API ibBackendTypeConfigFactory :
	public ibBackendTypeFactory {
public:

	virtual ibSelectorDataType GetFilterDataType() const {
		return ibSelectorDataType::ibSelectorDataType_reference;
	}

	// The format a type is shown through when nobody set one — the engine's.
	static bool GetFormatFromTypeDesc(const ibTypeDescription& type, class ibFormatString& formatString);

	virtual const class ibMetaData* GetMetaData() const = 0;
};

class FRMCLIENT_API ibBackendTypeSourceFactory :
	public ibBackendTypeConfigFactory {
public:

	virtual class ibSourceObject* GetSourceObject() const = 0;
	virtual ibSourceDescription& GetSourceDesc() const = 0;

	void SetDefaultSourceType(const ibSourceDescription& desc) { GetSourceDesc() = desc; }
	void ClearSourceType() { GetSourceDesc() = ibSourceDescription(); }
};

#endif
