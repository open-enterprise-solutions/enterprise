#ifndef _FRMCLIENT_BACKEND_VALUE_SIZE_H__
#define _FRMCLIENT_BACKEND_VALUE_SIZE_H__

#include "frmclient/backend/compiler/value.h"

// A SIZE AS A VALUE — the engine's ibValueSize (backend/system/value/valueSize.h): the size it holds. A script's value
// of one is the server's.
class FRMCLIENT_API ibValueSize : public ibValue {
public:

	wxSize m_size;

	// …an object, of its kind, as the engine's is made — see ibValueColour
	ibValueSize() : ibValue(ibValueTypes::TYPE_VALUE) {}
	ibValueSize(const wxSize& size) : ibValue(ibValueTypes::TYPE_VALUE), m_size(size) {}
};

#endif
