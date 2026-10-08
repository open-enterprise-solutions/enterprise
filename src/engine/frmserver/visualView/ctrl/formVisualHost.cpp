////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : form host
////////////////////////////////////////////////////////////////////////////

#include "form.h"
#include "formAttribute.h"                                        // ibFormAttributeValue COMPLETE — GetMainAttribute()->GetSourceValue()/GetTypeDesc()
#include "formCommand.h"                                           // ibFormCommandValue COMPLETE — a form command resolved BY ID (the walk's leaf)
#include "backend/metaCollection/partial/commonObject.h"
#include "frmserver/visualView/visualHost.h"
#include "backend/metaCollection/partial/reference/reference.h"   // ibValueReferenceDataObject::ConvertToMetaIds (main attr Type -> object)
#include "backend/metaData.h"                                      // FindAnyObjectByFilter (resolve the object metaobject)
#include "backend/metaCollection/metaCommandObject.h"              // ibValueMetaObjectCommand — command-capable leaf (GetCommandByHop)
#include "backend/metaCollection/metaObject.h"                     // g_metaCommonCommandCLSID / g_metaCommandCLSID

#include "backend/appData.h"

#define runFlag 0x000000
#define demoFlag 0x000001

//////////////////////////////////////////////////////////////

void ibValueForm::Modify(bool modify)
{
	if (IsShown()) {
		GetVisualDocument()->Modify(modify);
	}
	m_formModified = modify;
	// Its State says it, and the command bar what it lets through now.
	ibFormVisualDocument* const document = GetVisualDocument();
	if (ibFormVisualEditView* const host = document != nullptr ? document->GetFirstView() : nullptr)
		host->UpdateControl(this);
}

ibFormVisualDocument* ibValueForm::GetVisualDocument() const
{
	return ibFormVisualDocument::FindDocByUniqueKey(m_formKey);
}

void ibValueForm::RefreshForm()
{
	// What it shows may have changed anywhere — its data, what a handler made of it.
	ibFormVisualDocument* const document = GetVisualDocument();
	if (ibFormVisualEditView* const host = document != nullptr ? document->GetFirstView() : nullptr)
		host->UpdateControl(this);
	RefreshDocForm();
}

bool ibValueFrame::CallAsEvent(const ibEvent* event, ibValue** ppParams, const long lSizeArray) const
{
	if (event == nullptr)
		return false;
	ibEventDispatcher* dispatcher = event->GetDispatcher();
	if (dispatcher == nullptr || dispatcher->IsEmpty())
		return true;   // undefined -> no-op, the event just proceeds
	ibFormVisualDocument* const document = GetVisualDocument();
	if (ibFormVisualEditView* const host = document != nullptr ? document->GetFirstView() : nullptr)
		host->UpdateControl(GetOwnerForm());
	ibValue eventCancel = false;
	try {
		return dispatcher->Dispatch(GetFormProcUnit().get(), ppParams, lSizeArray, eventCancel);
	}
	catch (...) {
		return false;
	}
}

bool ibValueFrame::CallAsEvent(const wxString& functionName, ibValue** ppParams, const long lSizeArray) const
{
	std::shared_ptr<ibProcUnit> formProcUnit = GetFormProcUnit();
	if (formProcUnit == nullptr || functionName.IsEmpty())
		return true;
	ibFormVisualDocument* const document = GetVisualDocument();
	if (ibFormVisualEditView* const host = document != nullptr ? document->GetFirstView() : nullptr)
		host->UpdateControl(GetOwnerForm());
	try {
		formProcUnit->CallAsProc(functionName, ppParams, lSizeArray);
	}
	catch (...) {
		return false;
	}
	return true;
}

#include "frmserver/visualView/ctrl/typeControl.h"   // ibTypeControlFactory — a control's bound SOURCE head (command descend)

// A descendant control whose bound source PATH starts with `sourceId` — a command hop DESCENDS into it (a tablebox,
// itself command-capable). Recurses the frame tree; a control's source is its ibTypeControlFactory head.
static const ibValueFrame* FindControlBySourceHead(const ibValueFrame* root, const ibMetaID& sourceId)
{
	if (root == nullptr)
		return nullptr;
	if (const ibTypeControlFactory* factory = dynamic_cast<const ibTypeControlFactory*>(root)) {
		const std::vector<ibSourceHop>& path = factory->GetSourceDesc().GetPath();
		if (!path.empty() && path.front().m_id == sourceId)
			return root;
	}
	for (unsigned int i = 0; i < root->GetChildCount(); i++)
		if (const ibValueFrame* found = FindControlBySourceHead(root->GetChild(i), sourceId))
			return found;
	return nullptr;
}

const ibValueMetaObjectGenericData* ibValueForm::GetMetaObject() const
{
	// The form's data object is ALWAYS its MAIN attribute's object — the one flagged main. Nothing else, no
	// config-tree walk: the form asks its main attribute, exactly the "source OR metadata, no third variant" rule.
	ibFormAttributeValue* mainAttr = GetMainAttribute();
	if (mainAttr == nullptr)
		return nullptr;
	// (a) RUNTIME — the main holds a LIVE source that knows its metaobject directly.
	if (const ibSourceDataObject* src = mainAttr->GetSourceValue())
		if (const ibValueMetaObjectGenericData* obj = src->GetSourceMetaObject())
			return obj;
	// (b) DESIGNER — no live value; resolve the main's TYPE (an object reference) to the object metaobject through
	// the form's own config. Same resolve the attribute tree uses (ConvertToMetaIds) — metadata, not a live value.
	const ibMetaData* metaData = GetMetaData();
	if (metaData != nullptr) {
		const std::vector<ibMetaID> targets = ibValueReferenceDataObject::ConvertToMetaIds(mainAttr->GetTypeDesc().GetClsidList(), metaData);
		if (!targets.empty())
			return metaData->FindAnyObjectByFilter<ibValueMetaObjectGenericData>(targets.front(), true);
	}
	return nullptr;
}

// ibBackendCommandSender — the form is the ENTRY hop of a command path. Resolve the id to a config command /
// object command (a command METAOBJECT — itself command-capable, so a group / section then hops WITHIN itself),
// through the form's OWN metadata (never the global active tree). CONST: walking / executing never mutates — the
// command metaobject is const, Execute spawns a TRANSIENT runtime for the call. A form-command / action leaf is the
// front-end door's concern; here the form vends the METADATA commands (the "melting pot" of common + object commands).
bool ibValueForm::GetCommandByHop(const ibCommandHop& hop, ibValue& out)
{
	// (1) the form's OWN command (a form-local event) — the "current command of the form". Terminal (the caller runs
	// its Action). A form command is an ibValue (ibValueLayerObject), so it is a valid leaf of the walk.
	if (ibFormCommandValue* fc = FindFormCommandById(hop.m_id)) {
		out = static_cast<const ibValue*>(fc);   // non-owning view — the form owns the command
		return true;
	}
	// (2) else HAND CONTROL to the next hop: a config command / object command (a command metaobject — itself
	// command-capable, so a group / section then hops WITHIN itself), resolved through the form's OWN metadata.
	const ibMetaData* metaData = GetMetaData();
	if (metaData == nullptr)
		return false;
	if (const ibValueMetaObjectCommand* cmd = metaData->FindAnyObjectByFilter<ibValueMetaObjectCommand>(
			hop.m_id, { g_metaCommonCommandCLSID, g_metaCommandCLSID }, true)) {
		out = static_cast<const ibValue*>(cmd);   // const-ref to the command metaobject — itself command-capable
		return true;
	}
	// (3) an OBJECT ITEM (a catalog / register / constant checked into a section) — opens its own form by the desc's
	// command type. A command IS-A command-item too, so exclude commands by clsid (they went to (2)); the item is
	// the leaf (a business object, an ibBackendCommandItem).
	if (const ibValueMetaObject* obj = metaData->FindAnyObjectByFilter<ibValueMetaObject>(hop.m_id, true))
		if (obj->GetClassType() != g_metaCommonCommandCLSID && obj->GetClassType() != g_metaCommandCLSID
			&& dynamic_cast<const ibBackendCommandItem*>(obj) != nullptr) {
			out = static_cast<const ibValue*>(obj);
			return true;
		}
	// (4) DESCEND into a bound child control (a composite — a tablebox, itself command-capable; the next hop runs there).
	if (const ibValueFrame* child = FindControlBySourceHead(this, hop.m_id)) {
		out = static_cast<const ibValue*>(child);
		return true;
	}
	// (5) a standard ACTION on the FORM's OWN bus — the form is the action's runtime (the caller runs CallAsAction).
	auto actions = GetStandardCommands(GetTypeForm());
	if (!actions.GetNameByID((ibActionID)hop.m_id).IsEmpty()) {
		out = static_cast<const ibValue*>(this);
		return true;
	}
	return false;
}

//////////////////////////////////////////////////////////////

wxString ibValueForm::GetControlTitle() const
{
	if (m_propertyTitle->IsEmptyProperty()) {

		const ibValueMetaObjectGenericData* metaSource = GetMetaObject();
		if (metaSource != nullptr) return metaSource->GetSynonym();

		const ibValueMetaObjectFormBase* metaForm = GetFormMetaObject();
		if (metaForm != nullptr) return metaForm->GetSynonym();
	}

	return m_propertyTitle->GetValueAsTranslateString();
}

//////////////////////////////////////////////////////////////

#include "frmserver/docView/docView.h"

bool ibValueForm::CreateDocForm(ibDocument* docParent, bool createContext)
{
	ibFormVisualDocument* const visualFoundedDoc = GetVisualDocument();
	if (visualFoundedDoc != nullptr) {
		ActivateForm();
		return true;
	}

	if (createContext) {
		// "This one is already open — bring it up instead of opening a second copy." An
		// element placed on the start page carries an identity of its OWN, so it never
		// answers here and never blocks the same thing from being opened for real.
		const ibSourceDataObject* srcData = GetSourceObject();
		if (srcData == nullptr || !srcData->IsNewObject()) {
			ibFormVisualDocument* foundedVisualDocument =
				ibFormVisualDocument::FindDocByUniqueKey(m_formKey);
			if (foundedVisualDocument != nullptr) {
				foundedVisualDocument->Activate();
				return true;
			}
		}
	}

#pragma region __value_ref_guard_h__

	class ibValueFormControlGuard {
	public:
		ibValueFormControlGuard(ibValueForm* valueForm) : m_valueForm(valueForm) { valueForm->IncrRef(); }
		~ibValueFormControlGuard() { m_valueForm->DecrRef(); }
	private:
		ibValueForm* m_valueForm;
	};

	ibValueFormControlGuard enter(this);

#pragma endregion

	if (createContext) {

		ibValue bCancel = false;

		if (!CallAsEvent(wxT("beforeOpen"), bCancel))
			return false;

		if (bCancel.GetBoolean())
			return false;

		if (!CallAsEvent(wxT("onOpen")))
			return false;
	}

	ibFormVisualDocument* visualCreatedDoc = createContext ?
		new ibFormVisualDocument(this) :
		new ibFormVisualDocumentDemo(this);

	// A form opened FROM another is that document's child and goes with it; one opened on its own is one of the
	// client's documents.
	if (docParent != nullptr)
		visualCreatedDoc->SetDocParent(docParent);
	else if (docManager != nullptr)
		docManager->AddDocument(visualCreatedDoc);

	if (visualCreatedDoc->OnCreate(m_formKey, createContext ? runFlag : demoFlag)) {

		if (createContext)
			visualCreatedDoc->Modify(m_formModified);

		RefreshForm();
		return true;
	}

	// The document may be already destroyed, this happens if its view
	// creation fails as then the view being created is destroyed
	// triggering the destruction of the document as this first view is
	// also the last one. However if OnCreate() fails for any reason other
	// than view creation failure, the document is still alive and we need
	// to clean it up ourselves to avoid having a zombie document.

	visualCreatedDoc->DeleteAllViews();
	return false;
}

void ibValueForm::ActivateDocForm()
{
	ibFormVisualDocument* const ownerDocForm = GetVisualDocument();
	if (ownerDocForm != nullptr) {
		CallAsEvent(wxT("onReOpen"));
		ownerDocForm->Activate();
	}
}

void ibValueForm::ChoiceDocForm(ibValue& vSelected)
{
	if (m_controlOwner != nullptr) {
		ibValueForm* ownerForm = m_controlOwner->GetOwnerForm();
		if (ownerForm != nullptr)
			ownerForm->CallAsEvent(wxT("choiceProcessing"), vSelected, GetValue());
		m_controlOwner->ChoiceProcessing(vSelected);
		if (ownerForm != nullptr)
			ownerForm->UpdateForm();
	}
}

void ibValueForm::RefreshDocForm()
{
	if (!appData->DesignerMode()) {
		CallAsEvent(wxT("refreshDisplay"));
	}
}

bool ibValueForm::CloseDocForm()
{
	ibFormVisualDocument* const visualDoc = GetVisualDocument();

	if (visualDoc == nullptr)
		return false;

	if (!appData->DesignerMode() && !visualDoc->IsVisualDemonstrationDoc()) {

		ibValue bCancel = false;
		CallAsEvent(wxT("beforeClose"), bCancel);

		if (bCancel.GetBoolean())
			return false;

		CallAsEvent(wxT("onClose"));
	}

	return true;
}
