#ifndef __FRONTEND_TYPES_H__
#define __FRONTEND_TYPES_H__

// Shared type aliases so signatures stay identical between frontend.dll
// (desktop wx) and wfrontend.dll (web, OES_USE_WEB) — one method body,
// two type resolutions at compile time. Same `ibFrontendWindow` trick
// used for every method that takes a parent-window or sizer parameter
// in Create / OnCreated / Update / Cleanup across `visualView/ctrl/*`.
//
//   Desktop (frontend.dll):  ibFrontendWindow = wxWindow
//                            ibFrontendSizer  = wxSizer
//   Web     (wfrontend.dll): ibFrontendWindow = ibWebWindow
//                            ibFrontendSizer  = ibWebSizer
//
// The return type stays wxObject* on both sides: ibWebWindow inherits
// wxEvtHandler -> wxObject, ibWebSizer inherits wxObject directly, so
// a web control / sizer is a wxObject just like wxWindow / wxSizer are
// on desktop. No separate return alias needed.

#ifdef OES_USE_WEB
class ibWebWindow;
class ibWebSizer;
using ibFrontendWindow = ibWebWindow;
using ibFrontendSizer  = ibWebSizer;
#else
class wxWindow;
class wxSizer;
using ibFrontendWindow = wxWindow;
using ibFrontendSizer  = wxSizer;
#endif

// Base class for `ibVisualHost`. On desktop it's wxPanel: the host is the FACADE that carries
// the form's chrome, and the window that scrolls the form's controls lives INSIDE it
// (ibVisualHost::ibContentWindow). On web it's ibWebWindow (the host IS a node in the session's
// serialisable ibWebWindow tree — the same wrapping is what a web scroll window would slot
// into later). Lets `class ibVisualHost : public ibFrontendHostBase` stay one line instead of
// a #ifdef block.
//
// The ctor signatures differ (wxPanel needs parent/id/pos/size/style,
// ibWebWindow default-constructs), so ibVisualHost's own ctor
// still has a per-build body — the typedef only unifies the "which
// class to inherit from" choice, not the construction semantics.
#ifdef OES_USE_WEB
using ibFrontendHostBase = ibWebWindow;
#else
// Full include, not a forward decl — ibVisualHost inherits from
// ibFrontendHostBase in visualHost.h and the inheritance requires a
// complete class definition, not a mere declaration.
#include <wx/panel.h>
#include <wx/scrolwin.h>
using ibFrontendHostBase = wxPanel;
#endif

// No typedefs for concrete sizer subclasses (BoxSizer / WrapSizer /
// StaticBoxSizer / GridSizer). ibFrontendSizer (base) is enough for
// shared parameter types like `UpdateSizer(ibFrontendSizer*)`. Per-
// subclass Update bodies split the concrete cast via
// #ifdef OES_USE_WEB in ctrl/*sizer.cpp — keeps the header small and
// makes per-site API divergence explicit (e.g. wxStaticBoxSizer's
// title lives on a child wxStaticBox, ibWebStaticBoxSizer holds it
// on the sizer itself).

// Tick-source for idle handlers (ibValueForm::AttachIdleHandler).
// On desktop wxTimer fires inside wxApp's event loop; on web
// ibWebTimer drives a std::thread + QueueEvent so the session worker
// sees wxEVT_TIMER through the shared ProcessPendingEvents path.
// Same Bind+GetEventObject shape on both sides lets OnIdleHandler
// stay one method serving both builds.
#ifdef OES_USE_WEB
class ibWebTimer;
using ibFrontendTimer = ibWebTimer;
#else
class wxTimer;
using ibFrontendTimer = wxTimer;
#endif

#endif
