// Link-time stubs for desktop-only frontend classes that leak into the
// web DLL through transitive header inclusions. Bodies are empty and
// parameters unnamed — the web side never invokes any of these at
// runtime. When a stubbed method starts being used for real on web,
// its definition moves out of here into a proper per-file port.

#include "frontend/mainFrame/objinspect/objinspect.h"
#include "frontend/mainFrame/settings/keybinder.h"
#include "frontend/mainFrame/mainFrame.h"
#include "frontend/win/ctrls/dynamicBorder.h"
#include "frontend/win/dlgs/selectData.h"

// -----------------------------------------------------------------------------
// ibObjectInspector — designer property panel, referenced by inline
// methods pulled through docManager.h -> objinspect.h.
// -----------------------------------------------------------------------------

// Both AddItems overloads are inline in objinspect.h — their bodies
// already exist. Stubs below cover the non-inline members they touch.
void ibObjectInspector::Create(ibPropertyObject*, bool) {}
bool ibObjectInspector::IsShownInspector() const { return false; }
wxPGProperty* ibObjectInspector::GetProperty(ibProperty*) const { return nullptr; }
wxPGProperty* ibObjectInspector::GetEvent(ibEvent*) const { return nullptr; }

// -----------------------------------------------------------------------------
// ibFrontendMainFrame — desktop main frame, touched by doc/view glue.
// s_instance stays null so every null-check path is taken on web.
// -----------------------------------------------------------------------------

ibFrontendMainFrame* ibFrontendMainFrame::s_instance = nullptr;
void ibFrontendMainFrame::UpdateFrameManager() {}

// Web build has its own wxWebFrame / ibWebFrame plumbing; the desktop
// frame's lazy-runtime hook is never reached. Stub keeps the inline
// Show() in mainFrame.h link-clean inside wfrontend.dll.
bool ibFrontendMainFrame::EnsureRuntime() { return true; }

// -----------------------------------------------------------------------------
// ibKeyBinder — key-binding registry for the designer menus.
// -----------------------------------------------------------------------------

ibKeyBinder::~ibKeyBinder() {}

// -----------------------------------------------------------------------------
// ibDynamicStaticText — auto-sizing static text used by typeControl.
// -----------------------------------------------------------------------------

wxSize ibDynamicStaticText::DoGetBestClientSize() const { return wxDefaultSize; }

// -----------------------------------------------------------------------------
// ibDialogSelectDataType — designer's "pick a data type" modal.
// typeControl.cpp's ShowSelectType spawns it; never reached on web.
// -----------------------------------------------------------------------------

ibDialogSelectDataType::ibDialogSelectDataType(const ibMetaData*, const std::vector<ibClassID>&) {}
ibDialogSelectDataType::~ibDialogSelectDataType() {}
bool ibDialogSelectDataType::ShowModal(ibClassID&) { return false; }
void ibDialogSelectDataType::OnListItemSelected(wxListEvent&) {}
