#ifndef __UIKIT_H__
#define __UIKIT_H__

// ----------------------------------------------------------------------------
// uikit — the OES custom-drawn UI engine: the full wxUniversal theme engine
// forked with ib prefixes (canon), plus our Luna theme (theme/lunaTheme.cpp).
// wxWidgets below this layer is a cross-platform host only: window handles,
// DCs, raw input.
//
// Umbrella header listing the REVIVED part of the canon only — headers join
// as their control comes alive (fix compile errors, add the .cpp to
// frmclient.vcxproj, add the control to the demo form).
// ----------------------------------------------------------------------------

#include "frmclient/uikit/theme.h"
#include "frmclient/uikit/colourScheme.h"
#include "frmclient/uikit/renderer.h"
#include "frmclient/uikit/inputHandler.h"
#include "frmclient/uikit/inputConsumer.h"
#include "frmclient/uikit/window.h"

#include "frmclient/uikit/ctrl/control.h"
#include "frmclient/uikit/ctrl/anyButton.h"
#include "frmclient/uikit/ctrl/button.h"
#include "frmclient/uikit/ctrl/toggleButton.h"
#include "frmclient/uikit/ctrl/checkBox.h"
#include "frmclient/uikit/ctrl/radioButton.h"
#include "frmclient/uikit/ctrl/staticText.h"
#include "frmclient/uikit/ctrl/staticLine.h"
#include "frmclient/uikit/ctrl/scrollBar.h"
#include "frmclient/uikit/ctrl/gauge.h"
#include "frmclient/uikit/ctrl/textCtrl.h"
#include "frmclient/uikit/ctrl/listBox.h"
#include "frmclient/uikit/ctrl/slider.h"
#include "frmclient/uikit/ctrl/spinButton.h"
#include "frmclient/uikit/ctrl/comboBox.h"
#include "frmclient/uikit/ctrl/choice.h"
#include "frmclient/uikit/ctrl/notebook.h"
#include "frmclient/uikit/ctrl/toolBar.h"
#include "frmclient/uikit/ctrl/bmpButton.h"
#include "frmclient/uikit/ctrl/checkListBox.h"
#include "frmclient/uikit/ctrl/staticBitmap.h"
#include "frmclient/uikit/ctrl/staticBox.h"
#include "frmclient/uikit/ctrl/radioBox.h"
#include "frmclient/uikit/ctrl/treeCtrl.h"

#include "frmclient/uikit/menuItem.h"
#include "frmclient/uikit/menu.h"

// UIKIT-REVIVE pending: statusBar + ibMenuBar (need univ frame),
// dialog, frame, topLevel

#endif
