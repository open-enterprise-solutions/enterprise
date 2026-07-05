////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : window object
////////////////////////////////////////////////////////////////////////////

#include "window.h"
#include "backend/serialize/dataBuilder.h"   // ibDataNode (control -> node)
#include "form.h"
#include "frontend/visualView/layers/commandBar.h"  // ibValueCommandBar (store, web-safe) + BuildCommandBarToolBar
#ifndef OES_USE_WEB
#include "frontend/visualView/canvasWindow.h"   // ibCanvasWindow — layer canvas
#endif


//***********************************************************************************
//*                                    ValueWindow                                  *
//***********************************************************************************

ibValueWindow::ibValueWindow() : ibValueControl()
{
}

//***********************************************************************************
//*                                  Update                                       *
//***********************************************************************************

void ibValueWindow::UpdateWindow(ibFrontendWindow* window)
{
	if (window == nullptr)
		return;

	// Detached controls — /demo synthetic trees and other
	// programmatically-built hierarchies that never got a form owner —
	// used to crash here on the wxASSERT(ownerForm). For those, treat
	// the form as "enabled" by default; the control's own property
	// still gets applied. Form-owned controls keep the combined check.
	ibValueForm* ownerForm = GetOwnerForm();
	const bool formEnabled = (ownerForm == nullptr) ? true : ownerForm->IsFormEnabled();

	// Unified setter sequence — desktop's wxWindow and web's ibWebWindow
	// share the same SetMinSize / SetMaxSize / SetFont /
	// SetForegroundColour / SetBackgroundColour / Enable / Show /
	// SetToolTip API, so one body covers both. Only window->Layout()
	// at the end is wx-specific — web uses flex layout on the client,
	// no re-layout call needed.
	if (m_propertyMinSize->GetValueAsSize() != wxDefaultSize)
		window->SetMinSize(m_propertyMinSize->GetValueAsSize());
	if (m_propertyMaxSize->GetValueAsSize() != wxDefaultSize)
		window->SetMaxSize(m_propertyMaxSize->GetValueAsSize());
	
	ApplyLook(window);   // font / colours (shared with the layer look sync)
	
	window->Enable(m_propertyEnabled->GetValueAsBoolean() && formEnabled);
	
	// A source-bound control with NO source picked is never rendered — in the DESIGNER too, so it is
	// plainly visible (by its absence on the canvas) that such a control is unusable until bound; it is
	// still reachable through the object tree + inspector to pick a source. IsSourceMissing is a base
	// virtual — false for plain windows, overridden by the source controls — so no cross-cast is needed.
	// For a composite (tablebox) the SAME gate hides the whole chrome wrapper in UpdateWithLayers; here it
	// hides a plain control's widget.
	window->Show(m_propertyVisible->GetValueAsBoolean() && !IsSourceMissing());
	window->SetToolTip(m_propertyTooltip->GetValueAsTranslateString());

#ifndef OES_USE_WEB
	// Size changes may require a re-layout; flex handles that on web.
	if (m_propertyMinSize->GetValueAsSize() != wxDefaultSize ||
		m_propertyMaxSize->GetValueAsSize() != wxDefaultSize) {
		window->Layout();
	}
#endif
}

void ibValueWindow::ApplyLook(ibFrontendWindow* window)
{
	if (window == nullptr)
		return;
	if (m_propertyFont->IsOk())
		window->SetFont(m_propertyFont->GetValueAsFont());
	if (m_propertyFG->IsOk())
		window->SetForegroundColour(m_propertyFG->GetValueAsColour());
	if (m_propertyBG->IsOk())
		window->SetBackgroundColour(m_propertyBG->GetValueAsColour());
}

//**********************************************************************************
//*                                    Data										   *
//**********************************************************************************

bool ibValueWindow::ReadData(const ibDataNode& node)
{
	m_propertyMinSize->ReadNodeValue(node.GetProperty(m_propertyMinSize->GetName()));
	m_propertyMaxSize->ReadNodeValue(node.GetProperty(m_propertyMaxSize->GetName()));
	m_propertyFont->ReadNodeValue(node.GetProperty(m_propertyFont->GetName()));
	m_propertyFG->ReadNodeValue(node.GetProperty(m_propertyFG->GetName()));
	m_propertyBG->ReadNodeValue(node.GetProperty(m_propertyBG->GetName()));
	m_propertyTooltip->ReadNodeValue(node.GetProperty(m_propertyTooltip->GetName()));
	m_propertyEnabled->ReadNodeValue(node.GetProperty(m_propertyEnabled->GetName()));
	m_propertyVisible->ReadNodeValue(node.GetProperty(m_propertyVisible->GetName()));

	return ibValueControl::ReadData(node);
}

bool ibValueWindow::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyMinSize->GetName(), m_propertyMinSize->GetNodeValue());
	node.SetProperty(m_propertyMaxSize->GetName(), m_propertyMaxSize->GetNodeValue());
	node.SetProperty(m_propertyFont->GetName(), m_propertyFont->GetNodeValue());
	node.SetProperty(m_propertyFG->GetName(), m_propertyFG->GetNodeValue());
	node.SetProperty(m_propertyBG->GetName(), m_propertyBG->GetNodeValue());
	node.SetProperty(m_propertyTooltip->GetName(), m_propertyTooltip->GetNodeValue());
	node.SetProperty(m_propertyEnabled->GetName(), m_propertyEnabled->GetNodeValue());
	node.SetProperty(m_propertyVisible->GetName(), m_propertyVisible->GetNodeValue());

	return ibValueControl::WriteData(node);
}

//***********************************************************************************
//*                    ValueWindow — composite (chrome) variant                     *
//***********************************************************************************

// All five stages route through the layer canvas. Create builds the wrapper and puts
// the real control inside as its inner child; the rest unwrap the composite back to the
// inner (via ibCanvasWindow::GetGenericControl) before calling the control's own stage.

ibValueWindowComposite::ibValueWindowComposite() : ibValueWindow()
{
	// The frame owns the command-bar STORE and points it back at itself; the
	// visual host reads GetCommandBar() to render the toolbar layer. Non-composite
	// controls never create one, so their GetCommandBar() stays null.
	m_commandBar = new ibValueCommandBar();
	m_commandBar->SetOwner(this);
}

wxObject* ibValueWindowComposite::GetInnerWx() const
{
	wxObject* wx = GetWxObject();
#ifndef OES_USE_WEB
	if (ibCanvasWindow* chrome = wxDynamicCast(wx, ibCanvasWindow))
		return chrome->GetGenericControl();
#endif
	return wx;
}

bool ibValueWindowComposite::WriteData(ibDataNode& node) const
{
	// "Layers" block — the command bar is the first (and today only) layer; a search field /
	// status bar would add more sub-nodes here without touching the call sites.
	if (ibValueCommandBar* bar = GetCommandBar())
		bar->WriteData(node.Child(wxT("Layers")).Child(wxT("CommandBar")));
	return ibValueWindow::WriteData(node);
}

bool ibValueWindowComposite::ReadData(const ibDataNode& node)
{
	if (ibValueCommandBar* bar = GetCommandBar()) {
		if (const ibDataNode* layers = node.FindChild(wxT("Layers"))) {
			if (const ibDataNode* barNode = layers->FindChild(wxT("CommandBar")))
				bar->ReadData(*barNode);
		}
	}
	return ibValueWindow::ReadData(node);
}

std::vector<ibFrontendWindow*> ibValueWindowComposite::BuildLayerParts(ibFrontendWindow* parent, ibValueCommandBar* cbar, ibValueForm* form)
{
	// The ONE orchestration point — the composite window (and the form) assemble chrome here.
	// Per-part render is delegated: the toolbar from commandBar.h's BuildCommandBarToolBar.
	// Future parts (search field, status bar, …) are added HERE only — call sites never change.
	std::vector<ibFrontendWindow*> parts;
#ifndef OES_USE_WEB
	if (ibFrontendWindow* bar = BuildCommandBarToolBar(parent, cbar, form))
		parts.push_back(bar);
#endif
	return parts;
}

void ibValueWindowComposite::UpdateLayerParts(const std::vector<ibFrontendWindow*>& parts, ibValueCommandBar* cbar, ibValueForm* form)
{
	// Refresh the EXISTING parts in place — stable refs, NO recreate. Form and composite call
	// this SAME static; only WHERE the parts live differs (host-specific).
#ifndef OES_USE_WEB
	for (ibFrontendWindow* part : parts)
		RefreshCommandBarToolBar(part, cbar, form);

#endif
}

wxObject* ibValueWindowComposite::CreateWithLayers(ibFrontendWindow* wndParent, ibVisualHost* visualHost)
{
#ifndef OES_USE_WEB
	ibCanvasWindow* chrome = new ibCanvasWindow(wndParent);
	// Build the toolbar layer whenever the control HAS a command-bar object — the part must exist
	// even while currently SUPPRESSED (a table that IS the form's main source: its commands live on
	// the FORM toolbar) or empty, so a later Update can show/hide it on a main-toggle WITHOUT a
	// rebuild. Gating the BUILD on HasCommandBar() left a main-bound control with no part at all, so
	// un-setting main could never surface a toolbar until the form was reopened.
	ibValueCommandBar* cbar = GetCommandBar();
	for (ibFrontendWindow* part : BuildLayerParts(chrome, cbar, GetOwnerForm()))
		chrome->AddLayer(part);
	wxObject* inner = Create(chrome, visualHost);   // old create event, parented at the composite
	if (wxWindow* innerWin = wxDynamicCast(inner, wxWindow))
		chrome->SetInner(innerWin);
	// Initial visibility folds in suppression: HasCommandBar() is false while bound to the main, so
	// the effective cbar goes null and the bar hides. The SAME effective-cbar refresh runs on every
	// Update, so a main-toggle re-evaluates the toolbar in place — no rebuild, no reopen.
	UpdateLayerParts(chrome->GetLayerParts(), HasCommandBar() ? cbar : nullptr, GetOwnerForm());
	return chrome;
#else
	return Create(wndParent, visualHost);
#endif
}

void ibValueWindowComposite::OnCreatedWithLayers(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost, bool firstCreated)
{
#ifndef OES_USE_WEB
	if (ibCanvasWindow* chrome = wxDynamicCast(wxobject, ibCanvasWindow)) {
		OnCreated(chrome->GetGenericControl(), wxparent, visualHost, firstCreated);
		return;
	}
#else 
	OnCreated(wxobject, wxparent, visualHost, firstCreated);
#endif
}

void ibValueWindowComposite::UpdateWithLayers(wxObject* wxobject, ibVisualHost* visualHost)
{
#ifndef OES_USE_WEB
	if (ibCanvasWindow* chrome = wxDynamicCast(wxobject, ibCanvasWindow)) {
		
		// Effective cbar folds in suppression (null while bound to the main) so a main-toggle
		// hides / shows the bar in place — matches CreateWithLayers.
		UpdateLayerParts(chrome->GetLayerParts(), HasCommandBar() ? GetCommandBar() : nullptr, GetOwnerForm());
		
		// Inner control's own update (data + its window props on the inner widget).
		Update(chrome->GetGenericControl(), visualHost);
		// Toolbar inherits the control's LOOK (font / colours only, via ApplyLook —
		// NOT UpdateWindow, whose MinSize/MaxSize would wreck the toolbar layout).
		for (ibFrontendWindow* part : chrome->GetLayerParts())
			ApplyLook(part);
		
		// A source-bound composite (tablebox) with NO source hides its WHOLE chrome wrapper — the inner
		// widget + toolbar fold in with the parent (the inner's own UpdateWindow only hid the inner,
		// leaving an empty chrome). Hidden in the designer too, so an unbound tablebox is plainly unusable.
		chrome->Show(!IsSourceMissing());
		return;
	}
#else 
	Update(wxobject, visualHost);
#endif
}

void ibValueWindowComposite::OnUpdatedWithLayers(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost)
{
#ifndef OES_USE_WEB
	if (ibCanvasWindow* chrome = wxDynamicCast(wxobject, ibCanvasWindow)) {
		OnUpdated(chrome->GetGenericControl(), wxparent, visualHost);
		return;
	}
#else 
	OnUpdated(wxobject, wxparent, visualHost);
#endif
}

void ibValueWindowComposite::CleanupWithLayers(wxObject* wxobject, ibVisualHost* visualHost)
{
#ifndef OES_USE_WEB
	// Hand the inner widget to the plain Cleanup so the control tears down its OWN widget;
	// destroying the composite (ClearControl) then takes the toolbar and the inner with it.
	if (ibCanvasWindow* chrome = wxDynamicCast(wxobject, ibCanvasWindow)) {
		// No manual teardown of the parts — destroying the chrome window (ClearControl) takes
		// the toolbar and the inner control with it. Here we only run the inner's own cleanup.
		Cleanup(chrome->GetGenericControl(), visualHost);
		return;
	}
#else 
	Cleanup(wxobject, visualHost);
#endif
}