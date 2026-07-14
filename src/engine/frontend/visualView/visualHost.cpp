////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxFormBuilder
//	Description : visual editor
////////////////////////////////////////////////////////////////////////////

#include "visualHost.h"
#include "ctrl/window.h"                          // ibValueWindowComposite::BuildLayerParts
#ifdef OES_USE_WEB

#include "ctrl/frame.h"
#include "ctrl/form.h"
#include "ctrl/sizer.h"
#include "ctrl/window.h"
#include "formdefs.h"
#include "frontend/web/webSizer.h"

namespace {

// Recursive walker — stays inside this TU. Parent is passed as the
// typed pair (parentWindow, parentSizer) with exactly one non-null —
// lets the branches use static_cast on the created object since
// ibValueFrame::GetComponentType already discriminates the subtype
// (COMPONENT_TYPE_FRAME / _WINDOW → ibWebWindow,
// COMPONENT_TYPE_SIZER → ibWebSizer, _SIZERITEM transparent).
//
// Controls that haven't been ported fall through (GetComponentType
// says one thing but Create returns ibNoObject) — the null-check on
// `created` catches that; static_cast below only runs when it's safe.
void AppendChildControls(ibValueFrame* node,
	ibWebWindow* parentWindow, ibWebSizer* parentSizer,
	ibVisualHost* host, const ibWebSizer::AddParams* pendingParams)
{
	if (node == nullptr || (parentWindow == nullptr && parentSizer == nullptr))
		return;
	for (unsigned int i = 0; i < node->GetChildCount(); ++i) {
		ibValueFrame* child = dynamic_cast<ibValueFrame*>(node->GetChild(i));
		if (child == nullptr)
			continue;

		const int componentType = child->GetComponentType();

		// SIZERITEM: transparent carrier of Add params (proportion /
		// flag / border / stretch). Captured and forwarded into the
		// grandchild's Add/SetSizer. No visual node created.
		if (componentType == COMPONENT_TYPE_SIZERITEM) {
			auto* sizerItem = static_cast<ibValueSizerItem*>(child);
			ibWebSizer::AddParams params;
			params.proportion = sizerItem->GetProportion();
			params.flag       = static_cast<int>(sizerItem->GetFlagBorder())
			                   | static_cast<int>(sizerItem->GetFlagState());
			params.border     = sizerItem->GetBorder();
			AppendChildControls(child, parentWindow, parentSizer, host, &params);
			continue;
		}

		// Create() expects ibFrontendWindow* (= ibWebWindow* on web).
		// If we're sitting inside a sizer, walk up its owner chain
		// to the nearest window (sizers don't own widgets; they
		// belong to a window via SetSizer).
		ibWebWindow* createParent = parentWindow;
		if (createParent == nullptr) {
			for (ibWebSizer* s = parentSizer; s != nullptr; ) {
				wxObject* owner = s->GetOwner();
				if (auto* w = dynamic_cast<ibWebWindow*>(owner)) {
					createParent = w;
					break;
				}
				s = dynamic_cast<ibWebSizer*>(owner);
			}
		}

		// Ensure the ibValueFrame owns a non-zero controlId BEFORE the
		// web shim is built. Otherwise ibWebWindow's own auto-id pool
		// (starting at 1'000'000) steps in, the browser sees that id in
		// node.id, but form->FindControlByID searches ibValueFrame ids
		// only — the click POST /action/<id> lookup misses and commands
		// silently no-op. Metadata-saved ids keep their values; only
		// controls that loaded with m_controlId == 0 get freshly minted.
		if (child->GetControlID() == 0 && child->GetOwnerForm() != nullptr)
			child->GenerateNewID();
		wxObject* created = child->Create(createParent, host);

		// ibNoObject is the "empty" sentinel ibValueFrame::Create
		// returns when a subclass isn't ported to web (base returns
		// `new ibNoObject`). It's non-null but not an ibWebWindow /
		// ibWebSizer — static_cast below would be UB. One dynamic_cast
		// here is the safety filter; the rest of the path is then
		// invariant-safe and uses static_cast.
		if (created == nullptr || dynamic_cast<ibNoObject*>(created) != nullptr) {
			AppendChildControls(child, parentWindow, parentSizer, host, nullptr);
			continue;
		}

		ibWebWindow* nextWindow = parentWindow;
		ibWebSizer*  nextSizer  = parentSizer;

		switch (componentType) {
		case COMPONENT_TYPE_FRAME:
		case COMPONENT_TYPE_WINDOW:
		// COMPONENT_TYPE_ABSTRACT components (toolbar items /
		// separators) don't own a wx widget on desktop — their
		// OnCreated uses the parent toolbar's AddTool directly —
		// but on web we do render them as children of the parent
		// container (ibWebToolbar). Their Create override returns
		// an ibWebWindow-derived render shim.
		case COMPONENT_TYPE_ABSTRACT: {
			// Component type says it's a window; Create for this type
			// returns an ibWebWindow subclass on web by construction.
			// The controlId is set inside Create (the ibValueXxx knows
			// its own GetControlID), so the walker doesn't stamp it.
			ibWebWindow* win = static_cast<ibWebWindow*>(created);
			if (parentWindow != nullptr)
				win->SetParent(parentWindow);
			else if (parentSizer != nullptr)
				parentSizer->Add(win,
					pendingParams ? *pendingParams : ibWebSizer::AddParams{});
			host->AppendInnerControl(child, win);   // windows only
			nextWindow = win;
			nextSizer  = nullptr;
			break;
		}
		case COMPONENT_TYPE_SIZER: {
			ibWebSizer* siz = static_cast<ibWebSizer*>(created);
			if (parentWindow != nullptr)
				parentWindow->SetSizer(siz);
			else if (parentSizer != nullptr)
				parentSizer->Add(siz,
					pendingParams ? *pendingParams : ibWebSizer::AddParams{});
			nextWindow = nullptr;
			nextSizer  = siz;
			break;
		}
		default:
			// Unknown / unported component type — no web node to
			// attach. Descend with the parent we already had so any
			// future transparent subtypes still get walked.
			break;
		}

		// Self-Update: push property values into the raw shim Create
		// produced. Runs BEFORE recursing into children, so children
		// see the parent's state already applied.
		child->Update(created, host);

		// Pending params consumed by the first real Add/SetSizer
		// above; clear for recursive descent.
		AppendChildControls(child, nextWindow, nextSizer, host, nullptr);

		// OnCreated / OnUpdated fire AFTER children have fully run
		// their own Create + Update passes — matches desktop's
		// GenerateControl order (post-order OnCreated), so parent
		// hooks can assume their children are wired up.
		child->OnCreated(created, createParent, host, /*firstCreated*/ true);
		child->OnUpdated(created, createParent, host);
	}
}

} // namespace

bool ibVisualHost::CreateVisualHost()
{
	ibValueForm* form = GetValueForm();
	if (form == nullptr)
		return false;
	// Seed a default root ibWebBoxSizer(wxVERTICAL) on the host if the
	// form hasn't already placed a top-level sizer — mirrors desktop's
	// GetBackgroundWindow()->SetSizer(new wxBoxSizer(wxVERTICAL)). Keeps
	// SetOrientation target-addressable: the host always has a BoxSizer
	// to mutate, even for forms whose root child is a WINDOW. Walker is
	// still invoked with parentWindow = this (the host), so top-level
	// WINDOW children keep going through SetParent as before; a top-
	// level SIZER child still replaces this seeded one via the walker's
	// `parentWindow->SetSizer(siz)` branch. Net effect: SetOrientation
	// has something to grab onto on a bare form, and nothing else about
	// the existing layout flow changes.
	if (GetSizer() == nullptr)
		SetSizer(new ibWebBoxSizer(wxVERTICAL));
	// Apply the form's declared orientation to the root sizer —
	// mirrors desktop CreateVisualHost which does the same right
	// after SetCaption. Forms designed with horizontal root layout
	// (wxHORIZONTAL) then stack children across, not down.
	SetOrientation(form->GetOrient());
	// Pass parentSizer = root (parentWindow = nullptr) so top-level
	// controls get Added to the root sizer, the way desktop feeds
	// them through GetFrameSizer(). Before this, a WINDOW child
	// preferred SetParent(host) and ended up as a bare m_children
	// entry — rendered as a stacked block-level div ignoring the
	// sizer's orientation. A top-level SIZER child still replaces
	// the root via the walker's `parentSizer->Add(siz)` branch, and
	// nested controls follow the same pattern as before.
	auto* rootSizer = static_cast<ibWebSizer*>(GetSizer());
	AppendChildControls(form, nullptr, rootSizer, this, nullptr);
	return true;
}

namespace {
// Walk the existing ibValueFrame tree, call Update + OnUpdated on
// each node that has a matching web shim. Mirrors the desktop
// RefreshControl pass (per-element property refresh without tearing
// down the tree). Skips ibValueFrames with no entry in the host's
// map — those are structural elements (SizerItem) or unported types.
void UpdateChildControls(ibValueFrame* node, ibVisualHost* host,
	ibWebWindow* parentWindow)
{
	if (node == nullptr) return;
	for (unsigned int i = 0; i < node->GetChildCount(); ++i) {
		ibValueFrame* child = dynamic_cast<ibValueFrame*>(node->GetChild(i));
		if (child == nullptr) continue;

		wxObject* obj = host->GetWxObject(child);
		ibWebWindow* childWin = dynamic_cast<ibWebWindow*>(obj);

		// Self Update FIRST (pushes fresh properties), children
		// recurse next, then OnUpdated fires post-order.
		if (obj != nullptr)
			child->Update(obj, host);

		UpdateChildControls(child, host,
			childWin != nullptr ? childWin : parentWindow);

		if (obj != nullptr)
			child->OnUpdated(obj, parentWindow, host);
	}
}
} // namespace

bool ibVisualHost::UpdateVisualHost()
{
	// Refresh pass: walk the tree, invoke Update / OnUpdated on each
	// live web node. Web shims that want to push fresh property values
	// into their node override Update (textctrl value, tool caption,
	// checkbox state, …); no-op implementations leave the node alone,
	// and the stateless rebuild (ClearVisualHost + CreateVisualHost)
	// remains available for structural changes that can't be done
	// in place.
	ibValueForm* form = GetValueForm();
	if (form == nullptr)
		return false;
	UpdateChildControls(form, this, this);
	return true;
}

namespace {
// Post-order Cleanup pass: children first, then parent — so a control
// whose Cleanup assumes its children are still reachable sees them
// before they're wiped. Mirrors desktop ibVisualHost::ClearControl.
void CleanupChildControls(ibValueFrame* node, ibVisualHost* host)
{
	if (node == nullptr) return;
	for (unsigned int i = 0; i < node->GetChildCount(); ++i) {
		ibValueFrame* child = dynamic_cast<ibValueFrame*>(node->GetChild(i));
		if (child == nullptr) continue;
		CleanupChildControls(child, host);
		if (wxObject* obj = host->GetWxObject(child))
			child->Cleanup(obj, host);
	}
}
} // namespace

bool ibVisualHost::ClearVisualHost()
{
	// Walker allocates nodes with raw `new` and parents them under the
	// host — we own them and must delete here before rebuilding,
	// otherwise each CreateAndUpdate would leak the previous pass's
	// tree. Before deleting, fire each control's Cleanup so custom
	// controls see the "about to be removed" hook symmetrical to
	// Create. Three slots to clear: the ibWebWindow children (direct
	// siblings), the optional ibWebSizer set via SetSizer, and the
	// (ibValueFrame → wxObject) index populated by the walker.
	if (ibValueForm* form = GetValueForm())
		CleanupChildControls(form, this);
	m_baseObjects.clear();      // wipe after Cleanup so callbacks can still look up siblings
	while (!GetChildren().empty()) {
		ibWebWindow* c = GetChildren().back();
		c->SetParent(nullptr);  // detaches from our m_children
		delete c;
	}
	SetSizer(nullptr);          // deletes the previous pass's sizer tree
	return true;
}

ibValueFrame* ibVisualHost::GetObjectBase(const wxObject* wxobject) const
{
	// Reverse index — iterate m_baseObjects; only desktop has call
	// sites today. Kept defined on web so the virtual's symbol
	// exists for anything that links against both.
	for (const auto& kv : m_baseObjects) {
		if (kv.second == wxobject)
			return kv.first;
	}
	return nullptr;
}

wxObject* ibVisualHost::GetWxObject(const ibValueFrame* baseobject) const
{
	auto it = m_baseObjects.find(const_cast<ibValueFrame*>(baseobject));
	return it != m_baseObjects.end() ? it->second : nullptr;
}

// Incremental lifecycle hooks — no-ops on web. The script-side caller
// mutated the ibValueFrame tree; the next HTTP response's
// ClearVisualHost + CreateVisualHost picks up the change. Defined so
// shared caller code (ibValueForm::CreateControl / RemoveControl,
// ibValueModelTableBox) doesn't need #ifdef guards.
void ibVisualHost::CreateControl(ibValueFrame*, ibValueFrame*, bool) {}
void ibVisualHost::UpdateControl(ibValueFrame*, ibValueFrame*)       {}
void ibVisualHost::RemoveControl(ibValueFrame*, ibValueFrame*)       {}
void ibVisualHost::ClearControl(ibValueFrame*, bool)                 {}

#else  // !OES_USE_WEB
// Everything below is the desktop (wxScrolledCanvas-based) implementation
// of ibVisualHost. The web build reuses the class name but with a
// stripped-down ibWebWindow-based declaration (see visualHost.h), so
// none of the wx-specific implementation applies there. Rather than
// rewrite each method as a #ifdef sandwich, the whole desktop engine
// of the host stays here and is excluded from wfrontend.dll.

#include "ctrl/control.h"
#include "ctrl/form.h"
#include "frontend/win/ctrls/toolBar.h"        // ibAuiToolBar (host-rendered command bar)
#include "frontend/win/theme/luna_toolbarart.h"
#include "backend/backend_picture.h"           // ibBackendPicture / ibPictureDescription

#include "pageWindow.h"

#include <wx/collpane.h>
#include <wx/wupdlock.h>   // wxWindowUpdateLocker — RAII Freeze/Thaw

wxIMPLEMENT_ABSTRACT_CLASS(ibVisualHost, wxScrolledWindow)

/////////////////////////////////////////////////////////////////////////////////////////////////////

// The form's LAYERS — a command-bar toolbar today, a search row / status bar later —
// stacked at the TOP of the form sizer, ABOVE the form's content. It follows the control
// lifecycle — CREATED once (CreateVisualHost), REFRESHED in place on update (UpdateVisualHost),
// torn down with the host (DestroyChildren). Both stages call the SAME ibValueWindowComposite
// statics; only WHERE the layers are placed is host-specific (named children of the bg window).
static void CreateFormLayers(wxWindow* bgWindow, wxSizer* mainSizer, ibValueForm* form, std::vector<ibFrontendWindow*>& outParts)
{
	outParts.clear();
	if (bgWindow == nullptr || mainSizer == nullptr || form == nullptr)
		return;
	ibValueCommandBar* cbar = form->GetCommandBar();
	if (cbar == nullptr)
		return;
	// Layers (toolbar, search, …) are parallel items at the TOP of the main sizer, in order,
	// ahead of the content sub-sizer (already at the end). Remember them EXPLICITLY (outParts)
	// so the update refreshes the SAME parts without re-deriving them from the sizer.
	size_t pos = 0;
	for (ibFrontendWindow* part : ibValueWindowComposite::BuildLayerParts(bgWindow, cbar, form)) {
		mainSizer->Insert(pos++, part, 0, wxEXPAND);
		outParts.push_back(part);
	}
	bgWindow->Layout();
}

static void UpdateFormLayers(ibValueForm* form, const std::vector<ibFrontendWindow*>& parts)
{
	if (form == nullptr)
		return;
	ibValueCommandBar* cbar = form->GetCommandBar();
	if (cbar == nullptr)
		return;
	// Refresh the tracked parts in place — the SAME static the composite uses, over the parts
	// CreateFormLayers remembered (no sizer walk).
	ibValueWindowComposite::UpdateLayerParts(parts, cbar, form);
}

void ibVisualHost::InitMainSizer()
{
	m_mainSizer = new wxBoxSizer(wxVERTICAL);
	GetBackgroundWindow()->SetSizer(m_mainSizer);
}

void ibVisualHost::ApplyContentOrientation(int orient)
{
	if (wxBoxSizer* content = dynamic_cast<wxBoxSizer*>(GetFrameSizer()))
		content->SetOrientation(orient);
}

bool ibVisualHost::CreateVisualHost()
{
	const ibValueForm* valueForm = GetValueForm();

	wxWindowUpdateLocker freeze(GetParentBackgroundWindow());

	if (valueForm != nullptr && IsShownHost()) {

		// --- [1] MAIN sizer — the ctor-owned attribute m_mainSizer. It holds
		// PARALLEL layers: the chrome layers (toolbar, search, …) at the top and a
		// CONTENT sub-sizer (the "children layer") below. Children never go into
		// the main sizer directly — they fill the content sub-sizer, so their
		// index-based inserts never fight the toolbar layer. Clear it for a fresh
		// build; recreate only defensively (a concrete host always makes one).
		if (m_mainSizer == nullptr)
			InitMainSizer();
		else
			m_mainSizer->Clear(false);   // drop prior layers / content sub-sizer, keep the sizer

		// Content sub-sizer — GetFrameSizer() targets THIS from here on, so every
		// child-attach path (create / refresh / runtime-add) lands under the
		// chrome. The chrome layers are Inserted ahead of it in [6].
		wxBoxSizer* contentSizer = new wxBoxSizer(wxVERTICAL);
		m_mainSizer->Add(contentSizer, 1, wxEXPAND);
		m_frameContentSizer = contentSizer;

		// --- [2] Show form
		GetParentBackgroundWindow()->Show(true);

		// --- [3] Set the color of the form -------------------------------
		GetBackgroundWindow()->SetForegroundColour(valueForm->GetForegroundColour());
		GetBackgroundWindow()->SetBackgroundColour(valueForm->GetBackgroundColour());

		// --- [4] Title bar Setup
		SetCaption(valueForm->GetCaption());

		// --- [5] Default sizer Setup — orientation drives the CONTENT sub-sizer
		// (GetFrameSizer); the main sizer stays vertical so the chrome layers
		// always sit above the content.
		SetOrientation(valueForm->GetOrient());

		// --- [6] Chrome layers on the MAIN sizer, inserted as parallel items
		// AHEAD of the content sub-sizer, in order (toolbar, search, …). Same
		// shared BuildLayerParts the composite controls use.
		CreateFormLayers(GetBackgroundWindow(), m_mainSizer, GetValueForm(), m_formLayerParts);

		// --- [7] Create the content components of the form, INTO the content
		// sub-sizer (GetFrameSizer), under the chrome.
		// Used to save valueForm objects for later display
		for (unsigned int i = 0; i < valueForm->GetChildCount(); i++) {
			ibValueFrame* child = valueForm->GetChild(i);
			// Recursively generate the ObjectTree
			try {
				// we have to put the content valueForm panel as parentObject in order
				// to SetSizeHints be called.
				GenerateControl(child, GetBackgroundWindow(), GetFrameSizer());
			}
			catch (std::exception& ex) {
				wxLogError(ex.what());
			}
		}
	}
	else {
		// There is no form to display
		GetParentBackgroundWindow()->Show(false);
	}

#ifdef __WXOSX__
	GetParentBackgroundWindow()->Refresh();
	GetParentBackgroundWindow()->Update();
#endif

	return true;
}

bool ibVisualHost::UpdateVisualHost()
{
	const ibValueForm* valueForm = GetValueForm();

	
	wxWindowUpdateLocker freeze(GetParentBackgroundWindow());
	
	if (valueForm != nullptr && IsShownHost()) {

		// --- [1] Show form 
		GetParentBackgroundWindow()->Show(true);

		// --- [2] Set the color of the form -------------------------------
		GetBackgroundWindow()->SetForegroundColour(valueForm->GetForegroundColour());
		GetBackgroundWindow()->SetBackgroundColour(valueForm->GetBackgroundColour());

		// --- [3] Title bar Setup
		SetCaption(valueForm->GetCaption());

		// --- [4] Default sizer Setup 
		SetOrientation(valueForm->GetOrient());

		// The form's command bar was CREATED in CreateVisualHost — here we only REFRESH the
		// existing bar in place (no rebuild), matching the control update lifecycle.
		UpdateFormLayers(GetValueForm(), m_formLayerParts);

		// --- [5] Update the components of the form -------------------------
		// Used to save valueForm objects for later display
		for (unsigned int i = 0; i < valueForm->GetChildCount(); i++) {

			ibValueFrame* child = valueForm->GetChild(i);

			// Recursively generate the ObjectTree
			try {
				// we have to put the content valueForm panel as parentObject in order
				// to SetSizeHints be called.
				RefreshControl(child, GetBackgroundWindow(), GetFrameSizer());
			}
			catch (std::exception& ex) {
				wxLogError(ex.what());
			}
		}

		// --- [6] Calculate label size
		CalculateLabelSize();

		// --- [7] Set sizer properties
		UpdateHostSize();
	}
	else {
		// There is no form to display
		GetParentBackgroundWindow()->Show(false);
		GetParentBackgroundWindow()->Refresh();
	}

	UpdateVirtualSize();

#ifdef __WXOSX__
	GetParentBackgroundWindow()->Refresh();
	GetParentBackgroundWindow()->Update();
#endif

		return true;
}

bool ibVisualHost::ClearVisualHost()
{
	wxWindowUpdateLocker freeze(GetParentBackgroundWindow());

	ibValueForm* valueForm = GetValueForm();
	if (valueForm != nullptr) ClearControl(valueForm, true);

	// The form's command bar (chrome) is a child of the background window — DestroyChildren
	// tears it down here; no separate command-bar cleanup needed.
	GetParentBackgroundWindow()->DestroyChildren();
	GetParentBackgroundWindow()->SetSizer(nullptr); // *!*
	// Drop the cached sizer attributes; CreateVisualHost recovers m_mainSizer
	// from the window (or recreates it) and rebuilds the content sub-sizer.
	m_mainSizer = nullptr;
	m_frameContentSizer = nullptr;
	m_formLayerParts.clear();   // parts died with DestroyChildren; CreateFormLayers refills

	return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////

ibValueFrame* ibVisualHost::GetObjectBase(const wxObject* wxobject) const
{
	if (nullptr == wxobject) {
		wxLogError(wxT("wxObject was nullptr!"));
		return nullptr;
	}

	for (auto& pair : m_baseObjects) {
		if (pair.second == wxobject)
			return pair.first;
	}

	wxLogError(wxT("No corresponding ibValueFrame for wxObject. Name: %s"), wxobject->GetClassInfo()->GetClassName());
	return nullptr;
}

wxObject* ibVisualHost::GetWxObject(const ibValueFrame* baseobject) const
{
	if (baseobject == nullptr) {
		wxLogError(wxT("baseobject was nullptr!"));
		return nullptr;
	}

	if (baseobject->GetComponentType() == COMPONENT_TYPE_FRAME)
		return GetFrameSizer();

	// The map is keyed by ibValueFrame* — use find() instead of the linear
	// scan this used to do. O(1) per call adds up on forms with hundreds
	// of controls since every CreateControl/UpdateControl walks the tree
	// and looks up each parent.
	const auto it = m_baseObjects.find(const_cast<ibValueFrame*>(baseobject));
	if (it != m_baseObjects.end())
		return it->second;

	wxLogError(wxT("No corresponding wxObject for ibValueFrame. Name: %s"), baseobject->GetClassName().c_str());
	return nullptr;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////

void ibVisualHost::GenerateControl(ibValueFrame* obj, wxWindow* wxparent, wxObject* parent, bool firstCreated)
{
	wxObject* createdObject = Create(obj, wxparent);
	wxWindow* createdWindow = nullptr;

	switch (obj->GetComponentType())
	{
	case COMPONENT_TYPE_WINDOW:
		// Component-type + classname discriminators already narrowed
		// the type at compile time — Create for a COMPONENT_TYPE_WINDOW
		// always returns a wxWindow-derived object, and the NotebookPage
		// class specifically returns ibPanelPage. static_cast is safe.
		if (obj->GetClassName() == wxT("NotebookPage"))
			createdWindow = static_cast<ibPanelPage*>(createdObject);
		else
			createdWindow = static_cast<wxWindow*>(createdObject);
		break;

	case COMPONENT_TYPE_SIZER:
	case COMPONENT_TYPE_SIZERITEM:
		if (obj->GetClassName() == wxT("Staticboxsizer")) {
			wxStaticBoxSizer* s = static_cast<wxStaticBoxSizer*>(createdObject);
			createdWindow = s->GetStaticBox();
		}
		break;

	default: break;
	}

	AppendInnerControl(obj, createdObject);

	// Collapsible panes expose their inner pane as the real parent for
	// children; follow that redirection before recursing.
	if (wxCollapsiblePane* collpane = wxDynamicCast(createdObject, wxCollapsiblePane)) {
		createdWindow = collpane->GetPane();
		createdObject = createdWindow;
	}

	wxWindow* childParent = createdWindow != nullptr ? createdWindow : wxparent;
	for (unsigned int i = 0; i < obj->GetChildCount(); ++i)
		GenerateControl(obj->GetChild(i), childParent, createdObject, firstCreated);

	OnCreated(obj, createdObject, wxparent, firstCreated);
}

void ibVisualHost::RefreshControl(ibValueFrame* obj, wxWindow* wxparent, wxObject* parent)
{
	wxObject* createdObject = GetWxObject(obj);
	wxWindow* createdWindow = nullptr;
	wxSizer*  createdSizer  = nullptr;

	switch (obj->GetComponentType())
	{
	case COMPONENT_TYPE_WINDOW:
		createdWindow = static_cast<wxWindow*>(createdObject);
		break;

	case COMPONENT_TYPE_SIZER:
	case COMPONENT_TYPE_SIZERITEM:
		if (obj->GetClassName() == wxT("Staticboxsizer")) {
			wxStaticBoxSizer* s = static_cast<wxStaticBoxSizer*>(createdObject);
			createdWindow = s->GetStaticBox();
			createdSizer  = s;
		}
		else {
			createdSizer = static_cast<wxSizer*>(createdObject);
		}
		break;

	default: break;
	}

	Update(obj, createdObject);

	if (wxCollapsiblePane* collpane = wxDynamicCast(createdObject, wxCollapsiblePane)) {
		createdWindow = collpane->GetPane();
		createdObject = createdWindow;
	}

	wxWindow* childParent = createdWindow != nullptr ? createdWindow : wxparent;
	for (unsigned int i = 0; i < obj->GetChildCount(); ++i)
		RefreshControl(obj->GetChild(i), childParent, createdObject);

	OnUpdated(obj, createdObject, wxparent);

	// If we just produced a sizer sitting directly under a window, attach
	// it as that window's layout sizer and run the initial layout pass.
	const bool parentIsWindow = wxDynamicCast(parent, wxWindow) != nullptr;
	if (createdSizer != nullptr && (parentIsWindow || parent == nullptr)) {
		wxparent->SetSizer(createdSizer);
		if (parent != nullptr)
			createdSizer->SetSizeHints(wxparent);
		wxparent->SetAutoLayout(true);
		wxparent->Layout();
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////////////

namespace {

// If objParent is a wxStaticBoxSizer, trigger a layout pass on it so the
// newly inserted / updated / removed child is repositioned within the
// box. Shared by Create/Update/RemoveControl.
inline void RelayoutStaticBoxIfAny(ibValueFrame* objParent, wxObject* parentObj)
{
	if (objParent != nullptr && objParent->GetClassName() == wxT("Staticboxsizer")) {
		if (wxStaticBoxSizer* s = dynamic_cast<wxStaticBoxSizer*>(parentObj))
			s->Layout();
	}
}

} // namespace

ibVisualHost::ControlContext ibVisualHost::ResolveControlContext(
	ibValueFrame* obj, ibValueFrame* parent, bool resolveWindow) const
{
	ControlContext ctx{};
	ctx.objControl = obj;
	ctx.objParent  = parent != nullptr ? parent : obj->GetParent();

	// Unwrap a SIZERITEM wrapper so we operate on the actual parent sizer.
	if (ctx.objParent != nullptr &&
		ctx.objParent->GetComponentType() == COMPONENT_TYPE_SIZERITEM)
	{
		ctx.objControl = ctx.objParent;
		ctx.parentObj  = GetWxObject(ctx.objParent->GetParent());
		ctx.objParent  = ctx.objParent->GetParent();
	}
	else {
		ctx.parentObj = GetWxObject(ctx.objParent);
	}

	if (!resolveWindow)
		return ctx;

	// A static-box sizer embeds its own static-box window as the child's
	// wxWindow parent. Otherwise fall back to a window cast of parentObj
	// and walk up the tree looking for the nearest COMPONENT_TYPE_WINDOW
	// ancestor; finally use the host's background window.
	if (ctx.objParent != nullptr &&
		ctx.objParent->GetClassName() == wxT("Staticboxsizer"))
	{
		wxStaticBoxSizer* s = dynamic_cast<wxStaticBoxSizer*>(ctx.parentObj);
		wxASSERT(s);
		ctx.windowObj = s->GetStaticBox();
	}
	else {
		ctx.windowObj = dynamic_cast<wxWindow*>(ctx.parentObj);
	}

	for (ibValueFrame* up = ctx.objParent; !ctx.windowObj && up != nullptr; up = up->GetParent()) {
		if (up->GetComponentType() == COMPONENT_TYPE_WINDOW) {
			ctx.windowObj = dynamic_cast<wxWindow*>(GetWxObject(up));
			break;
		}
	}

	if (ctx.windowObj == nullptr)
		ctx.windowObj = GetBackgroundWindow();

	return ctx;
}

void ibVisualHost::CreateControl(ibValueFrame* obj, ibValueFrame* parent, bool firstCreated)
{
	const ControlContext ctx = ResolveControlContext(obj, parent);

	wxWindowUpdateLocker freeze(GetParentBackgroundWindow());

	GenerateControl(ctx.objControl, ctx.windowObj, ctx.parentObj, firstCreated);
	RefreshControl(ctx.objControl, ctx.windowObj, ctx.parentObj);
	RelayoutStaticBoxIfAny(ctx.objParent, ctx.parentObj);

	CalculateLabelSize(obj);
	UpdateHostSize();
	UpdateVirtualSize();
}

void ibVisualHost::UpdateControl(ibValueFrame* obj, ibValueFrame* parent)
{
	const ControlContext ctx = ResolveControlContext(obj, parent);

	wxWindowUpdateLocker freeze(GetParentBackgroundWindow());

	RefreshControl(ctx.objControl, ctx.windowObj, ctx.parentObj);
	RelayoutStaticBoxIfAny(ctx.objParent, ctx.parentObj);

	CalculateLabelSize(obj);
	// The FORM's own command bar autofills from the MAIN source control's action collection, so a control
	// property change that alters that set — e.g. a list's ChoiceMode adds the Select command — must refresh
	// the form layers too. RefreshControl only refreshed the control's OWN (here suppressed) bar; without this
	// the form toolbar kept the stale band until the whole host rebuilt. Cheap: BuildCommands + show/hide.
	UpdateFormLayers(GetValueForm(), m_formLayerParts);
	UpdateHostSize();
	UpdateVirtualSize();
}

void ibVisualHost::RemoveControl(ibValueFrame* obj, ibValueFrame* parent)
{
	const ControlContext ctx = ResolveControlContext(obj, parent, /*resolveWindow*/false);

	wxWindowUpdateLocker freeze(GetParentBackgroundWindow());

	ClearControl(ctx.objControl);
	RelayoutStaticBoxIfAny(ctx.objParent, ctx.parentObj);

	CalculateLabelSize(obj);
	UpdateHostSize();
	UpdateVirtualSize();
}

#include "frontend/win/ctrls/dynamicBorder.h"

void ibVisualHost::ClearControl(ibValueFrame* control, bool force)
{
	// Post-order: tear children down before the parent so destroy callbacks
	// see a consistent hierarchy.
	for (unsigned int i = 0; i < control->GetChildCount(); ++i)
		ClearControl(control->GetChild(i), force);

	switch (control->GetComponentType())
	{
	case COMPONENT_TYPE_WINDOW:
	{
		// control's component type says WINDOW → the wxObject stored
		// by Create is a wxWindow. static_cast is invariant-safe. For a chrome-wrapped
		// control this is the wrapper PANEL; destroying it takes the toolbar + inner
		// control, and Cleanup routes through CleanupWithLayers to tear down the inner.
		wxWindow* controlWnd = static_cast<wxWindow*>(GetWxObject(control));
		if (controlWnd == nullptr)
			return;

		wxWindow* controlParent = controlWnd->GetParent();
		Cleanup(control, controlWnd);
		RemoveInnerControl(control);
		controlWnd->DeletePendingEvents();
		controlWnd->Destroy();
		if (!force && controlParent != nullptr)
			controlParent->Layout();
		break;
	}

	case COMPONENT_TYPE_SIZER:
	{
		wxSizer* controlSizer = static_cast<wxSizer*>(GetWxObject(control));
		if (controlSizer == nullptr)
			return;

		ibValueFrame* controlParent = control->GetParent();
		Cleanup(control, controlSizer);
		RemoveInnerControl(control);

		// Unwrap SIZERITEM wrapper to get to the logical parent container.
		if (controlParent != nullptr &&
			controlParent->GetComponentType() == COMPONENT_TYPE_SIZERITEM)
		{
			controlParent = controlParent->GetParent();
		}

		wxSizer* controlParentSizer = nullptr;
		if (controlParent != nullptr && controlParent->GetClassName() == wxT("NotebookPage")) {
			ibPanelPage* pageWnd = dynamic_cast<ibPanelPage*>(GetWxObject(controlParent));
			wxASSERT(pageWnd);
			controlParentSizer = pageWnd->GetSizer();
		}
		else {
			controlParentSizer = dynamic_cast<wxSizer*>(GetWxObject(controlParent));
		}

		if (controlParentSizer != nullptr)
			controlParentSizer->Detach(controlSizer);
		wxDELETE(controlSizer);
		if (!force && controlParentSizer != nullptr)
			controlParentSizer->Layout();
		break;
	}

	case COMPONENT_TYPE_FRAME:
		// Form root — nothing to delete here; children were torn down above.
		break;

	default:
	{
		wxObject* controlObj = GetWxObject(control);
		if (controlObj != nullptr) {
			Cleanup(control, controlObj);
			RemoveInnerControl(control);
			wxDELETE(controlObj);
		}
		break;
	}
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////////////

bool ibVisualHost::CalculateLabelSize(ibValueFrame* control)
{
	// Two-pass alignment of label columns across a form's layout tree.
	//
	//   Pass 1 (Calculate): walk the current vertical sizer, find every
	//   child that exposes ibControlDynamicBorder, and record the widest
	//   natural label width among them. Horizontal sizers are treated as
	//   a single unit — their contents do not share a vertical column.
	//
	//   Pass 2 (Apply): feed that width back to each contributing control
	//   via ApplyLabelSize(). Labels in the same vertical run line up on
	//   one grid, so their trailing controls (text fields, combos, ...)
	//   start at the same x offset.
	//
	// Apply uses post-order traversal so nested vertical groups compute
	// their own local max before the outer group aligns top-level items.
	// A horizontal sizer resets the outer max after its label is handled,
	// giving each vertical run its own fresh column budget.
	struct ibLabelAligner {

		// Recursively collect the max label width within a vertical sizer.
		static void Calculate(const ibValueFrame* node,
			wxBoxSizer* parentSizer, int& maxLabelWidth)
		{
			if (node->GetComponentType() == COMPONENT_TYPE_SIZERITEM)
				node = node->GetChild(0);
			wxASSERT(node);

			wxObject* wxObj = node->GetWxObject();
			ibControlDynamicBorder* label =
				dynamic_cast<ibControlDynamicBorder*>(wxObj);

			if (label != nullptr && label->AllowCalc()) {
				wxCoord w = 0, h = 0;
				label->CalculateLabelSize(&w, &h);
				if (w > maxLabelWidth)
					maxLabelWidth = w;
			}

			// A horizontal sizer stops the column — its children sit on
			// one row, not stacked vertically.
			if (parentSizer->GetOrientation() == wxHORIZONTAL)
				return;

			wxBoxSizer* childSizer = dynamic_cast<wxBoxSizer*>(wxObj);
			wxBoxSizer* nextParent = childSizer != nullptr ? childSizer : parentSizer;
			for (unsigned int i = 0; i < node->GetChildCount(); ++i)
				Calculate(node->GetChild(i), nextParent, maxLabelWidth);
		}

		// Recursively apply the computed width back to each label.
		static void Apply(ibValueFrame* node,
			wxBoxSizer* parentSizer, int& maxLabelWidth)
		{
			if (node->GetComponentType() == COMPONENT_TYPE_SIZERITEM)
				node = node->GetChild(0);
			wxASSERT(node);

			wxObject* wxObj = node->GetWxObject();
			wxBoxSizer* childSizer = dynamic_cast<wxBoxSizer*>(wxObj);
			ibControlDynamicBorder* label =
				dynamic_cast<ibControlDynamicBorder*>(wxObj);

			// Children see a snapshot of our current max; any adjustments
			// they make stay local to their subtree.
			int subtreeMax = maxLabelWidth;
			wxBoxSizer* nextParent = childSizer != nullptr ? childSizer : parentSizer;
			for (unsigned int i = 0; i < node->GetChildCount(); ++i)
				Apply(node->GetChild(i), nextParent, subtreeMax);

			if (label != nullptr && label->AllowCalc()) {
				label->ApplyLabelSize(maxLabelWidth != wxNOT_FOUND
					? wxSize(maxLabelWidth, wxNOT_FOUND)
					: wxDefaultSize);

				// After a label in a horizontal sizer we break the column
				// so the next vertical run starts from zero.
				if (parentSizer->GetOrientation() == wxHORIZONTAL)
					maxLabelWidth = wxNOT_FOUND;
			}
		}

		static bool CalculateAndApply(const ibValueForm* form)
		{
			wxBoxSizer* rootSizer =
				dynamic_cast<wxBoxSizer*>(form->GetWxObject());
			if (rootSizer == nullptr)
				return false;

			int maxLabelWidth = 0;
			if (rootSizer->GetOrientation() == wxVERTICAL)
				Calculate(form, rootSizer, maxLabelWidth);

			for (unsigned int i = 0; i < form->GetChildCount(); ++i)
				Apply(form->GetChild(i), rootSizer, maxLabelWidth);

			return true;
		}
	};

	return ibLabelAligner::CalculateAndApply(GetValueForm());
}

void ibVisualHost::UpdateVirtualSize()
{
	int w, h, panelW, panelH;
	GetVirtualSize(&w, &h);
	GetBackgroundWindow()->GetSize(&panelW, &panelH);
	panelW += 50; panelH += 50;
	if (panelW != w || panelH != h)
		SetVirtualSize(panelW, panelH);

	//Refresh frame window
	wxScrolledCanvas::Refresh();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////

std::set<ibFrontendVisualEditorNotebook*> ibFrontendVisualEditorNotebook::ms_visualEditorArray = {};

/////////////////////////////////////////////////////////////////////////////////////////////////////

ibFrontendVisualEditorNotebook::ibFrontendVisualEditorNotebook()
{
	ms_visualEditorArray.insert(this);
}

ibFrontendVisualEditorNotebook::~ibFrontendVisualEditorNotebook()
{
	ms_visualEditorArray.erase(this);
}

ibFrontendVisualEditorNotebook* ibFrontendVisualEditorNotebook::FindEditorByForm(const ibValueFrame* valueForm)
{
	for (auto& visualEditor : ms_visualEditorArray) {
		if (valueForm == visualEditor->GetValueForm())
			return visualEditor;
	}

	return nullptr;
}

#endif // !OES_USE_WEB