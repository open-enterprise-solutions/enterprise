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

// ibVisualHost — the container that owns one open form's control tree.
//
// Base class swaps between builds (wxScrolledCanvas on desktop for
// native scrolling + wx event routing; ibWebWindow on web to sit in the
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
	ibVisualHost(wxWindow* parent,
		wxWindowID id,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize,
		long style = wxScrolledWindowStyle) : wxScrolledCanvas(parent, id, pos, size, style | wxBORDER_SUNKEN)
	{
		wxScrolledCanvas::SetDoubleBuffered(true);
		wxScrolledCanvas::SetScrollRate(5, 5);
#ifdef __WXOSX__
		wxScrolledCanvas::SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
		wxScrolledCanvas::SetBackgroundStyle(wxBG_STYLE_SYSTEM);
#endif
	}
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
	// (visualHost.cpp::AppendChildControls). Desktop callers use the
	// same method internally after GenerateControl creates a wxWindow.
	void AppendInnerControl(ibValueFrame* control, wxObject* wx_object) {
		m_baseObjects.insert_or_assign(control, wx_object);
	}
	void RemoveInnerControl(ibValueFrame* control) {
		m_baseObjects.erase(control);
	}

	// Returned type is ibFrontendWindow* (wxWindow* on desktop,
	// ibWebWindow* on web) so the signature stays shared. Bodies
	// that call through the pointer are still wx-specific and live
	// inside #ifndef OES_USE_WEB — unifying the declaration lets
	// desktop callers compile unchanged while keeping web symbols
	// available for future shared helpers.
	virtual ibFrontendWindow* GetParentBackgroundWindow() const = 0;
	virtual ibFrontendWindow* GetBackgroundWindow() const = 0;

#ifndef OES_USE_WEB
	wxSizer* GetFrameSizer() const { return GetBackgroundWindow()->GetSizer(); }

	virtual void OnClickFromApp(wxWindow* currentWindow, wxMouseEvent& event) {}
#endif

protected:

	virtual void SetCaption(const wxString& strCaption) = 0;
	virtual void SetOrientation(int orient) = 0;

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
	virtual void UpdateHostSize() {}

private:

	// Desktop-only internals — the wx-tree generation / refresh walker
	// (wxWindow-heavy) and the per-incremental-edit context helper.
	// Web's rebuild path uses AppendChildControls in visualHost.cpp
	// instead, so none of these need web equivalents.
	void GenerateControl(ibValueFrame* obj, wxWindow* wxparent, wxObject* parentObject, bool firstCreated = false);
	void RefreshControl(ibValueFrame* obj, wxWindow* wxparent, wxObject* parentObject);

protected:

	struct ControlContext {
		ibValueFrame* objControl;   // original control, or its SIZERITEM wrapper
		ibValueFrame* objParent;    // logical parent (SIZERITEM unwrapped)
		wxObject*     parentObj;    // wxObject for objParent
		wxWindow*     windowObj;    // owning wxWindow for layout (host as fallback)
	};
	ControlContext ResolveControlContext(ibValueFrame* obj, ibValueFrame* parent,
		bool resolveWindow = true) const;

	// Calculate label size for static text
	bool CalculateLabelSize(ibValueFrame* control = nullptr);

	//Update virtual size
	void UpdateVirtualSize();
#endif // !OES_USE_WEB

	// Friend set applies to both builds — ibValueForm calls the
	// (now unified) CreateControl/RemoveControl hooks from live-edit
	// code paths, and ibValueModelTableBox does the same for column
	// collection rebuilds. Declaring once outside the ifdef keeps the
	// access surface identical across builds.
	friend class ibValueForm;
	friend class ibValueModelTableBox;

protected:
	// (ibValueFrame → wxObject) index. Populated by Create() on both
	// builds; queried by GetWxObject / dispatchers. See the public
	// accessors above.
	std::unordered_map<ibValueFrame*, wxObject* > m_baseObjects;
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
