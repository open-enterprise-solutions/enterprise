#ifndef _FRMCLIENT_VIEW_BASE_CONTROL_H__
#define _FRMCLIENT_VIEW_BASE_CONTROL_H__

#include "frame.h"

// THE CONTROL — the old desktop's ibValueControl.Its name is the frame's
// (ibValueFrame::GetControlName); its functional options are the server's: what it offers, the frame draws.
class ibValueControl : public ibValueFrame {
public:

	ibValueControl(ibVisualHostClient& host, long long controlId) : ibValueFrame(host, controlId) {}

	// A name another control of its form has is refused — the inspector marks the cell (the desktop's).
	virtual bool OnPropertyChanging(ibProperty* property, const wxVariant& newValue) override;
};

#endif
