#include "sizer.h"
#include "backend/serialize/dataBuilder.h"   // ibDataNode (control -> node)
#include "form.h"
#ifndef OES_USE_WEB
#include "frontend/visualView/pageWindow.h"
#endif


#ifdef OES_USE_WEB
#include "frontend/web/webSizer.h"
#include "frontend/web/webWindow.h"
#endif

namespace {

// Resolve the sizer that actually owns this SizerItem's child. On
// desktop the NotebookPage wrapper hides its inner wxSizer, so we
// unwrap it here. On web the mapping is direct: parent's wxObject
// (ibWebSizer subclass) IS the container.
ibFrontendSizer* ResolveParentSizer(ibVisualHost* visualHost, ibValueFrame* object)
{
	ibValueFrame* parent = object->GetParent();
	if (parent == nullptr) return nullptr;
	wxObject* parentWx = visualHost->GetWxObject(parent);
#ifndef OES_USE_WEB
	if (parent->GetClassName() == wxT("NotebookPage")) {
		ibPanelPage* page = dynamic_cast<ibPanelPage*>(parentWx);
		return page != nullptr ? page->GetSizer() : nullptr;
	}
	return wxDynamicCast(parentWx, wxSizer);
#else
	// dynamic_cast through the typedef upsets MSVC in some contexts;
	// spell out the concrete target here.
	return dynamic_cast<ibWebSizer*>(parentWx);
#endif
}

wxObject* GetSizerItemChildWx(ibVisualHost* visualHost, ibValueFrame* object)
{
	if (object == nullptr || object->GetChildCount() == 0) return nullptr;
	return visualHost->GetWxObject(object->GetChild(0));
}

} // namespace

ibValueSizerItem::ibValueSizerItem() : ibValueFrame()
{
}

void ibValueSizerItem::OnCreated(wxObject* wxobject, ibFrontendWindow* /*wxparent*/, ibVisualHost* visualHost, bool /*firstCreated*/)
{
	// Single body for both builds. Desktop's Detach + Add hack (wxSizer
	// has no in-place SetItemProportion) lives inside
	// ibValueSizer::SetChildSizerParams; web just forwards to
	// ibWebSizer::UpdateItemParams.
	if (wxobject == nullptr || visualHost == nullptr) return;
	ibValueFrame* object = visualHost->GetObjectBase(wxobject);
	if (object == nullptr) return;

	ibFrontendSizer* sizer = ResolveParentSizer(visualHost, object);
	wxObject*        child = GetSizerItemChildWx(visualHost, object);
	if (sizer == nullptr || child == nullptr) {
		if (child == nullptr)
			wxLogError(wxT("The SizerItem component has no child - this should not be possible!"));
		return;
	}

	ibValueSizerItem* obj = dynamic_cast<ibValueSizerItem*>(object);
	if (obj == nullptr) return;

	ibSizerOps::SetChildParams(sizer, child,
		obj->GetProportion(),
		obj->GetFlagBorder() | obj->GetFlagState(),
		obj->GetBorder());
}

void ibValueSizerItem::OnUpdated(wxObject* wxobject, ibFrontendWindow* /*wxparent*/, ibVisualHost* visualHost)
{
	// Single body. Desktop additionally preserves child order via
	// Insert(idx, …) — idx comes from the ibValueFrame tree position.
	// Web ignores idx (ibWebSizer's m_items stays in walker order).
	if (wxobject == nullptr || visualHost == nullptr) return;
	ibValueFrame* object = visualHost->GetObjectBase(wxobject);
	if (object == nullptr) return;

	ibFrontendSizer* sizer = ResolveParentSizer(visualHost, object);
	wxObject*        child = GetSizerItemChildWx(visualHost, object);
	if (sizer == nullptr || child == nullptr) {
		if (child == nullptr)
			wxLogError(wxT("The SizerItem component has no child - this should not be possible!"));
		return;
	}

	ibValueSizerItem* obj = dynamic_cast<ibValueSizerItem*>(object);
	if (obj == nullptr) return;

	// Find the item's position in the parent's child list. wxNOT_FOUND
	// → append. SetChildSizerParams handles both (idx >= 0 → Insert
	// on desktop, ignored on web).
	int idx = -1;
	if (ibValueFrame* parentControl = GetParent()) {
		for (unsigned int i = 0; i < parentControl->GetChildCount(); i++) {
			if (m_controlId == parentControl->GetChild(i)->GetControlID()) {
				idx = static_cast<int>(i);
				break;
			}
		}
	}

	ibSizerOps::SetChildParams(sizer, child,
		obj->GetProportion(),
		obj->GetFlagBorder() | obj->GetFlagState(),
		obj->GetBorder(),
		idx);

#ifndef OES_USE_WEB
	// NotebookPage layout fixup — after sizer mutation the notebook-
	// page child window needs a manual Layout to settle; no web analog.
	ibValueFrame* parent = object->GetParent();
	if (parent != nullptr && parent->GetClassName() == wxT("NotebookPage")) {
		if (auto* page = dynamic_cast<ibPanelPage*>(visualHost->GetWxObject(parent)))
			page->Layout();
	}
#endif
}

#include "backend/metaData.h"

const ibMetaData* ibValueSizerItem::GetMetaData() const
{
	const ibValueMetaObjectFormBase* metaFormObject = m_formOwner ?
		m_formOwner->GetFormMetaObject() :
		nullptr;

	//for form buider
	if (metaFormObject == nullptr) {
		ibSourceDataObject* srcValue = m_formOwner ?
			m_formOwner->GetSourceObject() :
			nullptr;
		if (srcValue != nullptr) {
			const ibValueMetaObjectGenericData* metaValue = srcValue->GetSourceMetaObject();
			wxASSERT(metaValue);
			return metaValue->GetMetaData();
		}
	}

	return metaFormObject ?
		metaFormObject->GetMetaData() :
		nullptr;
}

#include "backend/metaCollection/metaFormObject.h"

ibFormID ibValueSizerItem::GetTypeForm() const
{
	if (!m_formOwner) {
		wxASSERT(m_formOwner);
		return 0;
	}

	const ibValueMetaObjectFormBase* metaFormObj =
		m_formOwner->GetFormMetaObject();
	wxASSERT(metaFormObj);

	return metaFormObj->GetTypeForm();
}

//**********************************************************************************
//*                                    Data										   *
//**********************************************************************************

bool ibValueSizerItem::ReadData(const ibDataNode& node)
{
	//m_propertyProportion->ReadNodeValue(node.GetProperty(m_propertyProportion->GetName()));
	//m_propertyFlagBorder->ReadNodeValue(node.GetProperty(m_propertyFlagBorder->GetName()));
	//m_propertyFlagState->ReadNodeValue(node.GetProperty(m_propertyFlagState->GetName()));
	//m_propertyBorder->ReadNodeValue(node.GetProperty(m_propertyBorder->GetName()));

	m_propertyProportion->ReadNodeValue(node.GetProperty(m_propertyProportion->GetName()));
	//m_propertyFlagBorder->ReadNodeValue(node.GetProperty(m_propertyFlagBorder->GetName()));

	m_propertyFlagBorderLeft->ReadNodeValue(node.GetProperty(m_propertyFlagBorderLeft->GetName()));
	m_propertyFlagBorderRight->ReadNodeValue(node.GetProperty(m_propertyFlagBorderRight->GetName()));
	m_propertyFlagBorderTop->ReadNodeValue(node.GetProperty(m_propertyFlagBorderTop->GetName()));
	m_propertyFlagBorderBottom->ReadNodeValue(node.GetProperty(m_propertyFlagBorderBottom->GetName()));

	m_propertyFlagState->ReadNodeValue(node.GetProperty(m_propertyFlagState->GetName()));
	m_propertyBorder->ReadNodeValue(node.GetProperty(m_propertyBorder->GetName()));

	return ibValueFrame::ReadData(node);
}

bool ibValueSizerItem::WriteData(ibDataNode& node) const
{
	//node.SetProperty(m_propertyProportion->GetName(), m_propertyProportion->GetNodeValue());
	//node.SetProperty(m_propertyFlagBorder->GetName(), m_propertyFlagBorder->GetNodeValue());
	//node.SetProperty(m_propertyFlagState->GetName(), m_propertyFlagState->GetNodeValue());
	//node.SetProperty(m_propertyBorder->GetName(), m_propertyBorder->GetNodeValue());

	node.SetProperty(m_propertyProportion->GetName(), m_propertyProportion->GetNodeValue());
	//node.SetProperty(m_propertyFlagBorder->GetName(), m_propertyFlagBorder->GetNodeValue());

	node.SetProperty(m_propertyFlagBorderLeft->GetName(), m_propertyFlagBorderLeft->GetNodeValue());
	node.SetProperty(m_propertyFlagBorderRight->GetName(), m_propertyFlagBorderRight->GetNodeValue());
	node.SetProperty(m_propertyFlagBorderTop->GetName(), m_propertyFlagBorderTop->GetNodeValue());
	node.SetProperty(m_propertyFlagBorderBottom->GetName(), m_propertyFlagBorderBottom->GetNodeValue());

	node.SetProperty(m_propertyFlagState->GetName(), m_propertyFlagState->GetNodeValue());
	node.SetProperty(m_propertyBorder->GetName(), m_propertyBorder->GetNodeValue());

	return ibValueFrame::WriteData(node);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

S_CONTROL_TYPE_REGISTER(ibValueSizerItem, "SizerItem", "Sizer", control_to_clsid("CT_SIZR"));
