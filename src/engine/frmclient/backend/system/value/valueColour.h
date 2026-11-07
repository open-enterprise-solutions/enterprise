#ifndef _FRMCLIENT_BACKEND_VALUE_COLOUR_H__
#define _FRMCLIENT_BACKEND_VALUE_COLOUR_H__

#include "frmclient/backend/compiler/value.h"

// A COLOUR AS A VALUE — the engine's (backend/system/value/valueColour.h): what an appearance's parameter keeps, written
// as the engine writes it — its class, and its text (typeConv) — so the server reads it back as its own.
class FRMCLIENT_API ibValueColour : public ibValue {
public:

	wxColour m_colour;

	// …an object, of its kind, as the engine's is made: the default value is EMPTY, which a store writes as nothing chosen
	// (ibStoreValue) — a colour picked for an appearance was dropped as it was kept, its cell read back empty
	ibValueColour() : ibValue(ibValueTypes::TYPE_VALUE) {}
	ibValueColour(const wxColour& colour) : ibValue(ibValueTypes::TYPE_VALUE), m_colour(colour) {}

	virtual ibClassID GetClassType() const override { return value_to_clsid("VL_COLOR"); }
	virtual ibString GetString() const override;
	virtual bool IsEmpty() const override { return !m_colour.IsOk(); }

protected:

	virtual bool DoSerialize(class ibDataNode& node) const override;
	virtual bool DoDeserialize(const class ibDataNode& node) override;
};

#endif
