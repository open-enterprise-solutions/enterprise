////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : base control
////////////////////////////////////////////////////////////////////////////

#include "control.h"
#include "form.h"
#include "backend/compiler/procUnit.h"
#include "backend/serialize/dataBuilder.h"   // ibDataNode / ibDataBuilder / ibBinaryProvider

#ifdef OES_USE_WEB
#include <iostream>
#endif


//*************************************************************************
//*                          ValueControl		                          *
//*************************************************************************

ibValueFrame::ibValueFrame() : ibValueDynamicMembers(ibValueTypes::TYPE_VALUE),
m_valEventContainer(new ibValueEventContainer(this)),
m_controlId(0), m_controlGuid(ibGuid::newGuid())
{
	m_members.Bind(this, &ibValueFrame::FillMembers);
}

ibValueFrame::~ibValueFrame()
{
}

wxString ibValueFrame::GetClassName() const
{
	const ibClassID& clsid = GetClassType();
	if (clsid == 0)
		return _("Class not registered");

	const ibCtorAbstractType* typeCtor = ibValue::GetAvailableCtor(clsid);
	if (typeCtor != nullptr)
		return typeCtor->GetClassName();
	return _("Class not registered");
}

wxString ibValueFrame::GetObjectTypeName() const
{
	const ibCtorControlTypeBase* typeCtor =
		static_cast<const ibCtorControlTypeBase*>(ibValue::GetAvailableCtor(typeid(*this)));

	if (typeCtor != nullptr)
		return typeCtor->GetTypeControlName();
	return _("Class not registered");
}

#define	headerBlock 0x012230
#define	dataBlock 0x012250
#define	childBlock 0x012270
#define	frameBlock 0x012290

bool ibValueFrame::IsEditable() const
{
	const ibValueForm* handler = GetOwnerForm();
	if (handler != nullptr)
		return handler->IsEditable();
	return false;
}

// The control is serialized as a node tree (header + per-type WriteData), rendered to
// the form buffer by the binary provider — the same path metaobjects use.
bool ibValueFrame::LoadControl(const ibValueMetaObjectFormBase* metaForm, ibReaderMemory& dataReader)
{
	ibDataBuilder builder;
	if (!builder.Load(ibBinaryProvider(), dataReader))
		return false;
	return LoadNode(builder.Root());
}

bool ibValueFrame::SaveControl(const ibValueMetaObjectFormBase* metaForm, ibWriterMemory& dataWritter, bool copy_form) const
{
	ibDataBuilder builder;
	if (!SaveNode(builder.Root()))
		return false;
	return builder.Save(ibBinaryProvider(), dataWritter);
}

// control header (id / name / expanded as fields) + per-type Read/WriteData, then the
// sub-controls recurse as node CHILDREN — so a control's node IS its whole subtree and
// the form is one transparent node tree (the provider frames each child in kChildBlock).
bool ibValueFrame::LoadNode(const ibDataNode& node)
{
	m_controlId = (ibFormID)node.GetValue<s32>(wxT("ControlId"));
	SetControlName(node.GetValue<wxString>(wxT("Name")));
	m_expanded = node.GetValue<bool>(wxT("Expanded"));
	if (!ReadData(node))
		return false;

	// Re-create each sub-control through the owning form's factory (NewObject attaches it
	// to this parent and stamps the owner form), then recurse into its own subtree.
	ibValueForm* ownerForm = GetOwnerForm();
	if (ownerForm != nullptr) {
		for (const ibDataNode& childNode : node.Children()) {
			ibValueFrame* child = ownerForm->NewObject(childNode.GetClsid(), this, false);
			if (child == nullptr)
				continue;
			if (!child->LoadNode(childNode))
				return false;
		}
	}
	return true;
}

bool ibValueFrame::SaveNode(ibDataNode& node) const
{
	node.SetValue(wxT("ControlId"), (s32)m_controlId);
	node.SetValue(wxT("Name"),      GetControlName());
	node.SetValue(wxT("Expanded"),  m_expanded);
	if (!WriteData(node))
		return false;

	// clsid = control type, metaId = control id — the load side recreates by clsid.
	for (unsigned int idx = 0; idx < GetChildCount(); idx++) {
		ibValueFrame* child = GetChild(idx);
		if (child == nullptr)
			continue;
		ibDataNode& childNode = node.AddChild(child->GetClassType(), child->GetControlID());
		if (!child->SaveNode(childNode))
			return false;
	}
	return true;
}

//*******************************************************************

bool ibValueFrame::Init()
{
	// always false
	return false;
}

bool ibValueFrame::Init(ibValue** paParams, const long lSizeArray)
{
	if (lSizeArray < 2)
		return false;
	ibValueForm* ownerForm = nullptr;
	ibValueFrame* controlParent = nullptr;
	if (paParams[0]->ConvertToValue(ownerForm) &&
		paParams[1]->ConvertToValue(controlParent)) {
		if (controlParent != nullptr) {
			controlParent->AddChild(this);
			SetParent(controlParent);
		}
		SetOwnerForm(ownerForm);
		ownerForm->ResolveNameConflict(this);
		if (paParams[2]->GetBoolean()) {
			GenerateNewID();
		}
		return true;
	}
	return false;
}

//*******************************************************************

bool ibValueFrame::ChangeChildPosition(ibValueFrame* obj, unsigned int pos)
{
	OnChangeChildPosition(obj, pos);
	return ibPropertyObjectHelper::ChangeChildPosition(obj, pos);
}

//*******************************************************************

ibValueFrame* ibValueFrame::CreatePasteObject(const ibReaderMemory& reader,
	ibValueForm* dstForm, ibValueFrame* dstParent)
{
	std::shared_ptr <ibReaderMemory>readerHeaderMemory(reader.open_chunk(headerBlock));
	if (readerHeaderMemory == nullptr)
		return nullptr;

	const ibVersionID& version = readerHeaderMemory->r_s32();
	const ibClassID& clsid = readerHeaderMemory->r_u64();

	return dstForm->NewObject(clsid, dstParent);
}

//*******************************************************************

bool ibValueFrame::CopyObject(ibWriterMemory& writer) const
{
	ibWriterMemory writerHeaderMemory;
	writerHeaderMemory.w_s32(0); //reserved
	writerHeaderMemory.w_u64(GetClassType()); //get class type 
	writer.w_chunk(headerBlock, writerHeaderMemory.pointer(), writerHeaderMemory.size());
	ibWriterMemory writerDataMemory;
	if (!CopyProperty(writerDataMemory))
		return false;
	writer.w_chunk(dataBlock, writerDataMemory.pointer(), writerDataMemory.size());
	ibWriterMemory writerChildMemory;
	for (unsigned int idx = 0; idx < GetChildCount(); idx++) {
		ibWriterMemory writerMemory;
		ibValueFrame* obj = GetChild(idx);
		if (!obj->CopyObject(writerMemory))
			return false;
		writerChildMemory.w_chunk(obj->GetClassType(), writerMemory.pointer(), writerMemory.size());
	}
	writer.w_chunk(childBlock, writerChildMemory.pointer(), writerChildMemory.size());
	return true;
}

bool ibValueFrame::PasteObject(ibReaderMemory& reader)
{
	ibValueForm* valueForm = GetOwnerForm();
	if (valueForm == nullptr) return false;

	std::shared_ptr <ibReaderMemory>readerHeaderMemory(reader.open_chunk(headerBlock));

	const ibVersionID& version = readerHeaderMemory->r_s32(); //reserved
	const ibClassID& clsid = readerHeaderMemory->r_u64();

	std::shared_ptr <ibReaderMemory> readerChildMemory(reader.open_chunk(childBlock));
	if (readerChildMemory != nullptr) {
		ibReaderMemory* prevReaderMemory = nullptr;
		do {
			ibClassID founded_clsid = 0;
			ibReaderMemory* readerMemory = readerChildMemory->open_chunk_iterator(founded_clsid, &*prevReaderMemory);
			if (readerMemory == nullptr)
				break;
			if (founded_clsid > 0) {
				ibValueFrame* valueFrame = valueForm->NewObject(founded_clsid, this, false);
				if (valueFrame != nullptr && !valueFrame->PasteObject(*readerMemory))
					return false;
			}
			prevReaderMemory = readerMemory;
		} while (true);
	}

	std::shared_ptr <ibReaderMemory>readerDataMemory(reader.open_chunk(dataBlock));
	if (!PasteProperty(*readerDataMemory))
		return false;

	valueForm->ResolveNameConflict(this);
	return true;
}

std::shared_ptr<ibProcUnit> ibValueFrame::GetFormProcUnit() const
{
	const ibValueForm* valueForm =
		GetOwnerForm();

	if (valueForm == nullptr) 
		return nullptr;

	return valueForm->GetProcUnit();
}

//*******************************************************************

#include "backend/metaData.h"
#include "backend/objCtor.h"

bool ibValueFrame::HasQuickChoice() const {
	const ibMetaData* metaData = GetMetaData();
	if (metaData == nullptr)
		return false;
	ibValue selValue; GetControlValue(selValue);
	const ibCtorAbstractType* so = metaData->GetAvailableCtor(selValue.GetClassType());
	if (so != nullptr && so->GetObjectTypeCtor() == ibCtorObjectType_object_primitive) {
		return true;
	}
	else if (so != nullptr && so->GetObjectTypeCtor() == ibCtorObjectType_object_meta_value) {
		const ibCtorMetaValueType* meta_so = dynamic_cast<const ibCtorMetaValueType*>(so);
		if (meta_so != nullptr) {
			const ibValueMetaObjectRecordDataRef* metaObject = dynamic_cast<const ibValueMetaObjectRecordDataRef*>(meta_so->GetMetaObject());
			if (metaObject != nullptr)
				return metaObject->HasQuickChoice();
		}
	}
	return false;
}

//*******************************************************************

ibBackendValueForm* ibValueFrame::GetBackendForm() const
{
	return GetOwnerForm();
}

//*******************************************************************

ibFormVisualDocument* ibValueFrame::GetVisualDocument() const
{
	ibValueForm* const valueForm = GetOwnerForm();
	if (valueForm == nullptr)
		return nullptr;
	return valueForm->GetVisualDocument();
}

#ifndef OES_USE_WEB
ibFrontendVisualEditorNotebook* ibValueFrame::FindVisualEditor() const
{
	return ibFrontendVisualEditorNotebook::FindEditorByForm(GetOwnerForm());
}
#endif

//*******************************************************************
//*                          Runtime                                *
//*******************************************************************

#include "frontend/visualView/visualHostClient.h"

wxObject* ibValueFrame::GetWxObject() const
{
	// Unified path: form в†’ visualDoc в†’ view в†’ host в†’ GetWxObject(this).
	// Web's ibVisualHostClient also populates m_baseObjects (walker
	// inserts ibWebWindow* per ibValueFrame), so the same lookup works
	// on both builds. Previously web returned nullptr unconditionally,
	// which silently broke SetControlValue's editor-poke — Clear couldn't
	// reach the ibWebTextCtrl to reset m_value (2026-04-19).
	ibValueForm* const valueForm = GetOwnerForm();
	if (valueForm != nullptr) {

		ibFormVisualDocument* const visualDoc = valueForm->GetVisualDocument();
		if (visualDoc != nullptr) {

			const ibFormVisualEditView* visualView = visualDoc->GetFirstView();
			const ibVisualHost* visualHost = visualView ?
				visualView->GetVisualHost() : nullptr;

			if (visualHost == nullptr)
				return nullptr;

			return visualHost->GetWxObject((ibValueFrame*)this);

		}
#ifndef OES_USE_WEB
		else if (g_visualHostContext != nullptr) {

			//if run designer form search in own visualHost
			const ibVisualHost* visualHost =
				g_visualHostContext->GetVisualHost();

			return visualHost->GetWxObject((ibValueFrame*)this);
		}
#endif
	}

	return nullptr;
}

#include "backend/metaCollection/partial/commonObject.h"

//****************************************************************************
//*                              Support methods                             *
//****************************************************************************

void ibValueFrame::FillMembers(ibMemberTable& helper) const
{
	{
		wxString propertyName;
		for (unsigned int idx = 0; idx < ibPropertyObject::GetPropertyCount(); idx++) {
			ibProperty* property = ibPropertyObject::GetProperty(idx);
			if (property == nullptr)
				continue;
			property->GetName(propertyName);
			helper.AppendProp(propertyName, idx, eProperty);
		}
		//if we have sizerItem then call him
		ibValueFrame* sizeritem = GetParent();
		if (sizeritem != nullptr && sizeritem->GetComponentType() == COMPONENT_TYPE_SIZERITEM) {
			for (unsigned int idx = 0; idx < sizeritem->GetPropertyCount(); idx++) {
				ibProperty* property = sizeritem->GetProperty(idx);
				if (property == nullptr)
					continue;
				property->GetName(propertyName);
				helper.AppendProp(propertyName, idx, eSizerItem);
			}
		}
	}

	helper.AppendProp(wxT("Events"), true, false, 0, eEvent);
}

bool ibValueFrame::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	const long lPropAlias = m_members.GetPropAlias(lPropNum);
	if (lPropAlias == eProperty) {
		unsigned int idx = m_members.GetPropData(lPropNum);
		ibProperty* property = GetPropertyByIndex(idx);
		if (property != nullptr)
			property->SetDataValue(varPropVal);
	}
	else if (lPropAlias == eSizerItem) {
		//if we have sizerItem then call him savepropery 
		ibValueFrame* sizerItem = GetParent();
		if (sizerItem != nullptr &&
			sizerItem->GetComponentType() == COMPONENT_TYPE_SIZERITEM) {
			ibProperty* property = sizerItem->GetPropertyByIndex(lPropNum);
			if (property != nullptr)
				property->SetDataValue(varPropVal);
		}
	}

	ibValueForm* ownerForm = GetOwnerForm();
	if (ownerForm == nullptr)
		return false;

#ifndef OES_USE_WEB
	// Live-update path: find the existing wxWindow for this control,
	// re-apply properties via Update(), re-layout the parent. Web path
	// rebuilds from scratch on each HTTP response (stateless render),
	// so there's no live object to poke — return true and let the next
	// ClearVisualHost+CreateVisualHost pass do the work.
	ibFormVisualDocument* visualDoc = ownerForm->GetVisualDocument();
	if (visualDoc != nullptr) {

		ibVisualHostClient* visualView = visualDoc->GetFirstView() ?
			visualDoc->GetFirstView()->GetVisualHost() : nullptr;

		if (visualView == nullptr)
			return false;

		wxObject* wxobject = visualView->GetWxObject(this);

		if (wxobject != nullptr) {
			wxWindow* wxparent = nullptr;
			Update(wxobject, visualView);
			ibValueFrame* nextParent = GetParent();
			while (wxparent == nullptr && nextParent != nullptr) {
				if (nextParent->GetComponentType() == COMPONENT_TYPE_WINDOW) {
					wxObject* wxobject = visualView->GetWxObject(nextParent);
					wxparent = dynamic_cast<wxWindow*>(wxobject);
					break;
				}
				nextParent = nextParent->GetParent();
			}
			if (wxparent == nullptr) wxparent = visualView->GetBackgroundWindow();
			OnUpdated(wxobject, wxparent, visualView);
			if (wxparent != nullptr) wxparent->Layout();
		}
	}
#endif

	return true;
}

bool ibValueFrame::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	const long lPropAlias = m_members.GetPropAlias(lPropNum);
	if (lPropAlias == eSizerItem) {
		//if we have sizerItem then call him savepropery 
		ibValueFrame* sizerItem = GetParent();
		if (sizerItem != nullptr &&
			sizerItem->GetComponentType() == COMPONENT_TYPE_SIZERITEM) {
			unsigned int idx = m_members.GetPropData(lPropNum);
			ibProperty* property = sizerItem->GetPropertyByIndex(idx);
			if (property != nullptr)
				return property->GetDataValue(pvarPropVal);
			return false;
		}
	}
	else if (lPropAlias == eEvent) {
		pvarPropVal = m_valEventContainer;
		return true;
	}
	else {
		unsigned int idx = m_members.GetPropData(lPropNum);
		ibProperty* property = GetPropertyByIndex(idx);
		if (property != nullptr)
			return property->GetDataValue(pvarPropVal);
		return false;
	}

	return false;
}