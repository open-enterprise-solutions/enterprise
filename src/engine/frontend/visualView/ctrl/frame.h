#ifndef _BASE__FRAME__H__
#define _BASE__FRAME__H__

#include <wx/wx.h>

#include "frontend/frontend.h"
#include "frontend/frontendTypes.h"

#include "backend/compiler/value.h"
#include "backend/propertyManager/propertyManager.h"

#include "backend/backend_type.h"
#include "backend/backend_form.h"
#include "backend/backend_localization.h"
#include "backend/eventDispatcher.h"   // ibEventDispatcher — CallAsEvent dispatches the event's value through it

#include "frontend/visualView/formdefs.h"
#include "frontend/visualView/controlCtor.h"
#include "frontend/visualView/visualHost.h"

class BACKEND_API ibProcUnit;

class BACKEND_API ibValueMetaObjectFormBase;

class BACKEND_API ibSourceDataObject;
class BACKEND_API ibValueRecordDataObject;
class BACKEND_API ibDataNode;   // serialize/dataBuilder.h — universal node (control -> node)

class FRONTEND_API ibValueForm;
class FRONTEND_API ibVisualHostClient;

#include "backend/standardCommand.h"
#include "backend/moduleInfo.h"
#include "frontend/visualView/layers/commandBar.h"   // ibValueCommandBar (command STORE the frame owns)

// Default foreground / background for designer-created form controls,
// toolbars, dataviews, dialogs. Aligned with interior palette: deep
// dusty blue text on cream content surface. Was Windows-blue accent
// (#0078D7) + light off-white grey (#EBEBF1) - both clashed with the
// powder-blue + cream + terracotta palette.
#define wxDefaultStypeFGColour wxColour(0x3F, 0x5C, 0x77)  // #3F5C77 deep dusty blue
#define wxDefaultStypeBGColour wxColour(0xFA, 0xF7, 0xF0)  // #FAF7F0 cream content

#include "backend/fileSystem/fs.h"

class FRONTEND_API ibFormVisualDocument;

#include "frontend/visualView/controlEnum.h"

class FRONTEND_API ibControlFrame : public ibBackendControlFrame {
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
FRONTEND_API bool HasQuickChoice(const class ibCtorAbstractType* typeCtor);

class FRONTEND_API ibValueFrame : public ibValueDynamicMembers,
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
	// Two reasons, either sufficient. A control is MUTABLE and being edited by the
	// user right now, so a second session reading it would see a moving target.
	// And it is bound to the wx widget behind it, which belongs to one thread:
	// a job touching it from a worker is a cross-thread wx call, which is
	// undefined rather than merely racy.
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
	// forms, etc.), synthesise one via GenerateNewID - analogue of
	// wxID_ANY on desktop. Idempotent after first call: once
	// m_controlId is non-zero, returns it unchanged. Lets web
	// Create() rely on a stable id without each caller repeating the
	// if-then-generate dance.
	//
	// Falls back to leaving m_controlId == 0 when there is no owner
	// form (synthetic /demo trees, detached controls) - in that case
	// the ibWebWindow ctor's own auto-id counter kicks in and the
	// node is at least uniquely addressable even though it's not
	// findable through form->FindControlByID.
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
	* Support default menu
	*/
	virtual void PrepareDefaultMenu(wxMenu* menu) {}
	virtual void ExecuteMenu(ibVisualHost* visualHost, int id) {}

	/**
	* Get wxObject from visual view (if exist). Resolved through the
	* form's ibVisualHost map both on desktop (wxWindow-keyed) and on
	* web (ibWebWindow / ibWebSizer keyed). The dispatcher downcasts
	* to ibWebWindow* for HandleRequest.
	*/
	wxObject* GetWxObject() const;

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

public:

	// before/after run 
	virtual bool InitializeControl() { return true; }

public:

	/**
	* Create an instance of the wxObject and return a pointer
	*/
	virtual wxObject* Create(ibFrontendWindow* wndParent, ibVisualHost* visualHost) {
		return new ibNoObject;
	}

	//*********************************************************
	//*   Chrome — auxiliary UI around a control (unified)     *
	//*                                                        *
	// The visual host drives EVERY lifecycle stage through these *WithLayers wrappers
	// instead of the plain Create/OnCreated/… below. The base just forwards to the
	// plain method — a control with no chrome is unaffected. A control that carries
	// chrome (ibValueControl: a command-bar toolbar today, a status bar / search box
	// later) overrides them to build its chrome as one grouped unit and to route each
	// stage to its OWN inner window. The plain methods stay untouched.
	virtual wxObject* CreateWithLayers(ibFrontendWindow* wndParent, ibVisualHost* visualHost) {
		return Create(wndParent, visualHost);
	}
	virtual void OnCreatedWithLayers(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost, bool firstCreated) {
		OnCreated(wxobject, wxparent, visualHost, firstCreated);
	}
	virtual void OnSelectedWithLayers(wxObject* wxobject) {
		OnSelected(wxobject);
	}
	virtual void UpdateWithLayers(wxObject* wxobject, ibVisualHost* visualHost) {
		Update(wxobject, visualHost);
	}
	virtual void OnUpdatedWithLayers(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost) {
		OnUpdated(wxobject, wxparent, visualHost);
	}
	virtual void CleanupWithLayers(wxObject* wxobject, ibVisualHost* visualHost) {
		Cleanup(wxobject, visualHost);
	}

	/**
	* Allows components to do something after they have been created.
	* For example, Abstract components like NotebookPage and SizerItem can
	* add the actual widget to the Notebook or sizer.
	*
	* @param wxobject The object which was just created.
	* @param wxparent The wxWidgets parent - the wxObject that the created object was added to.
	*/
	virtual void OnCreated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost, bool firstCreated) {};

	/**
	* Allows components to respond when selected in object tree.
	* For example, when a wxNotebook's page is selected, it can switch to that page
	*/
	virtual void OnSelected(wxObject* wxobject) {};

	/**
	* Allows components to do something after they have been updated.
	*/
	virtual void Update(wxObject* wxobject, ibVisualHost* visualHost) {};

	/**
	* Allows components to do something after they have been updated.
	* For example, Abstract components like NotebookPage and SizerItem can
	* add the actual widget to the Notebook or sizer.
	*
	* @param wxobject The object which was just updated.
	* @param wxparent The wxWidgets parent - the wxObject that the updated object was added to.
	*/
	virtual void OnUpdated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost) {};

	/**
	 * Cleanup (do the reverse of Create)
	 */
	virtual void Cleanup(wxObject* obj, ibVisualHost* visualHost) {};

public:

	// call current event — ask the event for its dispatcher (a named-event value or a lambda, both ibEventDispatcher)
	// and Dispatch. The fire site stays agnostic: named vs lambda is pure polymorphism behind GetDispatcher()->Dispatch.
	template <typename ...Types>
	bool CallAsEvent(const ibEvent* event, Types&&... args) const {
		if (event == nullptr)
			return false;
		ibEventDispatcher* dispatcher = event->GetDispatcher();
		if (dispatcher == nullptr || dispatcher->IsEmpty())
			return true;   // undefined -> no-op, the event just proceeds
		ibValue* argPtrs[] = { (&args)..., nullptr };   // trailing null keeps a 0-arg array valid (mirrors CallAsProc)
		ibValue eventCancel = false;
		try {
			return dispatcher->Dispatch(GetFormProcUnit().get(), argPtrs, (long)sizeof...(args), eventCancel);
		}
		catch (...) {
			return false;
		}
	}

	//call current form
	template <typename ...Types>
	bool CallAsEvent(const wxString& functionName, Types&&... args) const {
		std::shared_ptr<ibProcUnit> formProcUnit = GetFormProcUnit();
		if (formProcUnit != nullptr && !functionName.IsEmpty()) {
			try {
				formProcUnit->CallAsProc(
					functionName,
					args...
				);
			}
			catch (...) {
				return false;
			}
			return true;
		}

		return true;
	}

public:

	virtual ibBackendValueForm* GetBackendForm() const;

	//get visual doc
	virtual ibFormVisualDocument* GetVisualDocument() const;

	virtual bool HasQuickChoice() const;
	virtual void ChoiceProcessing(ibValue& vSelected) {}

#ifndef OES_USE_WEB
	// Designer-only: find the editor notebook that currently holds the
	// form this control belongs to. The notebook class itself is web-
	// excluded (see visualHost.h), so the accessor goes with it.
	ibFrontendVisualEditorNotebook* FindVisualEditor() const;
#endif

	//support printing 
	virtual wxPrintout* CreatePrintout() const { return nullptr; }

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

	virtual bool OnEventChanging(ibEvent* event, const wxString& newValue);
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

protected:

	virtual void OnChangeChildPosition(ibValueFrame* obj, unsigned int pos) {}
	virtual void OnChoiceProcessing(ibValue& vSelected) {}

	virtual bool ReadData(const ibDataNode& node) { return true; }
	virtual bool WriteData(ibDataNode& node) const { return true; }

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
