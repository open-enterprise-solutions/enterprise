#ifndef _FRMCLIENT_BACKEND_VALUE_SIZE_H__
#define _FRMCLIENT_BACKEND_VALUE_SIZE_H__

#include "frmclient/backend/compiler/value.h"

// A SIZE AS A VALUE — the engine's ibValueSize (backend/system/value/valueSize.h): the size it holds. A script's value
// of one is the server's.
class FRMCLIENT_API ibValueSize : public ibValue {
public:

	wxSize m_size;

	ibValueSize() = default;
	ibValueSize(const wxSize& size) : m_size(size) {}
};

#endif
