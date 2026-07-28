#include "toolbar.h"
#include "backend/serialize/dataBuilder.h"   // ibDataNode (control -> node)
#include "backend/appData.h"
#ifdef OES_USE_WEB
#include "frontend/web/webWindow.h"
#include "backend/backend_picture.h"
#endif

//***********************************************************************************
//*                           IMPLEMENT_DYNAMIC_CLASS                               *
//***********************************************************************************


//***********************************************************************************
//*                           ibValueToolBarItem                               *
//***********************************************************************************

ibValueToolBarItem::ibValueToolBarItem() : ibValueControl()
{
}

wxObject* ibValueToolBarItem::Create(ibFrontendWindow* /*wxparent*/, ibVisualHost* /*visualHost*/)
{
#ifdef OES_USE_WEB
	// Web: real render shim. Caption priority:
	//   1. GetItemCaption(owner's action collection) — resolves a
	//      system action's caption when per-tool m_propertyTitle is
	//      empty (Auto representation). ibValueToolbar::Create
	//      populates m_actionArray up front so this works.
	//   2. m_propertyTitle — Designer-set per-tool title.
	//   3. GetControlName() — internal fallback so truly-unlabelled
	//      tools at least carry their name ("MainToolbarClose" etc.).
	wxString caption;
	if (ibValueToolbar* owner = GetOwner())
		caption = GetItemCaption(owner->GetActionArray());
	if (caption.IsEmpty())
		caption = m_propertyTitle->GetValueAsTranslateString();
	if (caption.IsEmpty())
		caption = GetControlName();

	ibWebToolBarItem* item = new ibWebToolBarItem(caption, GetControlID());
	item->Enable(m_propertyEnabled->GetValueAsBoolean());
	return item;
#else
	// Desktop: wxAuiToolBar::AddTool fires in OnCreated below with
	// the parent toolbar. Create just returns a placeholder that
	// OnCreated's SetUserData picks up as a unique lookup key.
	return new ibNoObject;
#endif
}

void ibValueToolBarItem::OnCreated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost, bool firstCreated)
{
#ifdef OES_USE_WEB
	(void)wxobject; (void)wxparent; (void)visualHost; (void)firstCreated;
	// Web: Create already built the shim; nothing live to poke.
#else
	ibAuiToolBar* toolbar = dynamic_cast<ibAuiToolBar*>(wxparent);
	wxASSERT(toolbar);
	wxAuiToolBarItem* toolItem = toolbar->AddTool(GetControlID(),
		m_propertyTitle->GetValueAsTranslateString(),
		m_propertyPicture->GetValueAsBitmap(),
		wxNullBitmap,
		wxItemKind::wxITEM_NORMAL,
		m_properyTooltip->GetValueAsTranslateString(),
		wxEmptyString,
		wxobject
	);

#if defined(_LP64) || defined(__LP64__) || defined(__arch64__) || defined(_WIN64)
	toolItem->SetUserData((long long)wxobject);
#else 
	toolItem->SetUserData((long)wxobject);
#endif 

	toolbar->EnableTool(toolItem->GetId(), m_propertyEnabled->GetValueAsBoolean());

	if (m_propertyContextMenu->GetValueAsBoolean() == true && !toolItem->HasDropDown())
		toolbar->SetToolDropDown(toolItem->GetId(), true);
	else if (m_propertyContextMenu->GetValueAsBoolean() == false && toolItem->HasDropDown())
		toolbar->SetToolDropDown(toolItem->GetId(), false);

	toolbar->Realize();
	if (!appData->DesignerMode() || !visualHost->IsDesignerHost())
		toolbar->SetToolBarMinSize(wxSize(-1, toolbar->GetMinHeight()));
	toolbar->Refresh();
	toolbar->Update();
#endif
}

void ibValueToolBarItem::Update(wxObject* wxobject, ibVisualHost* visualHost)
{
#ifdef OES_USE_WEB
	(void)visualHost;
	// Mirror desktop OnUpdated's Auto/Picture/PictureAndText/Text
	// switch — representation drives whether the browser shows
	// icon, text, or both. Caption + picture come from the tool's
	// own property OR the action collection (via GetItemCaption /
	// GetItemPicture helpers).
	ibWebToolBarItem* item = static_cast<ibWebToolBarItem*>(wxobject);

	ibValueToolbar* owner = GetOwner();
	const ibStandardCommandSet& coll = owner ? owner->GetActionArray()
	                                       : ibStandardCommandSet();

	// Effective representation — Auto resolves through collection.
	ibRepresentation rep = m_propertyRepresentation->GetValueAsEnum();
	if (rep == ibRepresentation::ibRepresentation_Auto)
		rep = GetItemRepresentation(coll);

	// Always resolve the caption (independent of representation) so the
	// browser has it available. The client decides show/hide based on
	// representation; emitting the caption unconditionally also helps
	// accessibility (aria-label, tooltip fallback).
	wxString caption = GetItemCaption(coll);
	if (caption.IsEmpty())
		caption = m_propertyTitle->GetValueAsTranslateString();
	if (caption.IsEmpty())
		caption = GetControlName();

	// hasPicture: any non-Text mode that ends up with a real bitmap
	// from the tool's own property or the action's picture.
	const wxBitmap bmp = (rep == ibRepresentation::ibRepresentation_Text)
		? wxNullBitmap
		: GetItemPicture(coll);
	const bool hasPic = bmp.IsOk();

	// data:image/png;base64,... so the browser can <img src> it directly.
	// ibBackendPicture::CreateBase64Image handles the PNG encode.
	wxString pictureUri;
	if (hasPic) {
		const wxString b64 = ibBackendPicture::CreateBase64Image(bmp.ConvertToImage());
		if (!b64.IsEmpty())
			pictureUri = wxT("data:image/png;base64,") + b64;
	}

	item->SetLabel(caption);
	item->SetRepresentation(static_cast<int>(rep));
	item->SetHasPicture(hasPic);
	item->SetPictureDataUri(pictureUri);
	item->Enable(m_propertyEnabled->GetValueAsBoolean());
#else
	(void)wxobject;
	(void)visualHost;
#endif
}

void ibValueToolBarItem::OnUpdated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost)
{
#ifdef OES_USE_WEB
	(void)wxobject; (void)wxparent; (void)visualHost;
#else
	ibAuiToolBar* toolbar = dynamic_cast<ibAuiToolBar*>(wxparent);
	wxASSERT(toolbar);

	wxAuiToolBarItem* toolItem = toolbar->FindTool(GetControlID());
	ibValueFrame* parentControl = GetParent(); int idx = wxNOT_FOUND;

	for (unsigned int i = 0; i < parentControl->GetChildCount(); i++) {
		ibValueFrame* child = parentControl->GetChild(i);
		if (m_controlId == child->GetControlID()) {
			idx = i;
			break;
		}
	}

	if (toolItem != nullptr)
		toolbar->DestroyTool(GetControlID());

	if (m_propertyRepresentation->GetValueAsEnum() == ibRepresentation::ibRepresentation_Auto) {
		const ibStandardCommandSet& collection = GetOwner()->GetActionArray();
		if (GetItemRepresentation(collection) == ibRepresentation::ibRepresentation_PictureAndText) {
			toolItem = toolbar->InsertTool(idx, GetControlID(),
				GetItemCaption(collection),
				GetItemPicture(collection),
				wxNullBitmap,
				wxItemKind::wxITEM_NORMAL,
				GetItemToolTip(collection),
				wxEmptyString,
				wxobject
			);
		}
		else if (GetItemRepresentation(collection) == ibRepresentation::ibRepresentation_Picture) {
			toolItem = toolbar->InsertTool(idx, GetControlID(),
				wxEmptyString,
				GetItemPicture(collection),
				wxNullBitmap,
				wxItemKind::wxITEM_NORMAL,
				GetItemToolTip(collection),
				wxEmptyString,
				wxobject
			);
		}
		else if (GetItemRepresentation(collection) == ibRepresentation::ibRepresentation_Text) {
			toolItem = toolbar->InsertTool(idx, GetControlID(),
				GetItemCaption(collection),
				wxNullBitmap,
				wxNullBitmap,
				wxItemKind::wxITEM_NORMAL,
				GetItemToolTip(collection),
				wxEmptyString,
				wxobject
			);
		}
	}
	else if (m_propertyRepresentation->GetValueAsEnum() == ibRepresentation::ibRepresentation_PictureAndText) {
		const ibStandardCommandSet& collection = GetOwner()->GetActionArray();
		toolItem = toolbar->InsertTool(idx, GetControlID(),
			GetItemCaption(collection),
			GetItemPicture(collection),
			wxNullBitmap,
			wxItemKind::wxITEM_NORMAL,
			GetItemToolTip(collection),
			wxEmptyString,
			wxobject
		);
	}
	else if (m_propertyRepresentation->GetValueAsEnum() == ibRepresentation::ibRepresentation_Picture) {
		const ibStandardCommandSet& collection = GetOwner()->GetActionArray();
		toolItem = toolbar->InsertTool(idx, GetControlID(),
			wxEmptyString,
			GetItemPicture(collection),
			wxNullBitmap,
			wxItemKind::wxITEM_NORMAL,
			GetItemToolTip(collection),
			wxEmptyString,
			wxobject
		);
	}
	else if (m_propertyRepresentation->GetValueAsEnum() == ibRepresentation::ibRepresentation_Text) {
		const ibStandardCommandSet& collection = GetOwner()->GetActionArray();
		toolItem = toolbar->InsertTool(idx, GetControlID(),
			GetItemCaption(collection),
			wxNullBitmap,
			wxNullBitmap,
			wxItemKind::wxITEM_NORMAL,
			GetItemToolTip(collection),
			wxEmptyString,
			wxobject
		);
	}

#if defined(_LP64) || defined(__LP64__) || defined(__arch64__) || defined(_WIN64)
	toolItem->SetUserData((long long)wxobject);
#else 
	toolItem->SetUserData((long)wxobject);
#endif 

	toolbar->EnableTool(toolItem->GetId(), m_propertyEnabled->GetValueAsBoolean());

	if (m_propertyContextMenu->GetValueAsBoolean() == true && !toolItem->HasDropDown())
		toolbar->SetToolDropDown(toolItem->GetId(), true);
	else if (m_propertyContextMenu->GetValueAsBoolean() == false && toolItem->HasDropDown())
		toolbar->SetToolDropDown(toolItem->GetId(), false);

	toolbar->Realize();
	if (!appData->DesignerMode() || !visualHost->IsDesignerHost())
		toolbar->SetToolBarMinSize(wxSize(-1, toolbar->GetMinHeight()));
	toolbar->Refresh();
	toolbar->Update();

	toolbar->InvalidateBestSize();
#endif
}

void ibValueToolBarItem::Cleanup(wxObject* obj, ibVisualHost* visualHost)
{
#ifdef OES_USE_WEB
	(void)obj; (void)visualHost;
#else
	ibAuiToolBar* toolbar = dynamic_cast<ibAuiToolBar*>(visualHost->GetWxObject(GetParent()));

	toolbar->DestroyTool(GetControlID());

	toolbar->Realize();
	if (!appData->DesignerMode() || !visualHost->IsDesignerHost())
		toolbar->SetToolBarMinSize(wxSize(-1, toolbar->GetMinHeight()));
	toolbar->Refresh();
	toolbar->Update();
#endif
}

bool ibValueToolBarItem::CanDeleteControl() const
{
	unsigned int toolCount = 0;
	for (unsigned int idx = 0; idx < m_parent->GetChildCount(); idx++) {
		auto child = m_parent->GetChild(idx);
		if (wxT("tool") == child->GetClassName()) {
			toolCount++;
		}
	}

	return toolCount > 1;
}

//***********************************************************************************
//*                           ibValueToolBarSeparator                                *
//***********************************************************************************

ibValueToolBarSeparator::ibValueToolBarSeparator() : ibValueControl()
{
}

wxObject* ibValueToolBarSeparator::Create(ibFrontendWindow* /*wxparent*/, ibVisualHost* /*visualHost*/)
{
#ifdef OES_USE_WEB
	return new ibWebToolBarSeparator(GetControlID());
#else
	return new ibNoObject;
#endif
}

void ibValueToolBarSeparator::OnCreated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost, bool firstCreated)
{
#ifdef OES_USE_WEB
	(void)wxobject; (void)wxparent; (void)visualHost; (void)firstCreated;
#else
	ibAuiToolBar* toolbar = dynamic_cast<ibAuiToolBar*>(visualHost->GetWxObject(GetParent()));
	wxASSERT(toolbar);
	wxAuiToolBarItem* toolItem = toolbar->AddSeparator();
	toolItem->SetId(GetControlID());

	toolbar->Realize();
	if (!appData->DesignerMode() || !visualHost->IsDesignerHost())
		toolbar->SetToolBarMinSize(wxSize(-1, toolbar->GetMinHeight()));
	toolbar->Refresh();
	toolbar->Update();
#endif
}

void ibValueToolBarSeparator::OnUpdated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost)
{
#ifdef OES_USE_WEB
	(void)wxobject; (void)wxparent; (void)visualHost;
#else
	ibAuiToolBar* toolbar = dynamic_cast<ibAuiToolBar*>(visualHost->GetWxObject(GetParent()));
	wxASSERT(toolbar);

	wxAuiToolBarItem* toolItem = toolbar->FindTool(GetControlID());
	ibValueFrame* m_parentControl = GetParent(); int idx = wxNOT_FOUND;

	for (unsigned int i = 0; i < m_parentControl->GetChildCount(); i++)
	{
		ibValueFrame* child = m_parentControl->GetChild(i);
		if (m_controlId == child->GetControlID()) { idx = i; break; }
	}

	if (toolItem) { toolbar->DestroyTool(GetControlID()); }
	toolbar->InsertSeparator(idx, GetControlID());

	toolbar->Realize();
	if (!appData->DesignerMode() || !visualHost->IsDesignerHost())
		toolbar->SetToolBarMinSize(wxSize(-1, toolbar->GetMinHeight()));
	toolbar->Refresh();
	toolbar->Update();
#endif
}

void ibValueToolBarSeparator::Cleanup(wxObject* obj, ibVisualHost* visualHost)
{
#ifdef OES_USE_WEB
	(void)obj; (void)visualHost;
#else
	ibAuiToolBar* toolbar = dynamic_cast<ibAuiToolBar*>(visualHost->GetWxObject(GetParent()));
	if (toolbar) toolbar->DestroyTool(GetControlID());
#endif
}

bool ibValueToolBarSeparator::CanDeleteControl() const
{
	return m_parent->GetChildCount() > 1;
}

//**********************************************************************************
//*                                  Data	                                       *
//**********************************************************************************

bool ibValueToolBarItem::ReadData(const ibDataNode& node)
{
	m_propertyTitle->ReadNodeValue(node.GetProperty(m_propertyTitle->GetName()));
	m_propertyPicture->ReadNodeValue(node.GetProperty(m_propertyPicture->GetName()));
	m_propertyRepresentation->ReadNodeValue(node.GetProperty(m_propertyRepresentation->GetName()));
	m_propertyContextMenu->ReadNodeValue(node.GetProperty(m_propertyContextMenu->GetName()));
	m_properyTooltip->ReadNodeValue(node.GetProperty(m_properyTooltip->GetName()));
	m_propertyEnabled->ReadNodeValue(node.GetProperty(m_propertyEnabled->GetName()));
	m_eventAction->ReadNodeValue(node.GetProperty(m_eventAction->GetName()));

	//events
	m_eventAction->ReadNodeValue(node.GetProperty(m_eventAction->GetName()));
	return ibValueControl::ReadData(node);
}

bool ibValueToolBarItem::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyTitle->GetName(), m_propertyTitle->GetNodeValue());
	node.SetProperty(m_propertyPicture->GetName(), m_propertyPicture->GetNodeValue());
	node.SetProperty(m_propertyRepresentation->GetName(), m_propertyRepresentation->GetNodeValue());
	node.SetProperty(m_propertyContextMenu->GetName(), m_propertyContextMenu->GetNodeValue());
	node.SetProperty(m_properyTooltip->GetName(), m_properyTooltip->GetNodeValue());
	node.SetProperty(m_propertyEnabled->GetName(), m_propertyEnabled->GetNodeValue());
	node.SetProperty(m_eventAction->GetName(), m_eventAction->GetNodeValue());

	//events
	node.SetProperty(m_eventAction->GetName(), m_eventAction->GetNodeValue());
	return ibValueControl::WriteData(node);
}

bool ibValueToolBarSeparator::ReadData(const ibDataNode& node)
{
	return ibValueControl::ReadData(node);
}

bool ibValueToolBarSeparator::WriteData(ibDataNode& node) const
{
	return ibValueControl::WriteData(node);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

S_CONTROL_TYPE_REGISTER(ibValueToolBarItem, "Tool", "Tool", g_controlToolBarItemCLSID);
S_CONTROL_TYPE_REGISTER(ibValueToolBarSeparator, "ToolSeparator", "Tool", g_controlToolBarSeparatorCLSID);