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
// frontend.vcxproj, add the control to the demo form).
// ----------------------------------------------------------------------------

#include "frontend/uikit/theme.h"
#include "frontend/uikit/colourScheme.h"
#include "frontend/uikit/renderer.h"
#include "frontend/uikit/inputHandler.h"
#include "frontend/uikit/inputConsumer.h"
#include "frontend/uikit/window.h"

#include "frontend/uikit/ctrl/control.h"
#include "frontend/uikit/ctrl/anyButton.h"
#include "frontend/uikit/ctrl/button.h"
#include "frontend/uikit/ctrl/toggleButton.h"
#include "frontend/uikit/ctrl/checkBox.h"
#include "frontend/uikit/ctrl/radioButton.h"
#include "frontend/uikit/ctrl/staticText.h"
#include "frontend/uikit/ctrl/staticLine.h"
#include "frontend/uikit/ctrl/scrollBar.h"
#include "frontend/uikit/ctrl/gauge.h"
#include "frontend/uikit/ctrl/textCtrl.h"
#include "frontend/uikit/ctrl/listBox.h"
#include "frontend/uikit/ctrl/slider.h"
#include "frontend/uikit/ctrl/spinButton.h"
#include "frontend/uikit/ctrl/comboBox.h"
#include "frontend/uikit/ctrl/choice.h"
#include "frontend/uikit/ctrl/notebook.h"
#include "frontend/uikit/ctrl/toolBar.h"
#include "frontend/uikit/ctrl/bmpButton.h"
#include "frontend/uikit/ctrl/checkListBox.h"
#include "frontend/uikit/ctrl/staticBitmap.h"
#include "frontend/uikit/ctrl/staticBox.h"
#include "frontend/uikit/ctrl/radioBox.h"
#include "frontend/uikit/ctrl/treeCtrl.h"

#include "frontend/uikit/menuItem.h"
#include "frontend/uikit/menu.h"

// UIKIT-REVIVE pending: statusBar + ibMenuBar (need univ frame),
// dialog, frame, topLevel

#endif
