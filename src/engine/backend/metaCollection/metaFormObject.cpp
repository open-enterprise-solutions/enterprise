////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metaform object
////////////////////////////////////////////////////////////////////////////

#include "metaFormObject.h"
#include "backend/serialize/dataBuilder.h"
#include "backend/metaData.h"
#include "backend/metaCollection/partial/commonObject.h"
#include "backend/appData.h"
#include "backend/backend_metatree.h"
#include "backend/system/systemManager.h"

// ibDeferredForm impl — defined here where ibValueMetaObjectGenericData
// (parent's CreateObjectForm) and formWrapper are fully visible.
ibValue* ibDeferredForm::Construct() const
{
	if (m_form == nullptr || !m_build)
		return nullptr;
	// PURE lazy build — no metadata side effect. The re-home is automatic: m_build() reads the control blob through
	// LoadControl, which routes to PasteNode by the blob's OWN self-describing tag (PasteFormat, stamped by SaveControl
	// at copy time), independent of any live paste mark → each guid source hop re-homes onto the pasted object via
	// GetIdByGuid. A raw blob (no tag) reads plainly via LoadNode.
	return formWrapper::inl::cast_value(m_build());
}

// -----------------------------------------------------------------------
// ibBackendCommandItem
// -----------------------------------------------------------------------

#include "backend/system/systemManager.h"

bool ibBackendCommandItem::Execute(ibInterfaceCommandType cmdType, ibBackendValueForm* /*srcForm*/, ibValue* /*commandParameter*/) const
{
	ibBackendValueForm* valueForm = nullptr;

	try {

		valueForm = GetFormByCommandType(cmdType);

		if (valueForm == nullptr)
			return false;   // default (form) behaviour; a command OVERRIDES Execute to run its handler

		valueForm->ShowForm();
	}
	catch (const ibBackendAccessException& err) {
		wxDELETE(valueForm);
		ibValueSystemFunction::Alert(err.GetErrorDescription());
		return false;
	}
	catch (const ibBackendException&) {
		wxDELETE(valueForm);
		return false;
	}

	return true;
}

// -----------------------------------------------------------------------
// ibValueMetaObjectFormBase
// -----------------------------------------------------------------------


//***********************************************************************
//*                          common value object                        *
//***********************************************************************

bool ibValueMetaObjectFormBase::LoadFormData(ibBackendValueForm* valueForm) const {
	return valueForm->LoadForm(GetFormData());
}

bool ibValueMetaObjectFormBase::SaveFormData(ibBackendValueForm* valueForm) {
	wxMemoryBuffer memoryBuffer;
	if (valueForm->SaveForm(memoryBuffer)) {
		SetFormData(memoryBuffer);
		// Designer's form-edit commit — invalidate this form's compile-cache
		// entry so the next FindCompileModule rebuilds the form value via the
		// stored ibDeferredForm rebuilder. Caller is always the visual form
		// editor; runtime never reaches here.
		if (auto* cc = m_metaData ? m_metaData->GetCompileCache() : nullptr)
			cc->InvalidateCompileModule(this);
		return !memoryBuffer.IsEmpty();
	}
	return false;   // form refused to serialize (e.g. a dynamic list with no queryable source) — don't commit
}

//***********************************************************************

#pragma region _form_creator_h_

ibBackendValueForm* ibValueMetaObjectFormBase::CreateAndBuildForm(const ibValueMetaObjectFormBase* creator,
	ibBackendControlFrame* ownerControl, ibSourceDataObject* srcObject, const ibUniqueKey& formGuid)
{
	return CreateAndBuildForm(creator,
		creator != nullptr ? creator->GetTypeForm() : defaultFormType, ownerControl, srcObject, formGuid);
}

ibBackendValueForm* ibValueMetaObjectFormBase::CreateAndBuildForm(const ibValueMetaObjectFormBase* creator, const ibFormID& form_id,
	ibBackendControlFrame* ownerControl, ibSourceDataObject* srcObject, const ibUniqueKey& formGuid)
{
	ibBackendValueForm* result = nullptr;

	if (creator != nullptr) {
		const ibMetaData* metaData = creator->GetMetaData();
		wxASSERT(metaData);
		auto* cc = metaData->GetCompileCache();
		const bool foundCached = cc && cc->FindCompileModule(creator, result);
		if (!foundCached) {
			result = ibBackendValueForm::CreateNewForm(creator, ownerControl, srcObject, formGuid);
			if (!creator->GetFormData().IsEmpty() && !creator->LoadFormData(result)) {
				wxDELETE(result);
				return nullptr;
			}
			else if (creator->GetFormData().IsEmpty()) {
				result->BuildForm(form_id);
			}
		}
	}
	else {
		result = ibBackendValueForm::CreateNewForm(nullptr, ownerControl, srcObject, formGuid);
		result->BuildForm(form_id);
	}

	if (result != nullptr) {

		bool success = true;

		try {
			success = result->InitializeFormModule();
		}
		catch (const ibBackendException& err) {
			// Surface it — don't let a form-module compile/run error or a missing
			// required binding vanish (the old catch(...) made the form silently
			// fail to open with no clue why).
			ibValueSystemFunction::Message(err.GetErrorDescription(),
				ibStatusMessage::ibStatusMessage_Error);
			success = false;
		}
		catch (...) {
			success = false;
		}

		if (!success) {
			wxDELETE(result);
			return nullptr;
		}

		if (srcObject != nullptr) result->Modify(srcObject->IsModified());
	}

	return result;
}

#pragma endregion

///////////////////////////////////////////////////////////////////////////

// node <-> runtime-blob shim. The form blob IS the binary-provider node format (each
// control's SaveNode subtree), so the adapter is a straight provider round-trip.
ibDataValue ibValueMetaObjectFormBase::FormBlobToNode(const wxMemoryBuffer& blob)
{
	if (blob.GetDataLen() == 0)
		return ibDataValue();
	auto node = std::make_shared<ibDataNode>();
	ibReaderMemory reader(blob);
	ibBinaryProvider().Read(reader, *node);
	return ibDataValue::Child(node);
}

wxMemoryBuffer ibValueMetaObjectFormBase::FormNodeToBlob(const ibDataValue& formNode)
{
	if (formNode.Kind() != ibDataKind::Child)
		return wxMemoryBuffer();
	const std::shared_ptr<ibDataNode>& node = formNode.AsChild();
	if (!node)
		return wxMemoryBuffer();
	ibWriterMemory writer;
	ibBinaryProvider().Write(*node, writer);
	return writer.buffer();
}

// The LIVE form's control tree AS a transparent node (Child) — the blob never hits disk. The form metaobject (this)
// is in COPY mode here (ibControlCopyGuard stamped it), so SaveForm routes SaveControl → CopyNode: the source hops
// ride their metaobject copy-guids (the copy/paste binary), so a paste re-homes them onto the pasted objects.
ibDataValue ibValueMetaObjectFormBase::CopyFormData() const
{
	ibBackendValueForm* valueForm = nullptr;
	auto* cc = m_metaData->GetCompileCache();
	if (cc && cc->FindCompileModule(this, valueForm)) {
		wxMemoryBuffer blob;
		if (valueForm->SaveForm(blob))
			return FormBlobToNode(blob);
	}
	return ibDataValue();
}

// Intentional no-op. The paste's own work — storing the copy/paste blob — is done by ibPropertyForm::PasteNodeValue
// (it materialises the node into the form-data cell). The RE-HOMING happens later, on first form access: the form
// object does not exist yet here, so ibDeferredForm re-arms the paste mark and the deferred build reads the blob via
// PasteNode. Kept as the symmetric hook to CopyFormData / an extension point.
bool ibValueMetaObjectFormBase::PasteFormData()
{
	return true;
}

///////////////////////////////////////////////////////////////////////////

ibValueMetaObjectFormBase::ibValueMetaObjectFormBase(const wxString& name, const wxString& synonym, const wxString& comment) :
	ibValueMetaObjectModuleBase(name, synonym, comment)
{
	//set default proc
	SetDefaultProcedure(wxT("BeforeOpen"), ibContentHelper::eProcedureHelper, { wxT("Cancel") });
	SetDefaultProcedure(wxT("OnOpen"), ibContentHelper::eProcedureHelper);
	SetDefaultProcedure(wxT("BeforeClose"), ibContentHelper::eProcedureHelper, { wxT("Cancel") });
	SetDefaultProcedure(wxT("OnClose"), ibContentHelper::eProcedureHelper);

	SetDefaultProcedure(wxT("OnReOpen"), ibContentHelper::eProcedureHelper);
	SetDefaultProcedure(wxT("ChoiceProcessing"), ibContentHelper::eProcedureHelper, { { wxT("SelectedValue") }, { wxT("ChoiceSource") } });
	SetDefaultProcedure(wxT("RefreshDisplay"), ibContentHelper::eProcedureHelper);
}


//***********************************************************************
//*                            Metaform                                 *
//***********************************************************************

ibValueMetaObjectForm::ibValueMetaObjectForm(const wxString& name, const wxString& synonym, const wxString& comment) : ibValueMetaObjectFormBase(name, synonym, comment)
{
}

bool ibValueMetaObjectForm::ReadData(const ibDataNode& node)
{
	m_properyFormType->ReadNodeValue(node.GetProperty(m_properyFormType->GetName()));
	m_propertyForm->ReadNodeValue(node.GetProperty(m_propertyForm->GetName()));
	return true;
}

bool ibValueMetaObjectForm::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_properyFormType->GetName(), m_properyFormType->GetNodeValue());
	node.SetProperty(m_propertyForm->GetName(), m_propertyForm->GetNodeValue());
	return true;
}

//***********************************************************************

bool ibValueMetaObjectForm::FillGenericFormType(ibPropertyList* prop)
{
	const ibValueMetaObjectGenericData* geneticObject = m_parent != nullptr ?
		m_parent->ConvertToType<ibValueMetaObjectGenericData>() : nullptr;

	if (geneticObject != nullptr) {

		ibFormTypeList formList = geneticObject->GetFormType();
		for (unsigned int idx = 0; idx < formList.GetItemCount(); idx++) {
			prop->AppendItem(
				formList.GetItemName(idx),
				formList.GetItemLabel(idx),
				formList.GetItemHelp(idx),
				formList.GetItemId(idx),
				GetIcon(),
				formList.GetItemName(idx)
			);
		}

		return true;
	}

	return false;
}

//***********************************************************************
//*                             event object                            *
//***********************************************************************

bool ibValueMetaObjectForm::OnCreateMetaObject(ibMetaData* metaData, int flags)
{
	if (!ibValueMetaObjectFormBase::OnCreateMetaObject(metaData, flags))
		return false;

	if ((flags & newObjectFlag) != 0) {

		ibValueMetaObjectGenericData* metaObject = dynamic_cast<ibValueMetaObjectGenericData*>(m_parent);

		wxASSERT(metaObject);

		ibFormID res = wxNOT_FOUND;
		if (metaData != nullptr) {
			ibBackendMetadataTree* metaTree = metaData->GetMetaTree();
			if (metaTree != nullptr) {
				res = metaTree->SelectFormType(this);
			}
		}
		if (res != wxNOT_FOUND) {
			m_properyFormType->SetValue(res);
		}
		else {
			return false;
		}
		metaObject->OnCreateFormObject(this);
		if (metaData != nullptr) {
			ibBackendMetadataTree* metaTree = metaData->GetMetaTree();
			if (metaTree != nullptr) {
				metaTree->UpdateChoiceSelection();
			}
		}
	}

	return true;
}

bool ibValueMetaObjectForm::OnLoadMetaObject(ibMetaData* metaData)
{
	return ibValueMetaObjectFormBase::OnLoadMetaObject(metaData);
}

bool ibValueMetaObjectForm::OnSaveMetaObject(int flags)
{
	return ibValueMetaObjectFormBase::OnSaveMetaObject(flags);
}

bool ibValueMetaObjectForm::OnDeleteMetaObject()
{
	ibValueMetaObjectGenericData* metaObject = dynamic_cast<ibValueMetaObjectGenericData*>(m_parent);
	wxASSERT(metaObject);
	metaObject->OnRemoveMetaForm(this);

	return ibValueMetaObjectFormBase::OnDeleteMetaObject();
}

ibBackendValueForm* ibValueMetaObjectForm::GetObjectForm(ibBackendControlFrame* ownerControl, const ibUniqueKey& formGuid) const
{
	// The owner is what knows the source a form of this kind must be bound to (and it checks
	// the access right on the way, exactly as the common form's own answer does). A form with
	// no owning object cannot be materialised at all.
	const ibValueMetaObjectGenericData* const owner =
		dynamic_cast<const ibValueMetaObjectGenericData*>(GetParent());

	return owner != nullptr ? owner->CreateObjectForm(this, formGuid) : nullptr;
}

bool ibValueMetaObjectForm::OnBeforeRunMetaObject(int flags)
{
	return ibValueMetaObjectFormBase::OnBeforeRunMetaObject(flags);
}

bool ibValueMetaObjectForm::OnAfterRunMetaObject(int flags)
{
	if (auto* cc = m_metaData->GetCompileCache()) {

		ibValueMetaObjectGenericData* metaObject = dynamic_cast<ibValueMetaObjectGenericData*>(m_parent);
		wxASSERT(metaObject);

		// Defer the actual form build — passing an eager CreateObjectForm
		// result here would need session->m_root compiled, but CompileRoot
		// only fires after RunDatabase finishes. The cache stores a builder
		// and materializes it on first FindCompileModule lookup.
		// The build is DEFERRED but the paste RE-HOME is not: ibValueMetaObject::PasteObject forces this build at the
		// end of the paste, while the metaobject's paste mark is still live, so Construct re-homes the form's source
		// hops onto the pasted objects and normalizes the stored blob. A later lazy build just reads the raw blob.
		return cc->AddCompileModule(this, [deferred = ibDeferredForm(this, [metaObject, this]() -> ibBackendValueForm* {
				// Keyed by the METAFORM: this value IS the compile cache's, one per metaform.
				return metaObject->CreateObjectForm(this);
			})]() -> ibValue* {
			return deferred.Construct();
			});
	}

	return ibValueMetaObjectFormBase::OnAfterRunMetaObject(flags);
}

bool ibValueMetaObjectForm::OnBeforeCloseMetaObject()
{

	if (auto* cc = m_metaData->GetCompileCache()) {
		if (!cc->RemoveCompileModule(this))
			return false;
	}

	return ibValueMetaObjectFormBase::OnBeforeCloseMetaObject();
}

bool ibValueMetaObjectForm::OnAfterCloseMetaObject()
{
	return ibValueMetaObjectFormBase::OnAfterCloseMetaObject();
}

//***********************************************************************
//*                           CommonFormObject metaData                 *
//***********************************************************************


ibValueMetaObjectCommonForm::ibValueMetaObjectCommonForm(const wxString& name, const wxString& synonym, const wxString& comment) : ibValueMetaObjectFormBase(name, synonym, comment) {}

bool ibValueMetaObjectCommonForm::ReadData(const ibDataNode& node)
{
	m_propertyForm->ReadNodeValue(node.GetProperty(m_propertyForm->GetName()));
	return true;
}

bool ibValueMetaObjectCommonForm::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyForm->GetName(), m_propertyForm->GetNodeValue());
	return true;
}

#include "backend/system/systemManager.h"

ibBackendValueForm* ibValueMetaObjectCommonForm::GetObjectForm(ibBackendControlFrame* ownerControl, const ibUniqueKey& formGuid) const
{
	if (!AccessRight_Use()) {
		ibBackendAccessException::Error();
		return nullptr;
	}

	return ibValueMetaObjectFormBase::CreateAndBuildForm(
		this,
		ownerControl,
		nullptr,
		formGuid
	);
}

//***********************************************************************
//*                             event object                            *
//***********************************************************************

bool ibValueMetaObjectCommonForm::OnCreateMetaObject(ibMetaData* metaData, int flags)
{
	return ibValueMetaObjectModuleBase::OnCreateMetaObject(metaData, flags);
}

bool ibValueMetaObjectCommonForm::OnBeforeRunMetaObject(int flags)
{
	return ibValueMetaObjectModuleBase::OnBeforeRunMetaObject(flags);
}

bool ibValueMetaObjectCommonForm::OnAfterRunMetaObject(int flags)
{
	if (auto* cc = m_metaData->GetCompileCache()) {
		// DEFER the build (same as an object form) — do NOT build eagerly here. OnAfterRun fires per-object mid-run,
		// so an eager build would resolve the form's attribute types / source hops while metaobjects that register
		// LATER in the pass are still invisible → an object-typed attribute silently collapsed to String. The deferred
		// builder runs on first FindCompileModule, after the whole config has run and everything is registered. A
		// common form is standalone (no owning GenericData / source object), so it builds through CreateAndBuildForm
		// directly; the paste RE-HOME is forced by ibValueMetaObject::PasteObject at paste end (mark still live).
		if (cc->AddCompileModule(this, [deferred = ibDeferredForm(this, [this]() -> ibBackendValueForm* {
				return ibValueMetaObjectFormBase::CreateAndBuildForm(this, defaultFormType);
			})]() -> ibValue* {
			return deferred.Construct();
			})) {
			return ibValueMetaObjectModuleBase::OnAfterRunMetaObject(flags);
		}
		return false;
	}
	return ibValueMetaObjectModuleBase::OnAfterRunMetaObject(flags);
}

bool ibValueMetaObjectCommonForm::OnBeforeCloseMetaObject()
{
	if (auto* cc = m_metaData->GetCompileCache()) {
		if (cc->RemoveCompileModule(this)) {
			return ibValueMetaObjectModuleBase::OnBeforeCloseMetaObject();
		}
		return false;
	}

	return ibValueMetaObjectModuleBase::OnBeforeCloseMetaObject();
}

bool ibValueMetaObjectCommonForm::OnAfterCloseMetaObject()
{
	return ibValueMetaObjectModuleBase::OnAfterCloseMetaObject();
}


//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(ibValueMetaObjectForm, "Form", g_metaFormCLSID);
METADATA_TYPE_REGISTER(ibValueMetaObjectCommonForm, "CommonForm", g_metaCommonFormCLSID);