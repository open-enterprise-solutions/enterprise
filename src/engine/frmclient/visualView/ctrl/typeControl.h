#ifndef _ATTRIBUTE_CONTROL_H__
#define _ATTRIBUTE_CONTROL_H__

// THE VALUE CHOICE — the desktop's ibTypeControlFactory (frontend/visualView/ctrl/typeControl.h) as a value cell of the
// client's settings windows asks it: settle the type a cell holds, and choose a value of it — a number on its calculator,
// a date on its calendar. A value of a type of the server's (a reference, an enum member) is chosen on the server: the
// next step of the protocol.

#include <vector>

#include "frmclient/frmclient.h"
#include "frmclient/backend/backend_type.h"
#include "frmclient/backend/compiler/value.h"

class FRMCLIENT_API ibMetaData;
class FRMCLIENT_API ibValueMetaObject;
class FRMCLIENT_API ibControlFrame;
class ibBackendSourceColumn;

class FRMCLIENT_API ibTypeControlFactory : public ibBackendTypeSourceFactory {
public:

	virtual const ibBackendSourceColumn* GetSourceAttributeObject() const = 0;

	static bool ChooseValue(ibControlFrame* ownerValue,
		const class ibValueMetaObject* choiceForm = nullptr, wxWindow* parent = nullptr);

	static bool SimpleChoice(ibControlFrame* ownerValue, const ibClassID& clsid, wxWindow* parent);
	static bool QuickChoice(ibControlFrame* ownerValue, const ibClassID& clsid, wxWindow* parent);

	static ibClassID ShowSelectType(const ibMetaData* metadata, const ibTypeDescription& typeDescription);

	virtual ibValue CreateValue() const override;
	virtual ibClassID GetDataType() const;
};

#endif
