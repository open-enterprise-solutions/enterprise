#ifndef _BASE__FRAME__H__
#define _BASE__FRAME__H__

#include <wx/wx.h>

#include "frmserver/frmserver.h"

#include "backend/compiler/value.h"
#include "backend/propertyManager/propertyManager.h"

#include "backend/backend_type.h"
#include "backend/backend_form.h"
#include "backend/backend_localization.h"
#include "backend/eventDispatcher.h"   // ibEventDispatcher — CallAsEvent dispatches the event's value through it

#include "frmserver/visualView/formdefs.h"
#include "frmserver/visualView/controlCtor.h"
#include "protocol/protocol.h"

class BACKEND_API ibProcUnit;

class BACKEND_API ibValueMetaObjectFormBase;

class BACKEND_API ibSourceDataObject;
class BACKEND_API ibValueRecordDataObject;
class BACKEND_API ibDataNode;   // serialize/dataBuilder.h — universal node (control -> node)

class FRMSERVER_API ibValueForm;
class FRMSERVER_API ibVisualHost;   // visualHost.h — who holds the form; every step of a control's life is told it

#include "backend/standardCommand.h"
#include "backend/moduleInfo.h"
#include "frmserver/visualView/layers/commandBar.h"   // ibValueCommandBar (command STORE the frame owns)

// Default foreground / background for designer-created form controls,
// toolbars, dataviews, dialogs. Aligned with interior palette: deep
// dusty blue text on cream content surface. Was Windows-blue accent
// (#0078D7) + light off-white grey (#EBEBF1) - both clashed with the
// powder-blue + cream + terracotta palette.
#define wxDefaultStypeFGColour wxColour(0x3F, 0x5C, 0x77)  // #3F5C77 deep dusty blue
#define wxDefaultStypeBGColour wxColour(0xFA, 0xF7, 0xF0)  // #FAF7F0 cream content

#include "core/fileSystem/fs.h"

class FRMSERVER_API ibFormVisualDocument;

#include "frmserver/visualView/controlEnum.h"

class FRMSERVER_API ibControlFrame : public ibBackendControlFrame {
public:

	//get value control and guid 
	virtual bool GetControlValue(ibValue& pvarControlVal) const { return false; }
	// ...AND WRITE IT BACK. The pair belongs together: the shared value-choice route
	// (ibTypeControlFactory::ChooseValue) holds an ibControlFrame*, reads the current
	// value through the getter above and puts the chosen one back through this. With
	// only the getter here, everything that edited a value had to be reached through
	// its concrete type — which is why the sequence was copied per control instead of
	// written once. Default arg = clear (what "empty" means is the type's business).
	virtual bool SetControlValue(const ibValue& varControlVal = ibValue()) { return false; }
	virtual ibGuid GetControlGuid() const { return ibGuid::newGuid(); }

	//get owner form 
	virtual ibValueForm* GetOwnerForm() const { return nullptr; }

	//get ref class 
	virtual ibClassID GetClassType() const { return 0; }

	//get visual document
	virtual ibFormVisualDocument* GetVisualDocument() const { return nullptr; }

	virtual bool HasQuickChoice() const = 0;
	virtual void ChoiceProcessing(ibValue& vSelected) = 0;
};

// CAN A VALUE OF THIS TYPE BE PICKED FROM A SHORT LIST? ONE function, and the only place the ctor kinds
// are walked. It used to be written out at both callsites — the form control and the filter cell — and the
// two copies disagreed about enumerations, so the same account type dropped its member list in a filter and
// refused to on a form. Body in frame.cpp.
FRMSERVER_API bool HasQuickChoice(const class ibCtorAbstractType* typeCtor);

class FRMSERVER_API ibValueFrame : public ibValueDynamicMembers,
	public ibPropertyObjectHelper<ibValueFrame>,
	public ibControlFrame,
	public ibStandardCommandSource {
	public:
protected:

	enum {
		eProperty,
		eControl,
		eEvent,
		eSizerItem,
	};

private:

	ibValueFrame* DoFindControlByID(const ibFormID& id, ibValueFrame* control) const;
	ibValueFrame* DoFindControlByName(const wxString& controlName, ibValueFrame* control) const;
	void DoGenerateNewID(ibFormID& id, ibValueFrame* top) const;

public:

	// NOT transferable across sessions — and this one override covers the whole
	// control tree, the form included (ibValueForm derives from here).
	//
	// A control is MUTABLE and being edited by the user right now, so a second
	// session reading it would see a moving target.
	//
	// Pass a job the DATA instead — a reference, a number, a string. Whatever the
	// form is showing can be re-derived on the other side; the form itself cannot.
	virtual bool IsTransferable() const override { return false; }

	void SetControlName(const wxString& controlName) { SetControlNameAsString(controlName); }
	wxString GetControlName() const {
		wxString result;
		GetControlNameAsString(result);
		return result;
	}

	ibValueFrame();
	virtual ~ibValueFrame();

	//system override 
	virtual wxString GetClassName() const final;
	virtual wxString GetObjectTypeName() const final;

	/**
	* Support generate id
	*/
	virtual ibFormID GenerateNewID();

	/**
	* Support get/set object id
	*/
	virtual bool SetControlID(const ibFormID& id) {
		if (id > 0) {
			ibValueFrame* foundedControl =
				FindControlByID(id);
			wxASSERT(foundedControl == nullptr);
			if (foundedControl == nullptr) {
				m_controlId = id;
				return true;
			}
		}
		else {
			m_controlId = id;
			return true;
		}
		return false;
	}

	virtual ibFormID GetControlID() const { return m_controlId; }

	// If the id was never populated (typed-factory controls, demo
	// forms, etc.), synthesise one via GenerateNewID. Idempotent after
	// the first call: once m_controlId is non-zero, returns it unchanged.
	// The frame addresses a control by this id — an event comes back
	// naming it — so every node the form writes must carry one.
	//
	// Leaves m_controlId == 0 when there is no owner form (a detached
	// control): nothing could find it through form->FindControlByID anyway.
	ibFormID EnsureControlID() {
		if (m_controlId == 0 && GetOwnerForm() != nullptr)
			GenerateNewID();
		return m_controlId;
	}

	/**
	* Support control name
	*/

	virtual bool GetControlNameAsString(wxString& result) const {
		result = GetObjectTypeName();
		return true;
	}

	virtual bool SetControlNameAsString(const wxString& result) const {
		return false;
	}

	// get control caption
	virtual wxString GetControlTitle() const {
		return wxGetTranslation(stringUtils::GenerateSynonym(GetClassName()));
	}

	virtual ibGuid GetControlGuid() const { return m_controlGuid; }

	/**
	* Find by control id
	*/
	virtual ibValueFrame* FindControlByName(const wxString& controlName) const;
	virtual ibValueFrame* FindControlByID(const ibFormID& id) const;

	/**
	* Support form
	*/
	virtual ibValueForm* GetOwnerForm() const = 0;
	virtual void SetOwnerForm(ibValueForm* ownerForm) {};

	/**
	Sets whether the object is expanded in the object tree or not.
	*/
	void SetExpanded(bool expanded) { m_expanded = expanded; }

	/**
	Gets whether the object is expanded in the object tree or not.
	*/
	bool GetExpanded() const { return m_expanded; }

	//get metaData
	virtual const ibMetaData* GetMetaData() const = 0;

	/**
	* Can delete object
	*/
	virtual bool CanDeleteControl() const = 0;

	// Available by the functional options of this base — answered by an element of a form (control.h);
	// anything else in the tree (a form, a sizer item) always is.
	virtual bool IsAvailable() const { return true; }

public:

	// before/after run
	virtual bool InitializeControl() { return true; }

	// ⭐ THE CONTROL'S LIFE, as the window's was — driven by the host that holds the form (visualHost.h), a pair at
	// each step, the second when the children have had theirs:
	//   OnCreate → the children → OnCreated   the form opened (its beforeOpen / onOpen ran, the parameters an
	//                                         opener set are in place), or the control added to an open form
	//   OnUpdate → the children → OnUpdated   the frame drawn
	//   the children → OnCleanup              the form closed, or the control taken off an open one
	// …and OnSelected: the control picked out (the client's Focus). What holds the form is asked of the host
	// (IsDesignerHost), as the window's events asked it.
	virtual void OnCreate(ibVisualHost* host) {}
	virtual void OnCreated(ibVisualHost* host) {}
	virtual void OnSelected(ibVisualHost* host) {}
	virtual void OnCleanup(ibVisualHost* host) {}

public:

	// ⭐ WHAT THIS CONTROL SHOWS NOW. There is no window here: a client draws the form from the frame, and
	// the frame is the form's own schema — the node SaveNode writes, properties as the designer saved them —
	// with a `State` child beside them, which is what this writes: the value, the caption as resolved,
	// whether the control is enabled and shown. Everything a renderer would otherwise have to work out
	// itself is worked out here, once, on the server. A child of its own and not more fields, because the
	// JSON a client reads flattens fields and properties into one key set (serialize/jsonProvider.h): a
	// resolved `Enabled` beside the saved one would be the same key twice. The node stands where the window
	// stood in the window's Update — and, like it, this is not const: resolving can build what it reads (a
	// command bar gathers its entries).
	virtual void OnUpdate(ibDataNode& state, ibVisualHost* host) {}
	// …and the children have written theirs: what a container says once its children are drawn.
	virtual void OnUpdated(ibDataNode& state, ibVisualHost* host) {}

	// ⭐ AN EVENT FROM THE CLIENT, on this control: `event` is what happened to it (ibProtocolEvent), `args` the rest
	// of it. Each control answers the events it has and maps them onto its own model methods; the base answers
	// Command — an entry of the control's command bar pressed. False — not an event this control has. Reached
	// only through the form's door (ibValueForm::DispatchEvent), which has already refused a control that is not
	// enabled or not shown.
	virtual bool OnClientEvent(ibProtocolEvent event, const ibDataNode& args);

	// ⭐ A PART OF WHAT THIS CONTROL SHOWS, written when the client asks for it — what is too large to travel in
	// the frame: a table's rows, a spreadsheet's cells. `request` says which part, `response` gets it. False —
	// this control has nothing fetched (the default: everything it shows is in its State).
	virtual bool Fetch(const ibDataNode& request, ibDataNode& response) { return false; }

public:

	// call current event — ask the event for its dispatcher (a named-event value or a lambda, both ibEventDispatcher)
	// and Dispatch. The fire site stays agnostic: named vs lambda is pure polymorphism behind GetDispatcher()->Dispatch.
	template <typename ...Types>
	bool CallAsEvent(const ibEvent* event, Types&&... args) const {
		ibValue* ppParams[] = { (&args)..., nullptr };   // trailing null keeps a 0-arg array valid (mirrors CallAsProc)
		return CallAsEvent(event, ppParams, (long)sizeof...(args));
	}

	//call current form
	template <typename ...Types>
	bool CallAsEvent(const wxString& functionName, Types&&... args) const {
		ibValue* ppParams[] = { (&args)..., nullptr };
		return CallAsEvent(functionName, ppParams, (long)sizeof...(args));
	}

	// ⭐ A HANDLER RUN UPDATES THE FORM WHOLE (ibVisualHost::UpdateControl) — whatever it changed, the next frame draws.
	bool CallAsEvent(const ibEvent* event, ibValue** ppParams, const long lSizeArray) const;
	bool CallAsEvent(const wxString& functionName, ibValue** ppParams, const long lSizeArray) const;

public:

	virtual ibBackendValueForm* GetBackendForm() const;

	//get visual doc
	virtual ibFormVisualDocument* GetVisualDocument() const;

	virtual bool HasQuickChoice() const;
	virtual void ChoiceProcessing(ibValue& vSelected) {}

	// ⭐ A CONTROL THAT IS A DOCUMENT OF ITS OWN HANDS OVER ITS VIEW (Max, 2026-09-22: "the form is the same
	// doc/view, redirecting to the active element… the form's doc is only a facade"). The grid box and the
	// text box hold a document and its view; while the control is the active element, the form's view
	// (ibFormVisualEditView) is a facade over this one: menu, toolbar, commands, PRINTING — there is no
	// printout of a control's own any more, the view prints it — and saving. Everything else answers nothing.
	virtual class ibView* GetControlView() const { return nullptr; }

public:

	//support actionData
	virtual ibStandardCommandSet GetStandardCommands(const ibFormID& formType) override { return ibStandardCommandSet(); }
	virtual void CallAsAction(const ibActionID& lNumAction, ibBackendValueForm* srcForm) override {}

	// Command bar STORE, owned by this frame (created in the ctor of controls that
	// carry one — form/table). HasCommandBar = it exists; the visual host reads it
	// to form the toolbar around this control. Virtual so a control can suppress its
	// own bar (e.g. a table that IS the form's main source — the form toolbar covers it).
	virtual bool HasCommandBar() const { return m_commandBar != nullptr; }
	ibValueCommandBar* GetCommandBar() const { return m_commandBar; }

	// Does THIS control's binding reference the form's MAIN attribute (i.e. it IS the form's main data view)?
	// Base: no. A view bound single-hop to the main attribute overrides (ibValueModelTableBox::IsMainSourceBound);
	// the form's command-provider walk asks every control this base virtual — no per-type cast.
	virtual bool IsMainSourceBound() const { return false; }

	class ibValueEventContainer : public ibValueDynamicMembers {
	public:

		ibValueEventContainer();
		ibValueEventContainer(ibValueFrame* ownerEvent);
		virtual ~ibValueEventContainer();

		// DoGetPMethods (protected) + by-value m_members come from ibValueDynamicMembers.
		void FillMembers(ibMemberTable& helper) const;   // bound in ctor (was PrepareNames)
		virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);

		virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);
		virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal); //attribute value

		virtual bool SetAt(const ibValue& varKeyValue, const ibValue& varValue);
		virtual bool GetAt(const ibValue& varKeyValue, ibValue& pvarValue);

		//??????????? ??????:
		bool Property(const ibValue& varKeyValue, ibValue& cValueFound);
		unsigned int Count() const { return m_controlEvent->GetEventCount(); }

		//?????? ? ???????????:
		virtual std::shared_ptr<ibValueIteratorState> CreateIterator() override;

	private:
		ibValueFrame* m_controlEvent;
	};

	virtual bool GetControlValue(ibValue& pvarControlVal) const { return false; }

	// memory reader form clpboard 
	static ibValueFrame* CreatePasteObject(const ibReaderMemory& reader,
		ibValueForm* dstForm, ibValueFrame* dstParent);

	/**
	* Property events
	*/
	virtual bool OnPropertyChanging(ibProperty* property, const wxVariant& newValue);
	virtual void OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue);

	// `override` on purpose: this was declared with `const wxString&`, overrode nothing, and ran for
	// nobody — the inspector calls through ibPropertyObject, whose signature takes a wxVariant.
	virtual bool OnEventChanging(ibEvent* event, const wxVariant& newValue) override;
	virtual void OnEventChanged(ibEvent* event, const wxVariant& oldValue, const wxVariant& newValue);

	/**
	* Devuelve la posicion del hijo o GetChildCount() en caso de no encontrarlo
	*/
	bool ChangeChildPosition(ibValueFrame* obj, unsigned int pos);

	//copy & paste object 
	virtual bool CopyObject(ibWriterMemory& writer) const;
	virtual bool PasteObject(ibReaderMemory& reader);

public:

	/**
	* Get type form
	*/
	virtual ibFormID GetTypeForm() const = 0;

	//counter
	virtual void ControlIncrRef() { ibValue::IncrRef(); }
	virtual void ControlDecrRef() { ibValue::DecrRef(); }

	//methods
	// DoGetPMethods (protected) + by-value m_members come from ibValueDynamicMembers.
	// Derived controls bind their OWN FillMembers in their ctor (binders accumulate).
	void FillMembers(ibMemberTable& helper) const;   // bound in ctor (was PrepareNames)

	//attributes
	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);

	//check is empty
	virtual bool IsEmpty() const { return false; }

	virtual bool Init() final override;
	virtual bool Init(ibValue** paParams, const long lSizeArray) final override;

	//Get ref class 
	virtual ibClassID GetClassType() const {
		return ibValue::GetClassType();
	}

	virtual bool IsEditable() const;

	// Is this control READ-ONLY at runtime (its form in view-only mode)? Distinct from IsEditable (designer:
	// can the STRUCTURE be changed). The form is the root frame, so a control resolves this off its owner
	// form's IsViewOnly(); a control reads it at build time and renders classic read-only — value visible,
	// selectable, copyable, openable, but NOT editable. NOT Enable(false): that is availability (a dead,
	// greyed widget), which is for action BUTTONS, not data controls.
	virtual bool IsReadOnly() const;

	//runtime
	std::shared_ptr<ibProcUnit> GetFormProcUnit() const;

public:

	static inline wxArrayString GetAllowedUserProperty() {

		wxArrayString arr;

		arr.Add(wxT("title"));
		arr.Add(wxT("minimum_size"));
		arr.Add(wxT("maximum_size"));
		arr.Add(wxT("font"));
		arr.Add(wxT("fg"));
		arr.Add(wxT("bg"));
		arr.Add(wxT("align"));
		arr.Add(wxT("stretch"));
		arr.Add(wxT("proportion"));
		arr.Add(wxT("orient"));
		arr.Add(wxT("tooltip"));
		arr.Add(wxT("visible"));

		return arr;
	}

	// (de)serialize the whole control through the binary provider (form-blob entry)
	bool LoadControl(const ibValueMetaObjectFormBase* metaForm, ibReaderMemory& dataReader);
	bool SaveControl(const ibValueMetaObjectFormBase* metaForm, ibWriterMemory& dataWritter) const;

	// Node form, mirrors the metaobject path. Load/SaveNode add the header
	// (id / name / expanded) then delegate the per-type data to Read/WriteData — the
	// base has none, a control overrides. (Read/Load before Write/Save in every pair.)
	bool LoadNode(const ibDataNode& node);
	bool SaveNode(ibDataNode& node) const;

	// Copy / Paste node — the control's own clipboard serialization (routed to from Load/SaveControl while the form
	// is marked). Header + every property/event through the Copy/PasteNodeValue pair (source hops ride guids).
	bool PasteNode(const ibDataNode& node);
	bool CopyNode(ibDataNode& node) const;

	// The control's own properties, read from / written into its node — what Load/SaveNode carry beside the
	// header, and what the host writes into the frame (ibVisualHost::UpdateVisualHost). Base has none.
	virtual bool ReadData(const ibDataNode& node) { return true; }
	virtual bool WriteData(ibDataNode& node) const { return true; }

protected:

	virtual void OnChangeChildPosition(ibValueFrame* obj, unsigned int pos) {}
	virtual void OnChoiceProcessing(ibValue& vSelected) {}

	// Extra per-type data carried on the COPY/PASTE blob beyond properties & events — the
	// copy-path twin of Read/WriteData (which the copy walk bypasses to ride source hops on
	// guids). Base has none; ibValueForm overrides to carry its attribute collection.
	virtual bool CopyData(ibDataNode& node) const { return true; }
	virtual bool PasteData(const ibDataNode& node) { return true; }

protected:

	bool m_expanded = true; // is expanded in the object tree, allows for saving to file

	ibFormID	 m_controlId;

	// ⭐ THE OWNER FORM'S id counter — meaningful ONLY on the form (every control asks its owner).
	//
	// Same rewrite, same two reasons, as ibMetaData::GenerateNewID: the id used to be max(every id
	// in the control tree) + 1, recomputed on every new control. That is O(n) per control for a
	// question a counter answers in O(1) — and it puts a DELETED control's id straight back into
	// circulation, so a new control can be born holding the number something else still refers to.
	// Less destructive than the metadata twin (a control id never becomes a column name, and does
	// not survive the session), but wrong in the same way and for the same reason.
	//
	// 0 = not seeded; the first request walks the tree once to find the floor.
	ibFormID	 m_nextControlId = 0;
	ibGuid				 m_controlGuid;

	ibValuePtr<ibValueEventContainer> m_valEventContainer;
	ibValuePtr<ibValueCommandBar> m_commandBar;
};

#endif // !_BASE_H_
