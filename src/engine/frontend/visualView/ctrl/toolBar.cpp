#include "toolbar.h"
#include "backend/serialize/dataBuilder.h"   // ibDataNode (control -> node)
#include "frontend/visualView/visualHostClient.h"
#ifdef OES_USE_WEB
#include "frontend/web/webWindow.h"
#else
#include "frontend/win/theme/luna_toolbarart.h"
#endif

//***********************************************************************************
//*                           IMPLEMENT_DYNAMIC_CLASS                               *
//***********************************************************************************


//***********************************************************************************
//*                                  Custom Aui toolbar                             *
//***********************************************************************************

void ibValueToolbar::AddToolItem()
{
#ifndef OES_USE_WEB
	// Designer-only: g_visualHostContext is the designer's live
	// edit-tree gateway. wfrontend has no designer, so these helpers
	// are no-ops in the web build.
	wxASSERT(m_formOwner);
	ibValueFrame* toolItem = m_formOwner->NewObject(g_controlToolBarItemCLSID, this);
	g_visualHostContext->InsertControl(toolItem, this);
	g_visualHostContext->RefreshEditor();
#endif
}

void ibValueToolbar::AddToolSeparator()
{
#ifndef OES_USE_WEB
	wxASSERT(m_formOwner);
	ibValueFrame* toolSeparator = m_formOwner->NewObject(g_controlToolBarSeparatorCLSID, this);
	g_visualHostContext->InsertControl(toolSeparator, this);
	g_visualHostContext->RefreshEditor();
#endif
}

//***********************************************************************************
//*                                 Value Toolbar                                   *
//***********************************************************************************

ibValueToolbar::ibValueToolbar() : ibValueWindow()
{
}

wxObject* ibValueToolbar::Create(ibFrontendWindow* wxparent, ibVisualHost* visualHost)
{
#ifdef OES_USE_WEB
	(void)wxparent;
	(void)visualHost;
	auto* toolbar = new ibWebToolbar(GetControlID());
#else
	auto* toolbar = new ibAuiToolBar(wxparent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxAUI_TB_NO_AUTORESIZE | wxAUI_TB_HORZ_TEXT | wxAUI_TB_OVERFLOW);
	toolbar->SetDesignerMode(visualHost->IsDesignerHost());
	toolbar->SetArtProvider(new wxAuiLunaToolBarArt());
#endif
	// wxEVT_TOOL is shared across both toolbars — the web item fires
	// it on its parent just like wx's native toolbar does.
	toolbar->Bind(wxEVT_TOOL, &ibValueToolbar::OnTool, this);
#ifndef OES_USE_WEB
	// Dropdown + raw-click are desktop-only affordances (wxAuiToolBarEvent
	// needs wx/aui/auibar.h; LEFT_DOWN is for designer selection).
	toolbar->Bind(wxEVT_AUITOOLBAR_TOOL_DROPDOWN, &ibValueToolbar::OnToolDropDown, this);
	toolbar->Bind(wxEVT_LEFT_DOWN, &ibValueToolbar::OnToolBarLeftDown, this);
#endif
	return toolbar;
}

void ibValueToolbar::OnCreated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost, bool firstCreated)
{
#ifdef OES_USE_WEB
	(void)wxobject;
	(void)wxparent;
	(void)visualHost;
	(void)firstCreated;
#else
	if (visualHost->IsDesignerHost() && GetChildCount() == 0
		&& firstCreated) {
		ibValueToolbar::AddToolItem();
	}
#endif
}

void ibValueToolbar::Update(wxObject* wxobject, ibVisualHost* visualHost)
{
	(void)visualHost;
	// Only the pointer type differs between desktop and web; everything
	// else — source resolution, action-collection snapshot — is pure
	// runtime metadata work shared by both builds.
#ifdef OES_USE_WEB
	ibWebToolbar* toolbar = static_cast<ibWebToolbar*>(wxobject);
#else
	ibAuiToolBar* toolbar = static_cast<ibAuiToolBar*>(wxobject);
#endif

	if (toolbar != nullptr) {
		// Populate the action collection so each tool item's Update
		// can pull its caption via GetItemCaption(owner->GetActionArray()).
		// ibValueToolBarItem::Update runs in the walker's recursion
		// AFTER this — order guaranteed.
		//
		// Source resolution: prefer the saved m_actSource; when unset
		// (wxNOT_FOUND — common for forms saved without explicit action
		// source), fall back to the owner form. Desktop's BuildForm
		// sets m_actSource to FORM_ACTION at auto-build time; web
		// never auto-builds.
		const ibActionID id = GetActionSrc();
		ibValueFrame* sourceElement = id != wxNOT_FOUND
			? FindControlByID(id)
			: GetOwnerForm();
		if (sourceElement != nullptr)
			m_actionArray = sourceElement->GetStandardCommands(sourceElement->GetTypeForm());
		else
			m_actionArray = ibStandardCommandSet();
	}

	UpdateWindow(toolbar);
}

void ibValueToolbar::OnUpdated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost)
{
}

void ibValueToolbar::Cleanup(wxObject* obj, ibVisualHost* visualHost)
{
#ifdef OES_USE_WEB
	(void)obj;
	(void)visualHost;
#else
	ibAuiToolBar* toolbar = dynamic_cast<ibAuiToolBar*>(obj);
	toolbar->Unbind(wxEVT_TOOL, &ibValueToolbar::OnTool, this);
#endif
}

//**********************************************************************************
//*										 Property                                  *
//**********************************************************************************

bool ibValueToolbar::ReadData(const ibDataNode& node)
{
	m_actSource->ReadNodeValue(node.GetProperty(m_actSource->GetName()));
	return ibValueWindow::ReadData(node);
}

bool ibValueToolbar::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_actSource->GetName(), m_actSource->GetNodeValue());
	return ibValueWindow::WriteData(node);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_TYPE_REGISTER(ibValueToolbar, "Toolbar", "Toolbar", g_controlToolBarCLSID);