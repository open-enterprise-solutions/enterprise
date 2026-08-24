#ifndef __FORM_VALUE_H__
#define __FORM_VALUE_H__

#include "frontend/visualView/ctrl/control.h"
#include "frontend/docView/docView.h"   // ibDocument / ibMetaDocument — the doc-parent a form opens under
#include "backend/sourceDescription.h"   // ibSourceDescription — the binding-path wrapper (Get/SetValueByAttributePath)
#include "backend/backend_command.h"   // ibBackendCommandSender — the form IS-A command-hop source (entry gate)

#include <memory>   // std::unique_ptr — owns the form's attribute/value registry entries

#define defaultFormId 1
#define thisForm wxT("ThisForm")

//********************************************************************************************
//*                                     Defines                                              *
//********************************************************************************************

class BACKEND_API ibValueType;
class BACKEND_API ibUniqueKey;

class FRONTEND_API ibFormVisualEditView;

class BACKEND_API ibValueMetaObjectFormBase;
class BACKEND_API ibValueMetaObjectGenericData;

class FRONTEND_API ibFormAttributeValue;   // registry entry: owns an attribute + its managed value
class FRONTEND_API ibFormCommandValue;     // registry entry: a form-local command (event) property object
class BACKEND_API ibBackendFormAttribute;

//********************************************************************************************
//*                                 define commom clsid									     *
//********************************************************************************************

//COMMON FORM
constexpr ibClassID g_controlFormCLSID = control_to_clsid("CT_FRME");

//********************************************************************************************
//*                                      Value Frame                                         *
//********************************************************************************************

class FRONTEND_API ibValueForm :
	// ibValueFrame FIRST: it carries the ibValue/ibValueDynamicMembers sub-object, so
	// putting it at offset 0 keeps ibValue at the form's offset 0 — member-pmf binds
	// (m_members.Bind(this, &ibValueForm::FillFormMembers)) then need no MI
	// this-adjustment, which the cast to void(ibValue::*) would otherwise drop.
	// The form is the command SOURCE / ENTRY hop of the walk — the ibBackendCommandSender contract comes through
	// ibBackendValueForm (server-side, so a headless caller can walk commands); ibValueForm IMPLEMENTS GetCommandByHop.
	public ibValueFrame, public ibBackendValueForm, public ibRuntimeModuleDataObject
{
public:

private:

	enum {
		eSystem = eSizerItem + 1,
		eProcUnit = g_aliasExport,   // module exports go through the descriptor autobind
		eAttribute,                  // controls-with-value: ThisForm.<controlName>
		eFormAttribute               // form source attributes: ThisForm.<attrName>
	};

public:

	const ibUniqueKey& GetFormKey() const { return m_formKey; }
	bool CompareFormKey(const ibUniqueKey& formKey) const { return m_formKey == formKey; }

public:

	void SetCaption(const wxString& caption) { return m_propertyTitle->SetValue(caption); }
	wxString GetCaption() const { return m_propertyTitle->GetValueAsTranslateString(); }

	// Soft-lock state. m_lockBadgeHolder holds the blocking user's name
	// when TryAcquireFormLock conflicted on open; empty means the form
	// is editable normally. UI consumers (future lock-icon overlay /
	// status bar) read GetLockBadgeHolder to decide whether to render
	// the badge - title decoration was tried and rolled back as too
	// noisy (titles in OES are already long with code + description).
	//
	// RefreshLockBadge: re-attempt acquire and flip badge accordingly.
	// On success - clear badge (we now hold the lock, form editable).
	// On persistent conflict - update holder if it changed. Called
	// from UpdateForm so cross-user notifier ticks naturally refresh
	// lock state alongside data state; explicit callers (focus / poll
	// timer) may invoke directly too. Safe no-op when not in soft-
	// lock state.
	void SetLockBadge(const wxString& holderName) { m_lockBadgeHolder = holderName; }
	const wxString& GetLockBadgeHolder() const { return m_lockBadgeHolder; }
	void RefreshLockBadge();

	wxColour GetForegroundColour() const { return m_propertyFG->GetValueAsColour(); }
	wxColour GetBackgroundColour() const { return m_propertyBG->GetValueAsColour(); }

	bool IsFormEnabled() const { return m_propertyEnabled->GetValueAsBoolean(); }

	wxOrientation GetOrient() const { return m_propertyOrient->GetValueAsEnum(); }

	ibValueFrame* NewObject(const ibClassID& clsid, ibValueFrame* parentControl = nullptr, const ibValue& generateId = true);
	ibValueFrame* NewObject(const wxString& classControl, ibValueFrame* controlParent, const ibValue& generateId = true) {
		const ibClassID& clsid = ibValue::GetIDObjectFromString(classControl);
		if (clsid > 0) {
			return NewObject(
				ibValue::GetIDObjectFromString(classControl),
				controlParent,
				generateId
			);
		}
		return nullptr;
	}

	template <typename retType>
	inline retType* NewObject(const ibClassID& clsid, ibValueFrame* parentControl = nullptr, const ibValue& generateId = true) {
		return dynamic_cast<retType*>(
			NewObject(clsid, parentControl, generateId));
	}

	template <typename retType>
	inline retType* NewObject(const wxString& className, ibValueFrame* parentControl = nullptr, const ibValue& generateId = true) {
		return dynamic_cast<retType*>(
			NewObject(className, parentControl, generateId));
	}

	/**
	* Resuelve un posible conflicto de nombres.
	* @note el objeto a comprobar debe estar insertado en proyecto, por tanto
	*       no es v�lida para arboles "flotantes".
	*/
	void ResolveNameConflict(ibValueFrame* control);

	/**
	* Fabrica de objetos.
	* A partir del nombre de la clase se crea una nueva instancia de un objeto.
	*/
	ibValueFrame* CreateObject(const wxString& className, ibValueFrame* parentControl = nullptr);

	/**
	* Crea un objeto como copia de otro.
	*/
	static bool CopyObject(ibValueFrame* srcControl, bool copyOnPaste = true);
	static ibValueFrame* PasteObject(ibValueForm* dstForm, ibValueFrame* dstParent);

public:

	ibValueForm(const ibValueMetaObjectFormBase* creator = nullptr, ibControlFrame* ownerControl = nullptr,
		ibSourceDataObject* srcObject = nullptr, const ibUniqueKey& formGuid = wxNullUniqueKey);

	virtual ~ibValueForm();

	//****************************************************************************
	//*                              Override attribute                          *
	//****************************************************************************

	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);        //setting attribute
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);                   //attribute value

	//****************************************************************************
	//*                              Support methods                             *
	//****************************************************************************

	// The form's OWN members, ADDED on top of ibValueFrame::FillMembers (properties +
	// Events from the base bind); module exports follow as the helper's tail
	// (descriptor autobind). Bound in the ctor (was PrepareNames). The member-pmf bind
	// is lossless here because ibValueFrame is ibValueForm's FIRST base, so the ibValue
	// sub-object is at offset 0 (no MI this-adjustment to lose in the pmf cast).
	void FillFormMembers(ibMemberTable& helper) const;

	virtual bool CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray);
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);

	//****************************************************************************
	//*                              Support form context                        *
	//****************************************************************************

	virtual void BuildForm(const ibFormID& formType);
	virtual void InitializeForm(const ibValueMetaObjectFormBase* creator, ibControlFrame* ownerControl,
		ibSourceDataObject* srcObject, const ibUniqueKey& formGuid);

	virtual bool InitializeFormModule();

	// Form's meta-object drives lazy compile-module creation inside
	// ibRuntimeModuleDataObject::BindContextVariable. Form has m_metaFormObject
	// set at InitializeForm time, long before m_compileModule exists.
	virtual const class ibValueMetaObjectModuleBase* GetMetaForCompile() const override;

	//get metaData
	virtual const ibMetaData* GetMetaData() const;

	virtual ibValueForm* GetImplValueRef() const override {
		return const_cast<ibValueForm*>(this);
	}

	// The form's live source = the source assigned into the MAIN attribute
	// (falls back to m_sourceObject for back-compat / no main attribute).
	// Controls read it (and dot-walk on it) through here, so routing the source
	// through the attribute needs no per-control change. Out-of-line: needs the
	// complete ibValueFormAttribute type.
	virtual ibSourceDataObject* GetSourceObject() const;
	virtual const ibValueMetaObjectFormBase* GetFormMetaObject() const { return m_metaFormObject; }

	//****************************************************************************
	//*           Attribute store — the form's typed source registry            *
	//****************************************************************************
	// Externally managed (designer CRUD / build-time defaulting). Each entry is
	// a typed form-local source; the MAIN one receives the source object passed
	// on open. Serialized as a separate "Attributes" child node of the form,
	// read back on load. Each attribute is also a local variable of the form
	// module (bound by Name).

	// Registry CRUD — every accessor hands out the ENTRY (ibFormAttributeValue): the
	// entity carrying the attribute (its hidden private description) AND its value. Never
	// a raw attribute. An attribute is NEVER empty: name + Type + value.
	ibFormAttributeValue* AddAttribute(const wxString& name, const ibClassID& type, const ibValue& value);
	// The MAIN entry (the source object lands here) — guards that no other main exists.
	ibFormAttributeValue* AddMainAttribute(const wxString& name, const ibClassID& type, ibSourceDataObject* value);

	// Provision a fresh attribute — name = `name` made UNIQUE among the form's attributes, typed from
	// `typeDesc` (the control's default type, already produced by the source-type generator) — and return
	// its id. The auto-source the source controls' creation helper (ibValueControl::AutoBindNewSource)
	// binds to. wxNOT_FOUND on failure.
	ibMetaID AddAutoAttribute(const wxString& name, const ibTypeDescription& typeDesc);

	// Command-friendly split of AddAttribute (lets the visual-editor undo stack co-own the exact
	// entry across undo/redo — the holder is a ref-counted runtime value, held by ibValuePtr like a
	// control):
	//   MAKE   — build a holder UNOWNED by the form (no registry slot, no module bind); it creates
	//            its own description;
	//   ATTACH — the form co-owns the holder and module-wires it;
	//   DETACH — the form drops its ref (undo), unbinding it first; the caller keeps it alive.
	ibValuePtr<ibFormAttributeValue> MakeAttribute(const wxString& name, const ibClassID& type = 0, const ibValue& value = ibValue());
	ibFormAttributeValue* AttachAttribute(ibValuePtr<ibFormAttributeValue> holder);
	ibValuePtr<ibFormAttributeValue> DetachAttribute(ibFormAttributeValue* entry);

	ibFormAttributeValue* GetAttribute(const wxString& name) const;
	ibFormAttributeValue* GetAttribute(unsigned int idx) const;
	// Path-head resolve: a binding stores the attribute's id at path[0].
	ibFormAttributeValue* FindAttributeById(const ibMetaID& id) const;
	// The main entry — found by the attribute's Main flag (single source of truth).
	ibFormAttributeValue* GetMainAttribute() const;
	// Make entry the SOLE main attribute (sets its flag, clears it on all others).
	void SetMainAttribute(ibFormAttributeValue* entry);

	void DeleteAttribute(const wxString& name);
	void DeleteAttribute(unsigned int idx);
	// Bind `entry` into the form module: its value cell as a LOCAL named <attrName>, and —
	// when it is the MAIN — the exported DataSource too. Self-managed by the attribute's
	// lifecycle / main flag: add and become-main bind, delete and lose-main unbind, so no
	// external "is it bound" bookkeeping is needed. Idempotent (binds overwrite by name).
	void BindAttributeVariable(ibFormAttributeValue* entry);
	// Remove the form-module binds pointing into `entry` before it is destroyed (its local
	// name bind, and DataSource if it is the main) — prevents dangling-pointer compiles.
	void DropAttributeBinds(ibFormAttributeValue* entry);

	// Name uniqueness among the form's attributes (optionally ignoring one entry — for rename).
	bool IsAttributeNameUnique(const wxString& name, const ibFormAttributeValue* except = nullptr) const;
	// `base` made unique by appending a counter when it collides.
	wxString MakeUniqueAttributeName(const wxString& base = wxT("Attribute")) const;
	// Rename `entry` to `newName`, re-binding its module variable. False if empty / not unique.
	bool RenameAttribute(ibFormAttributeValue* entry, const wxString& newName);
	// Paste a clipboard-deserialized attribute node as a fresh NON-main entry (unique name + id,
	// wired into the module). Returns the new entry, or nullptr on failure.
	ibFormAttributeValue* PasteAttribute(const ibDataNode& node);

	// FORM COMMAND CRUD — form-local events managed like ATTRIBUTES: each is an ibFormCommandValue property object
	// (Name/Caption/Action/Picture) the inspector edits, held by ibValuePtr, serialized WITH the form data. A
	// projection binds to a form command by its ID (a normal 1-hop command path, rename-stable) and runs its Action
	// (a form-runtime procedure) live; nothing to do with metaobjects.
	ibFormCommandValue* AddFormCommand(const wxString& name, const wxString& procedure = wxEmptyString);
	const std::vector<ibValuePtr<ibFormCommandValue>>& GetFormCommands() const { return m_formCommands; }
	ibFormCommandValue* GetFormCommand(const wxString& name) const;
	ibFormCommandValue* FindFormCommandById(const ibMetaID& id) const;

	// ibBackendCommandSender — the form is the command SOURCE / entry hop: resolve an id to a command-capable
	// value. Either the id is a config command / object command (a command metaobject, itself command-capable, which
	// then hops its own sub-commands / a section its items), or the walk ends here. The front-end door starts
	// ResolveCommandPath on the form; each further hop self-describes, exactly as a source path does.
	virtual bool GetCommandByHop(const ibCommandHop& hop, ibValue& out) override;

	void DeleteFormCommand(const wxString& name);
	bool RenameFormCommand(ibFormCommandValue* entry, const wxString& newName);
	wxString MakeUniqueFormCommandName(const wxString& base = wxT("Command")) const;
	bool IsFormCommandNameUnique(const wxString& name, const ibFormCommandValue* except = nullptr) const;
	ibMetaID NextFormCommandId() const;
	ibFormCommandValue* PasteFormCommand(const ibDataNode& node);
	bool ReadFormCommands(const ibDataNode& node);
	bool WriteFormCommands(ibDataNode& node) const;

	unsigned int GetAttributeCount() const { return (unsigned int)m_attributes.size(); }

	// ⭐⭐ IS EVERY REQUIRED ATTRIBUTE FILLED — the form's own filling check, and the seam the
	// attribute's `FillCheck` was declared against. Messages each one that is not and answers false;
	// it does NOT raise, because a form is somebody standing in front of a window and the caller
	// decides what an empty field costs. Reachable from a script as `ThisForm.CheckFilling()`.
	bool CheckFilling() const;
	// Next free attribute id (max existing + 1) — PUBLIC so the holder's nested description can
	// stamp itself on construction (a nested class does not inherit the enclosing class's friends).
	ibMetaID NextAttributeId() const;

	// Serialize the attribute store as its own "Attributes" child node. Called from ReadData / WriteData.
	bool ReadAttributes(const ibDataNode& node);
	bool WriteAttributes(ibDataNode& node) const;

	// Available sources (backend wrappers) for a control of the given kind: the
	// MAIN attribute is always reachable; auxiliary attributes match by kind;
	// tableColumn enumerates nothing (a column sources from its parent table).
	// Filled into out; returns false (empty) when nothing is available.
	bool GetSourceList(ibSourceDataType kind, std::vector<ibBackendFormAttributeValue*>& out) const;

	// Resolve a binding DESCRIPTION whose HEAD (path[0]) selects an attribute from the table;
	// the remainder dot-walks that attribute's value/source. A head that matches no
	// attribute is a stale binding (returns false) — path[0] is always a form-local id.
	bool GetValueByAttributePath(const ibSourceDescription& desc, ibValue& result) const;

	// Write a value through a binding description (head selects the attribute). Only a DIRECT
	// field is writable — the attribute itself [attr] or head + one field [attr, field].
	// A deeper reference dot-walk is READ-ONLY (returns false / no-op).
	bool SetValueByAttributePath(const ibSourceDescription& desc, const ibValue& value);
	bool IsWritableBinding(const ibSourceDescription& desc) const;

	const ibValueMetaObjectGenericData* GetMetaObject() const;

	// get control caption
	virtual wxString GetControlTitle() const;

	ibValue GetCreatedValue() const { return m_createdValue; }

	// One-shot consume - read and reset.  NotifyCreate sets this to drive
	// position-to-new on the next UpdateForm.  Without clearing, every
	// subsequent UpdateForm (manual Refresh, sort, idle reset) sees the
	// same value and re-positions, bouncing the user's later selection
	// back to the create row.
	//
	// There is no changedValue twin any more: a CHANGE carries no position
	// anchor, because the current row survives a refresh as a refcounted
	// node and re-locates itself by row-key.  Only a create — a row that
	// did not exist to stand on — moves the user.
	ibValue ConsumeCreatedValue() {
		ibValue v = m_createdValue;
		m_createdValue = wxEmptyValue;
		return v;
	}

	virtual ibValueForm* GetOwnerForm() const {
		return const_cast<ibValueForm*>(this);
	}

	ibValueFrame* GetOwnerControl() const {
		return dynamic_cast<ibValueFrame*>(m_controlOwner);
	}

	/**
	* Get type form
	*/
	virtual ibFormID GetTypeForm() const;

	/**
	* Can delete object
	*/
	virtual bool CanDeleteControl() const { return false; }

	/**
	* Is editable object?
	*/
	virtual bool IsEditable() const;

	// Is the form in VIEW-ONLY mode? Runtime state (NOT a persisted property): the form was opened over a
	// source whose WRITE right is denied (a read-only role), or opened explicitly view-only. Controls read it
	// through ibValueFrame::IsReadOnly() and build themselves read-only; data-modifying commands grey out.
	// Distinct from IsEditable() (designer: can the STRUCTURE be changed). SetViewOnly forces it on open.
	bool IsViewOnly() const;
	void SetViewOnly(bool viewOnly) { m_viewOnly = viewOnly; }

public:

	class ibValueFormCollectionControl : public ibValueDynamicMembers {
	public:
		ibValueFormCollectionControl();
		ibValueFormCollectionControl(ibValueForm* ownerFrame);
		virtual ~ibValueFormCollectionControl();

		// DoGetPMethods (protected) + by-value m_members come from ibValueDynamicMembers.
		void FillMembers(ibMemberTable& helper) const;   // bound in ctor (was PrepareNames)

		virtual bool CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray);
		virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);

		virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal); //attribute value
		virtual bool GetAt(const ibValue& varKeyValue, ibValue& pvarValue);

		//??????????? ??????:
		bool Property(const ibValue& varKeyValue, ibValue& cValueFound);
		unsigned int Count() const { return (unsigned int)m_formOwner->GetControlList().size(); }

		//?????? ? ???????????:
		virtual std::shared_ptr<ibValueIteratorState> CreateIterator() override;
	private:
		ibValueForm* m_formOwner;
	};

public:

	ibValueFrame* CreateControl(const wxString& classControl, ibValueFrame* control = nullptr);
	void RemoveControl(ibValueFrame* control);

	// All controls owned by this form, derived by walking the control hierarchy
	// (m_children), skipping sizer-items. Replaces the maintained m_listControl set
	// (whose SetOwnerForm erase-bookkeeping was a teardown hazard). Cold path -
	// script Controls collection + design-time name-conflict; the form tree is small.
	std::vector<ibValueControl*> GetControlList() const;

public:

	virtual bool LoadForm(const wxMemoryBuffer& data);
	virtual bool SaveForm(wxMemoryBuffer &data) const;

	//notify
	virtual void NotifyCreate(const ibValue& vCreated);
	virtual void NotifyChange(const ibValue& vChanged);
	virtual void NotifyDelete(const ibValue& vChanged);

	virtual void NotifyChoice(ibValue& vSelected);

	ibValue CreateControl(const ibValueType* classControl, const ibValue& vControl);
	ibValue FindControl(const ibValue& vControl);
	void RemoveControl(const ibValue& vControl);

public:

	virtual void ActivateForm() { ActivateDocForm(); }
	virtual void RefreshForm() { RefreshDocForm(); }
	virtual void UpdateForm();
	virtual bool CloseForm(bool force = false);
	virtual void HelpForm();
	virtual void ChangeForm();

	virtual bool GenerateForm(ibValueRecordDataObjectRef* obj) const;
	// The backend contract, and nothing beyond a type step: backend cannot name the doc/view
	// types, and the two hierarchies meet in ibMetaDocument alone. It has no body of its own.
	virtual void ShowForm(ibBackendMetaDocument* docParent = nullptr, bool createContext = true) override {
		ShowForm(static_cast<ibDocument*>(static_cast<ibMetaDocument*>(docParent)), createContext);
	}

	//set & get modify 
	virtual void Modify(bool modify = true);
	virtual bool IsModified() const { return m_formModified; }

	//shown form 
	virtual bool IsShown() const { return GetVisualDocument() != nullptr; }

	//support close form
	virtual void CloseOnChoice(bool close = true) { m_closeOnChoice = close; }
	virtual bool IsCloseOnChoice() const { return m_closeOnChoice; }

	virtual void CloseOnOwnerClose(bool close = true) { m_closeOnOwnerClose = close; }
	virtual bool IsCloseOnOwnerClose() const { return m_closeOnOwnerClose; }

	//timers 
	void AttachIdleHandler(const wxString& procedureName, int interval, bool single);
	void DetachIdleHandler(const wxString& procedureName);

	//get visual document
	virtual ibFormVisualDocument* GetVisualDocument() const;

	//special proc
	virtual void Update(wxObject* wxobject, ibVisualHost* visualHost);
	virtual void OnUpdated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost);

	//actionData
	virtual ibStandardCommandSet GetStandardCommands(const ibFormID& formType);
	virtual void CallAsAction(const ibActionID& lNumAction, ibBackendValueForm* srcForm);

	// The form's COMMAND provider, resolved LIVE on every query (never cached — a stored pointer would go stale
	// the moment the designer re-points the MAIN attribute without re-running any view's bind). A list form:
	// the control whose WHOLE binding is the main attribute (ibValueModelTableBox::IsMainSourceBound) adapts its dumb
	// command model into a full action interface. An object form: the self-commanding source object (both
	// ibSourceDataObject and ibStandardCommandSource). null when neither exists.
	ibStandardCommandSource* GetCommandProvider();

	// THE open — one body, one door. The form says only WHOSE child it is; where it lands
	// follows from that: a parent that composes its children (the home page) hands the
	// document a window of its own frame, everyone else gets a tab.
	bool ShowForm(ibDocument* docParent, bool createContext = true);

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//load & save object in control
	virtual bool ReadData(const ibDataNode& node);
	virtual bool WriteData(ibDataNode& node) const;

	// Copy/paste twin of Read/WriteData — carry the attribute store on the clipboard blob
	// (the copy walk bypasses Read/WriteData, so attributes would otherwise be dropped).
	virtual bool CopyData(ibDataNode& node) const;
	virtual bool PasteData(const ibDataNode& node);

	virtual int GetComponentType() const { return COMPONENT_TYPE_FRAME; }

private:

	//doc event
	bool CreateDocForm(ibDocument* docParent, bool createContext = true);
	void ActivateDocForm();
	void ChoiceDocForm(ibValue& vSelected);
	void RefreshDocForm();
	bool CloseDocForm();

	// Body in formObject.cpp - needs ibWebTimer complete type on web
	// for the wxObject* upcast (frontendTypes.h only forward-declares
	// ibWebTimer). Inline in the header dragged web-specific includes
	// into every desktop TU.
	void OnIdleHandler(wxTimerEvent& event);

	ibValue					m_createdValue;

	ibFormID		m_formType;
	ibUniqueKey				m_formKey;

	bool					m_formModified;

	// Explicit view-only override (set on open); IsViewOnly() also derives it live from the source's WRITE
	// right, so an unset flag still yields view-only when the role denies writing.
	bool					m_viewOnly = false;

	bool					m_closeOnChoice;
	bool					m_closeOnOwnerClose;

	const ibValueMetaObjectFormBase* m_metaFormObject; // ref to metaData

	ibControlFrame* m_controlOwner;

	// The form's typed source registry: each entry OWNS an attribute (its definition)
	// and holds, separately, the runtime VALUE that attribute manages. Held by unique_ptr
	// so a returned entry pointer stays valid across vector growth. The MAIN entry is the
	// one whose attribute carries the Main flag — no separate pointer (single source of
	// truth = the flag).
	std::vector<ibValuePtr<ibFormAttributeValue>> m_attributes;

	// FORM COMMANDS — form-local events (NOT metaobjects), managed like m_attributes: each is an ibFormCommandValue
	// property object, held ref-counted, serialized with the form data, listed in the navigator's "Form commands".
	std::vector<ibValuePtr<ibFormCommandValue>> m_formCommands;

	// Shared internals of the Add*/Paste attribute paths (dedup):
	void WireAttribute(ibFormAttributeValue* entry);                    // bind (if module live) + InvalidateNames

	// ibFrontendTimer = wxTimer on desktop, ibWebTimer on web. Both
	// inherit wxEvtHandler + produce wxTimerEvent where GetEventObject()
	// returns the timer instance - so OnIdleHandler's lookup matches
	// uniformly across builds. shared_ptr removes the manual delete on
	// teardown paths (form dtor, DetachIdleHandler, exception unwinds) -
	// same ownership flavour as m_valueForm's ibValuePtr but here we
	// don't need intrusive refcount, std is enough.
	std::map<wxString, std::shared_ptr<ibFrontendTimer>> m_idleHandlerArray;

	ibValuePtr<ibValueFormCollectionControl> m_formCollectionControl;

	ibPropertyCategory* m_categoryFrame = ibPropertyObject::CreatePropertyCategory(wxT("Frame"), _("Frame"));
	ibPropertyTString* m_propertyTitle = ibPropertyObject::CreateProperty<ibPropertyTString>(m_categoryFrame, wxT("Title"), _("Title"), wxT(""));
	ibPropertyColour* m_propertyFG = ibPropertyObject::CreateProperty<ibPropertyColour>(m_categoryFrame, wxT("ForegroundColour"), _("Foreground"), _("Sets the foreground colour of the window."), wxDefaultStypeFGColour);
	ibPropertyColour* m_propertyBG = ibPropertyObject::CreateProperty<ibPropertyColour>(m_categoryFrame, wxT("BackgroundColour"), _("Background"), _("Sets the background colour of the window."), wxDefaultStypeBGColour);
	ibPropertyBoolean* m_propertyEnabled = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryFrame, wxT("Enabled"), _("Enabled"), _("Enable or disable the window for user input.Note that when a parent window is disabled, all of its children are disabled as well and they are reenabled again when the parent is."), true);
	ibPropertyCategory* m_categorySizer = ibPropertyObject::CreatePropertyCategory(wxT("Sizer"), _("Sizer"));
	ibPropertyEnum<ibValueEnumOrient>* m_propertyOrient = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumOrient>>(m_categorySizer, wxT("Orient"), _("Orient"), wxVERTICAL);

	// Soft-lock badge - user name of the session that holds the lock.
	// Empty when the form is editable; set on form-open conflict.
	wxString m_lockBadgeHolder;

	friend class ibValueControl;
	friend class ibValueFormCollectionControl;

	friend class ibFormAttributeValue;

	friend class ibFormVisualDocument;
	friend class ibFormVisualEditView;
};

#endif 
