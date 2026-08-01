#ifndef _VISUAL_EDITOR_BASE_H__
#define _VISUAL_EDITOR_BASE_H__

#include <set>

#ifndef OES_USE_WEB
#include <wx/artprov.h>
#include <wx/config.h>
#include <wx/cmdproc.h>
#include <wx/docview.h>
#include <wx/splitter.h>
#include <wx/spinbutt.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/treectrl.h>
#endif

#include <map>
#include <unordered_map>

#include "frontend/frontend.h"
#include "frontend/frontendTypes.h"

#ifdef OES_USE_WEB
#include "frontend/web/webWindow.h"
#endif

class FRONTEND_API ibValueFrame;

// Where an open form's controls are KEPT: the (ibValueFrame -> wxObject) pairs and the two
// questions ever asked of them — which object renders this control, which control does this
// object belong to. Both builds keep one, in the place that actually holds the controls: the
// inner scrolling window on desktop, the host node itself on web. Neither writes the lookup
// twice, and a "not one of mine" answer is a null, not an error — asking about a window that
// is not a control (a chrome part, a plain wx child) is a normal question here.
class FRONTEND_API ibControlIndex {
public:
	void Append(ibValueFrame* control, wxObject* wx_object) { m_objects.insert_or_assign(control, wx_object); }
	void Remove(ibValueFrame* control) { m_objects.erase(control); }
	void Clear() { m_objects.clear(); }

	// Keyed by control — O(1). The reverse question is a scan, and stays one: it is asked per
	// click, not per control built.
	wxObject* FindObject(const ibValueFrame* control) const;
	ibValueFrame* FindControl(const wxObject* wx_object) const;

private:
	std::unordered_map<ibValueFrame*, wxObject*> m_objects;
};

// ibVisualHost — the container that owns one open form's control tree.
//
// On desktop the host is a PANEL — a facade — and the window that scrolls is INSIDE it:
//
//   ibVisualHost (wxPanel)            the facade: the form's chrome (toolbar, search row, …)
//     └ ibContentWindow                the inner window: HOLDS the form's controls (the
//                                     ibValueFrame -> wxObject index) and scrolls them
//
// The facade fills itself — chrome, caption, show / hide — and forwards everything about the
// controls to the inner window it reaches by reference (GetContentWindow). That is why the
// toolbar no longer moves when the wheel turns: it is not in the scrolling window at all.
//
// Base class swaps between builds (wxPanel on desktop; ibWebWindow on web to sit in the
// session's serialisable ibWebWindow tree) via the ibFrontendHostBase
// typedef from frontendTypes.h. wx-specific guts — sizer resolution
// helpers, event handlers, Generate/Refresh walkers — live only on the
// desktop side. Everything the shared form code actually needs at the
// call sites (GetValueForm, CreateVisualHost, …) is declared once,
// with parameter types routed through ibFrontendWindow / ibFrontendSizer
// so signatures don't duplicate.
class FRONTEND_API ibVisualHost : public ibFrontendHostBase {
#ifndef OES_USE_WEB
	wxDECLARE_ABSTRACT_CLASS(ibVisualHost);
#endif
public:

#ifdef OES_USE_WEB
	ibVisualHost() = default;
#else
	// The inner scrolling window — the implementation half of the host. It owns the control
	// index and every walk over it (generate / refresh / clear / label alignment / virtual
	// size); the per-control hooks it needs (Create, OnCreated, Update, OnUpdated, Cleanup)
	// it calls back on the facade, which is where the designer overrides them.
	class FRONTEND_API ibContentWindow : public wxScrolledCanvas {
		friend class ibVisualHost;
	public:
		ibContentWindow(ibVisualHost& host, wxWindow* parent)
			: wxScrolledCanvas(parent, wxID_ANY), m_host(host)
		{
			wxScrolledCanvas::SetDoubleBuffered(true);
			wxScrolledCanvas::SetScrollRate(5, 5);
#ifdef __WXOSX__
			wxScrolledCanvas::SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
			wxScrolledCanvas::SetBackgroundStyle(wxBG_STYLE_SYSTEM);
#endif
		}

		// The controls — this window holds them, everyone else asks it.
		ibValueFrame* GetObjectBase(const wxObject* wxobject) const { return m_controls.FindControl(wxobject); }
		wxObject* GetWxObject(const ibValueFrame* baseobject) const;
		void AppendInnerControl(ibValueFrame* control, wxObject* wx_object) { m_controls.Append(control, wx_object); }
		void RemoveInnerControl(ibValueFrame* control) { m_controls.Remove(control); }

		// The form's children: build them, refresh them, tear them down.
		void CreateContent(const class ibValueForm* valueForm);
		void UpdateContent(const class ibValueForm* valueForm);
		void ClearContent();

		// Live edits of a single control (the facade forwards its own four verbs here).
		void CreateControl(ibValueFrame* obj, ibValueFrame* parent, bool firstCreated);
		void UpdateControl(ibValueFrame* obj, ibValueFrame* parent);
		void RemoveControl(ibValueFrame* obj, ibValueFrame* parent);
		void ClearControl(ibValueFrame* control, bool force);

		bool CalculateLabelSize(ibValueFrame* control = nullptr);
		void UpdateVirtualSize();

		// The sizer the controls are laid out in — owned by this window from CreateContent on.
		wxSizer* GetFrameSizer() const { return m_frameContentSizer; }

	private:

		void GenerateControl(ibValueFrame* obj, wxWindow* wxparent, wxObject* parentObject, bool firstCreated = false);
		void RefreshControl(ibValueFrame* obj, wxWindow* wxparent, wxObject* parentObject);

		struct ControlContext {
			ibValueFrame* objControl;   // original control, or its SIZERITEM wrapper
			ibValueFrame* objParent;    // logical parent (SIZERITEM unwrapped)
			wxObject*     parentObj;    // wxObject for objParent
			wxWindow*     windowObj;    // owning wxWindow for layout (this window as fallback)
		};
		ControlContext ResolveControlContext(ibValueFrame* obj, ibValueFrame* parent,
			bool resolveWindow = true) const;

		ibVisualHost& m_host;
		// The form's controls, held HERE.
		ibControlIndex m_controls;
		// The controls' sizer. Null before the first CreateContent / after ClearContent.
		wxSizer* m_frameContentSizer = nullptr;
	};

	ibVisualHost(wxWindow* parent,
		wxWindowID id,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize,
		long style = wxTAB_TRAVERSAL) : wxPanel(parent, id, pos, size, style | wxBORDER_SUNKEN)
	{
		// The facade is a frame around ONE window — its inner one, filling it. Set up here, in
		// the ctor, so it holds for every host: whatever a host later does about its chrome,
		// the window with the controls is always stretched to the facade.
		m_contentWindow = new ibContentWindow(*this, this);
		wxBoxSizer* const facadeSizer = new wxBoxSizer(wxVERTICAL);
		facadeSizer->Add(m_contentWindow, 1, wxEXPAND);
		wxPanel::SetSizer(facadeSizer);
	}

	// The inner window, by reference — the facade's one link to where the controls live.
	ibContentWindow* GetContentWindow() const { return m_contentWindow; }
#endif

	virtual ~ibVisualHost() = default;

#ifdef OES_USE_WEB
	// ibWebWindow tag. Kept web-only — desktop has wxWidgets RTTI for
	// type identification.
	virtual wxString GetControlType() const override { return wxT("visualHost"); }
#endif

	virtual bool IsShownHost()    const { return true; }
	virtual bool IsDesignerHost() const { return false; }

	virtual class ibValueForm* GetValueForm() const = 0;

	// Host-walker. Both builds expose the same three verbs; desktop
	// bodies build a wxWidgets tree under GetBackgroundWindow(), web
	// bodies build an ibWebWindow subtree under *this. See visualHost.cpp.
	bool CreateAndUpdateVisualHost() {
		return ClearVisualHost() && CreateVisualHost() && UpdateVisualHost();
	}
	bool CreateVisualHost();
	bool UpdateVisualHost();
	bool ClearVisualHost();

	// Map/query the wxObject that a given ibValueFrame is rendered
	// into. Shared across builds — on desktop it indexes wxWindow
	// subclasses (native wxButton, wxCheckBox, …); on web it indexes
	// ibWebWindow / ibWebSizer render shims. Populated at Create()
	// time (walker on web, GenerateControl on desktop) and cleared by
	// ClearVisualHost / RemoveControl. The dispatcher reaches
	// HandleRequest through GetWxObject → dynamic_cast<ibWebWindow*>.
	ibValueFrame* GetObjectBase(const wxObject* wxobject) const;
	wxObject* GetWxObject(const ibValueFrame* baseobject) const;

	// Publicly accessible insertion point used by the web walker
	// (visualHost.cpp::AppendChildControls). On desktop the index lives in the inner
	// scrolling window, so these — like every other control-facing verb here — are forwards.
#ifdef OES_USE_WEB
	void AppendInnerControl(ibValueFrame* control, wxObject* wx_object) { m_controls.Append(control, wx_object); }
	void RemoveInnerControl(ibValueFrame* control) { m_controls.Remove(control); }
#else
	void AppendInnerControl(ibValueFrame* control, wxObject* wx_object) {
		m_contentWindow->AppendInnerControl(control, wx_object);
	}
	void RemoveInnerControl(ibValueFrame* control) {
		m_contentWindow->RemoveInnerControl(control);
	}
#endif

	// Returned type is ibFrontendWindow* (wxWindow* on desktop,
	// ibWebWindow* on web) so the signature stays shared. Bodies
	// that call through the pointer are still wx-specific and live
	// inside #ifndef OES_USE_WEB — unifying the declaration lets
	// desktop callers compile unchanged while keeping web symbols
	// available for future shared helpers.
	virtual ibFrontendWindow* GetParentBackgroundWindow() const = 0;
	virtual ibFrontendWindow* GetBackgroundWindow() const = 0;

#ifndef OES_USE_WEB
	// The controls' sizer — held by the inner window; falls back to the background window's
	// own sizer before the first build.
	wxSizer* GetFrameSizer() const {
		wxSizer* const contentSizer = m_contentWindow->GetFrameSizer();
		return contentSizer != nullptr ? contentSizer : GetBackgroundWindow()->GetSizer();
	}

	// Create the MAIN sizer (m_mainSizer) — the chrome's sizer. Built on the first
	// CreateVisualHost, never by a concrete host: WHERE it lands follows from where that host
	// puts its controls (see the body), so there is nothing for a subclass to say.
	void InitMainSizer();

	virtual void OnClickFromApp(wxWindow* currentWindow, wxMouseEvent& event) {}
#endif

protected:

	virtual void SetCaption(const wxString& strCaption) = 0;

	// The form's orientation goes to the CONTROLS' sizer — the main sizer stays vertical so
	// the chrome always sits above them. Same answer for every host on both builds, so it is
	// the base's, not a virtual each one re-implements identically.
	void SetOrientation(int orient);

	//*********************************************************
	//*        Component lifecycle — unified surface          *
	//*                                                        *
	// Same signatures on both builds. Desktop bodies do the    *
	// incremental wx-tree edit (GenerateControl + RefreshControl
	// + Relayout + CalculateLabelSize + UpdateVirtualSize); web
	// bodies are no-ops — the next HTTP response rebuilds from
	// the ibValueFrame tree via ClearVisualHost + CreateVisualHost,
	// so script-side callers don't need to ifdef their live-edit
	// paths. See visualHost.cpp for the per-build implementations.
	void CreateControl(ibValueFrame* obj, ibValueFrame* parent = nullptr, bool firstCreated = false);
	void UpdateControl(ibValueFrame* obj, ibValueFrame* parent = nullptr);
	void RemoveControl(ibValueFrame* obj, ibValueFrame* parent = nullptr);
	void ClearControl(ibValueFrame* control, bool force = false);

	//*********************************************************
	//*            Events for visual — unified                 *
	//*                                                        *
	// Thin forwarders to the per-control ibValueFrame virtual. *
	// Parent type is routed through ibFrontendWindow so one    *
	// declaration covers both builds; the per-control Create   *
	// / OnCreated / Update / OnUpdated / Cleanup virtuals      *
	// already take ibFrontendWindow*.
	virtual wxObject* Create(ibValueFrame* control, ibFrontendWindow* wndParent);
	virtual void OnCreated(ibValueFrame* control, wxObject* obj, ibFrontendWindow* wndParent, bool firstCreated = false);
	virtual void OnSelected(ibValueFrame* control, wxObject* obj);
	virtual void Update(ibValueFrame* control, wxObject* obj);
	virtual void OnUpdated(ibValueFrame* control, wxObject* obj, ibFrontendWindow* wndParent);
	virtual void Cleanup(ibValueFrame* control, wxObject* obj);

#ifndef OES_USE_WEB
	// The heavy pass, run ONCE at the end of an update (never per control): lay the facade out
	// — chrome above, the inner window below — and repaint it. wx recurses from here, so no
	// step along the way has to lay itself out or repaint on its own. A host with a shape of
	// its own (the designer's card) extends this and chains back to it.
	virtual void UpdateHostSize() {
		Layout();
		Refresh();
	}

	// Label alignment and virtual size are walks over the controls, so they live in the inner
	// window; kept here as forwards because the designer host calls them.
	bool CalculateLabelSize(ibValueFrame* control = nullptr) {
		return m_contentWindow->CalculateLabelSize(control);
	}
	// The controls' scroll range — cheap, and the inner window owns it.
	void UpdateVirtualSize() { m_contentWindow->UpdateVirtualSize(); }
#endif // !OES_USE_WEB

	// Friend set applies to both builds — ibValueForm calls the
	// (now unified) CreateControl/RemoveControl hooks from live-edit
	// code paths, and ibValueModelTableBox does the same for column
	// collection rebuilds. Declaring once outside the ifdef keeps the
	// access surface identical across builds.
	friend class ibValueForm;
	friend class ibValueModelTableBox;

protected:
#ifdef OES_USE_WEB
	// On web the host holds the controls itself — there is no inner window; on desktop they
	// live in ibContentWindow, which keeps its own index of the same kind.
	ibControlIndex m_controls;
#else
	// The inner scrolling window — created in the ctor, destroyed with the host. THE place the
	// form's controls live; the facade only ever forwards to it.
	ibContentWindow* m_contentWindow = nullptr;
	// The host's MAIN sizer — a stable ATTRIBUTE created in the concrete host's
	// ctor (InitMainSizer) and set on the chrome window. Everything hangs on it: the chrome
	// layers (toolbar, search, …) at the top and the controls below.
	// It lives its own life across rebuilds — CreateVisualHost clears and
	// re-populates it rather than recreating it.
	wxSizer* m_mainSizer = nullptr;
	// The form's layer parts (toolbar today, search later), tracked EXPLICITLY — same as the
	// composite's canvas remembers its own (ibCanvasWindow::GetLayerParts). Filled by
	// CreateFormLayers, refreshed in place by UpdateFormLayers, cleared by ClearVisualHost. Lets
	// the update read its parts directly instead of re-deriving them by walking the sizer.
	std::vector<ibFrontendWindow*> m_formLayerParts;
#endif
};

#include "frontend/docView/docView.h"

#ifndef OES_USE_WEB
// Designer-only editor notebook interface. Lives outside the web build
// because the designer does not run under wfrontend.dll.
class FRONTEND_API ibFrontendVisualEditorNotebook {
public:

	static ibFrontendVisualEditorNotebook* FindEditorByForm(const ibValueFrame* valueForm);

	ibFrontendVisualEditorNotebook();
	virtual ~ibFrontendVisualEditorNotebook();

	virtual void CreateControl(const wxString& controlName) = 0;
	virtual void RemoveControl(ibValueFrame* obj) = 0;
	virtual void CutControl(ibValueFrame* obj, bool force = false) = 0;
	virtual void CopyControl(ibValueFrame* obj) = 0;
	virtual bool PasteControl(ibValueFrame* parent) = 0;
	virtual void InsertControl(ibValueFrame* obj, ibValueFrame* parent) = 0;
	virtual void ExpandControl(ibValueFrame* obj, bool expand) = 0;
	virtual void SelectControl(ibValueFrame* obj) = 0;
	// Select any property object (not a control) in the inspector — e.g. a command-bar tool
	// clicked in the designer. Goes through the common ibPropertyObject the inspector speaks.
	virtual void SelectPropertyObject(class ibPropertyObject* obj) = 0;

	virtual void ModifyEvent(class ibEvent* event, const wxVariant& oldValue, const wxVariant& newValue) = 0;
	virtual void ModifyProperty(class ibProperty* prop, const wxVariant& oldValue, const wxVariant& newValue) = 0;

	virtual void RefreshEditor() = 0;

	virtual ibValueFrame* GetValueForm() const = 0;
	virtual ibMetaDocument* GetEditorDocument() const = 0;
	virtual ibVisualHost* GetVisualHost() const = 0;

	virtual wxEvtHandler* GetHighlightPaintHandler(wxWindow* wnd) const = 0;

private:
	static std::set<ibFrontendVisualEditorNotebook*> ms_visualEditorArray;
};

#define g_visualHostContext FindVisualEditor()
#endif // !OES_USE_WEB

#endif
