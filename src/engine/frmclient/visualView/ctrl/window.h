#ifndef _FRMCLIENT_VIEW_WINDOW_BASE_H__
#define _FRMCLIENT_VIEW_WINDOW_BASE_H__

#include "control.h"

// A CONTROL THAT IS A WINDOW — the old desktop's ibValueWindow:its sizes, its look,
// its tooltip, whether it is enabled and shown are drawn from the frame (UpdateWindow, ApplyLook); its properties are
// the server's (ibValueFrame).
class ibValueWindow : public ibValueControl {
public:

	ibValueWindow(ibVisualHostClient& host, long long controlId) : ibValueControl(host, controlId) {}

	virtual int GetComponentType() const override { return COMPONENT_TYPE_WINDOW; }
};

#endif
