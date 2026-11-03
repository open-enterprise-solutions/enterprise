#ifndef _ATTRIBUTE_CONTROL_H__
#define _ATTRIBUTE_CONTROL_H__

// THE VALUE CHOICE — the old desktop's ibTypeControlFactory as a value cell of the
// client's settings windows asks it: settle the type a cell holds, and choose a value of it — a number on its calculator,
// a date on its calendar. A value of a type of the server's (a reference, an enum member) is chosen on the server: the
// next step of the protocol.

#include <vector>

#include <wx/arrstr.h>

#include "frmclient/frmclient.h"
#include "frmclient/backend/backend_type.h"
#include "frmclient/backend/compiler/value.h"

class FRMCLIENT_API ibMetaData;
class FRMCLIENT_API ibControlFrame;
class ibBackendSourceColumn;

// WHAT A VALUE IS CHOSEN AMONG — the values offered, each said by its caption, the selected one marked: the server's
// quick choice (ibProtocol::ibRequest::ibChoice::Quick), its items standing for their order. None — the value chosen
// says it itself.
struct ibChooseValueRequest {
	std::vector<ibValue> m_values;
	wxArrayString        m_captions;
	int                  m_selected = wxNOT_FOUND;
};

class FRMCLIENT_API ibTypeControlFactory : public ibBackendTypeSourceFactory {
public:

	virtual const ibBackendSourceColumn* GetSourceAttributeObject() const = 0;

	// THE ONE DOOR a value is chosen through — a field's Select, a server's question: the values offered dropped as a list;
	// none offered — the type settled, then a primitive chosen on its calculator, its calendar or its list, a value of the
	// server's type on the server. The one picked goes to ownerValue->ChoiceProcessing.
	static bool ChooseValue(ibControlFrame* ownerValue, const ibChooseValueRequest& request = {}, wxWindow* parent = nullptr);

	static ibClassID ShowSelectType(const ibMetaData* metadata, const ibTypeDescription& typeDescription);

	virtual ibValue CreateValue() const override;
	virtual ibClassID GetDataType() const;

private:

	static bool SimpleChoice(ibControlFrame* ownerValue, const ibClassID& clsid, wxWindow* parent);
	// False: nothing to offer.
	static bool QuickChoice(ibControlFrame* ownerValue, const ibChooseValueRequest& request, wxWindow* parent);
};

#endif
