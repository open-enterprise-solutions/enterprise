// FORKED from wxWidgets' wxPropertyGrid (wx 3.3.4, src/propgrid/propgrid.cpp): wx -> ib prefixes
// applied mechanically to the names it defines (docs/private/wx-fork.md).

/////////////////////////////////////////////////////////////////////////////
// Name:        src/propgrid/propgrid.cpp
// Purpose:     ibPropertyGrid
// Author:      Jaakko Salli
// Created:     2004-09-25
// Copyright:   (c) Jaakko Salli
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx/wx.h".
#include "wx/wxprec.h"


#if wxUSE_PROPGRID

#ifndef WX_PRECOMP
    #include "wx/defs.h"
    #include "wx/object.h"
    #include "wx/hash.h"
    #include "wx/string.h"
    #include "wx/log.h"
    #include "wx/event.h"
    #include "wx/window.h"
    #include "wx/panel.h"
    #include "wx/dc.h"
    #include "wx/dcmemory.h"
    #include "wx/button.h"
    #include "wx/pen.h"
    #include "wx/brush.h"
    #include "wx/cursor.h"
    #include "wx/dialog.h"
    #include "wx/settings.h"
    #include "wx/msgdlg.h"
    #include "wx/choice.h"
    #include "wx/stattext.h"
    #include "wx/scrolwin.h"
    #include "wx/dirdlg.h"
    #include "wx/sizer.h"
    #include "wx/textdlg.h"
    #include "wx/filedlg.h"
    #include "wx/statusbr.h"
    #include "wx/intl.h"
    #include "wx/frame.h"
    #include "wx/textctrl.h"
    #include "wx/settings.h"
#endif

#include "deskgui/univ/propgrid/propgrid.h"
#include "deskgui/univ/propgrid/editors.h"
#include "deskgui/univ/propgrid/private.h"
#include "deskgui/univ/theme.h"          // ibThemeEngine — whose scheme the grid's colours are (RegainColours)
#include "deskgui/univ/colourScheme.h"   // wxTHEME_COLOUR
#include "deskgui/univ/renderer.h"       // ibRenderer — the theme's, what draws the expander
#include "deskgui/univ/messageDialog.h"  // ibMessageBox — the property's error, the library's box

#include "deskgui/univ/ctrl/textCtrl.h"      // ibTextCtrl — the text editor, the label editor
#include "deskgui/univ/ctrl/comboBox.h"      // ibComboBox — the choice editor

#include "wx/timer.h"
#include "wx/dcbuffer.h"
#include "wx/scopeguard.h"
#include "wx/display.h"

#include <memory>   // std::unique_ptr — the paint's buffer

// Two pics for the expand / collapse buttons.
// Files are not supplied with this project (since it is
// recommended to use either custom or native rendering).
// If you want them, get ibTreeMultiCtrl by Jorgen Bodde,
// and copy xpm files from archive to ibPropertyGrid src directory
// (and also comment/undef ibPG_ICON_WIDTH in private.h).
#ifndef ibPG_ICON_WIDTH
  #if defined(__WXMAC__)
    #include "mac_collapse.xpm"
    #include "mac_expand.xpm"
  #elif defined(__WXGTK__)
    #include "linux_collapse.xpm"
    #include "linux_expand.xpm"
  #else
    #include "default_collapse.xpm"
    #include "default_expand.xpm"
  #endif
#endif


#define ibPG_YSPACING_MIN               1
#define ibPG_DEFAULT_VSPACING           2 // This matches .NET propertygrid's value,
                                          // but causes normal combobox to spill out under MSW
#define ibPG_DEFAULT_CURSOR             wxNullCursor
#define ibPG_PIXELS_PER_UNIT            m_lineHeight

//#define ibPG_TOOLTIP_DELAY              1000

// This is the number of pixels the expander button inside
// property cells (i.e. not in the grey margin area are
// adjusted.
#define IN_CELL_EXPANDER_BUTTON_X_ADJUST    2

namespace
{

// Bit values for ibPropertyGrid::m_coloursCustomized.
enum CustomColour
{
    CustomColour_None           = 0x0000,
    CustomColour_Margin         = 0x0001,
    CustomColour_CaptionBg      = 0x0002,
    CustomColour_CaptionText    = 0x0004,
    CustomColour_CellBg         = 0x0008,
    CustomColour_CellText       = 0x0010,
    CustomColour_SelectionBg    = 0x0020,
    CustomColour_SelectionText  = 0x0040,
    CustomColour_Line           = 0x0080,
    CustomColour_DisabledText   = 0x0100
};

} // anonymous namespace

// -----------------------------------------------------------------------

#if wxUSE_INTL
void ibPropertyGrid::AutoGetTranslation ( bool enable )
{
    ibPGGlobalVars->m_autoGetTranslation = enable;
}
#else
void ibPropertyGrid::AutoGetTranslation ( bool ) { }
#endif

// -----------------------------------------------------------------------

const char ibPropertyGridNameStr[] = "ibPropertyGrid";

// -----------------------------------------------------------------------
// Statics in one class for easy destruction.
// -----------------------------------------------------------------------

// This is the real global ibPGGlobalVarsClass object allocated on demand.
static ibPGGlobalVarsClass* g_PGGlobalVars = nullptr;

#include "wx/module.h"

class ibPGGlobalVarsClassManager : public wxModule
{
    wxDECLARE_DYNAMIC_CLASS(ibPGGlobalVarsClassManager);
public:
    ibPGGlobalVarsClassManager() = default;
    virtual bool OnInit() override { return true; }
    virtual void OnExit() override { wxDELETE(g_PGGlobalVars); }
};

wxIMPLEMENT_DYNAMIC_CLASS(ibPGGlobalVarsClassManager, wxModule);


// When ibPG is loaded dynamically after the application is already running
// then the built-in module system won't pick this one up.  Add it manually.
void ibPGInitResourceModule()
{
    wxModule* module = new ibPGGlobalVarsClassManager;
    wxModule::RegisterModule(module);
    wxModule::InitializeModules();
}

// This object is a proxy that allows us to only create ibPGGlobalVarsClass
// object when it's really needed. Note that we need to use an object for this
// instead of a function for compatibility reasons: a lot of existing code uses
// ibPGGlobalVars directly.
ibPGGlobalVarsPtr ibPGGlobalVars;

ibPGGlobalVarsClass* ibPGGlobalVarsPtr::operator->() const
{
    if ( !g_PGGlobalVars )
        g_PGGlobalVars = new ibPGGlobalVarsClass();

    return g_PGGlobalVars;
}

bool ibPGGlobalVarsPtr::operator!() const
{
    return !g_PGGlobalVars;
}

ibPGGlobalVarsPtr::operator bool() const
{
    return !!g_PGGlobalVars;
}

ibPGGlobalVarsClass::ibPGGlobalVarsClass()
    : m_fontFamilyChoices(nullptr)
    , m_defaultRenderer(new ibPGDefaultRenderer())
    , m_autoGetTranslation(false)
    , m_offline(0)
    , m_extraStyle(0)
    , m_warnings(0)
{

    /* TRANSLATORS: Name of Boolean false value */
    m_boolChoices.Add(_("False"));
    /* TRANSLATORS: Name of Boolean true value */
    m_boolChoices.Add(_("True"));
}


ibPGGlobalVarsClass::~ibPGGlobalVarsClass()
{
    delete m_defaultRenderer;

    // This will always have one ref
    delete m_fontFamilyChoices;

#if wxUSE_VALIDATORS
    for ( wxValidator* val : m_arrValidators )
    {
        delete val;
    }
#endif

    // Destroy editor class instances.
    // iterate over all the elements in the class
    for( const auto& vt_it : m_mapEditorClasses )
    {
        delete vt_it.second;
    }

    // Make sure the global pointers have been reset
    wxASSERT(ibPG_EDITOR(TextCtrl) == nullptr);
    wxASSERT(ibPG_EDITOR(ChoiceAndButton) == nullptr);

}

void ibPropertyGridInitGlobalsIfNeeded()
{
}

// -----------------------------------------------------------------------
// ibPropertyGrid
// -----------------------------------------------------------------------

#ifndef ibPG_ICON_WIDTH
namespace
{
    // create two bitmap nodes for drawing
    static wxBitmap s_expandbmp(expand_xpm);
    static wxBitmap s_collbmp(collapse_xpm);
}
#endif

wxIMPLEMENT_DYNAMIC_CLASS(ibPropertyGrid, ibControl);

wxBEGIN_EVENT_TABLE(ibPropertyGrid, ibControl)
  EVT_IDLE(ibPropertyGrid::OnIdle)
  EVT_PAINT(ibPropertyGrid::OnPaint)
  EVT_SIZE(ibPropertyGrid::OnResize)
  EVT_ENTER_WINDOW(ibPropertyGrid::OnMouseEntry)
  EVT_LEAVE_WINDOW(ibPropertyGrid::OnMouseEntry)
  EVT_MOUSE_CAPTURE_CHANGED(ibPropertyGrid::OnCaptureChange)
  EVT_SCROLLWIN(ibPropertyGrid::OnScrollEvent)
  EVT_CHILD_FOCUS(ibPropertyGrid::OnChildFocusEvent)
  EVT_SET_FOCUS(ibPropertyGrid::OnFocusEvent)
  EVT_KILL_FOCUS(ibPropertyGrid::OnFocusEvent)
  EVT_SYS_COLOUR_CHANGED(ibPropertyGrid::OnSysColourChanged)
  EVT_DPI_CHANGED(ibPropertyGrid::OnDPIChanged)
  EVT_MOTION(ibPropertyGrid::OnMouseMove)
  EVT_LEFT_DOWN(ibPropertyGrid::OnMouseClick)
  EVT_LEFT_UP(ibPropertyGrid::OnMouseUp)
  EVT_RIGHT_UP(ibPropertyGrid::OnMouseRightClick)
  EVT_LEFT_DCLICK(ibPropertyGrid::OnMouseDoubleClick)
  EVT_KEY_DOWN(ibPropertyGrid::OnKey)
wxEND_EVENT_TABLE()

// -----------------------------------------------------------------------

ibPropertyGrid::ibPropertyGrid()
    : wxScrolled<ibControl>()
{
    Init1();
}

// -----------------------------------------------------------------------

ibPropertyGrid::ibPropertyGrid( wxWindow *parent,
                                wxWindowID id,
                                const wxPoint& pos,
                                const wxSize& size,
                                long style,
                                const wxString& name )
    : wxScrolled<ibControl>()
{
    Init1();
    Create(parent,id,pos,size,style,name);
}

// -----------------------------------------------------------------------

bool ibPropertyGrid::Create( wxWindow *parent,
                             wxWindowID id,
                             const wxPoint& pos,
                             const wxSize& size,
                             long style,
                             const wxString& name )
{

    if (!(style&wxBORDER_MASK))
    {
        style |= wxBORDER_THEME;
    }

    style |= wxVSCROLL;

    // Filter out wxTAB_TRAVERSAL - we will handle TABs manually
    style &= ~(wxTAB_TRAVERSAL);
    style |= wxWANTS_CHARS;

    ibControl::Create(parent, id, pos, size,
                      (style & wxWINDOW_STYLE_MASK) | wxScrolledWindowStyle,
                      wxDefaultValidator,
                      name);

    m_windowStyle |= (style & ibPG_WINDOW_STYLE_MASK);

    Init2();

    return true;
}

// -----------------------------------------------------------------------

//
// Initialize values to defaults
//
void ibPropertyGrid::Init1()
{
    // Register editor classes, if necessary.
    if ( ibPGGlobalVars->m_mapEditorClasses.empty() )
        ibPropertyGrid::RegisterDefaultEditors();

    m_validatingEditor = 0;
    m_iFlags = 0;
    m_pState = nullptr;
    m_wndEditor = m_wndEditor2 = nullptr;
    m_selColumn = 1;
    m_colHover = 1;
    m_propHover = nullptr;
    m_labelEditor = nullptr;
    m_labelEditorProperty = nullptr;
    m_eventObject = this;
    m_curFocused = nullptr;
    m_processedEvent = nullptr;
    m_tlp = nullptr;
    m_sortFunction = nullptr;
    m_inDoPropertyChanged = false;
    m_inCommitChangesFromEditor = false;
    m_inDoSelectProperty = false;
    m_inOnValidationFailure = false;
    m_permanentValidationFailureBehavior = ibPGVFBFlags::Default;
    m_dragStatus = 0;
    m_editorFocused = false;

    // Set up default unspecified value 'colour'
    m_unspecifiedAppearance.SetFgCol(*wxLIGHT_GREY);

    // Set default keys
    AddActionTrigger(ibPGKeyboardAction::NextProperty, WXK_RIGHT);
    AddActionTrigger(ibPGKeyboardAction::NextProperty, WXK_DOWN);
    AddActionTrigger(ibPGKeyboardAction::PrevProperty, WXK_LEFT);
    AddActionTrigger(ibPGKeyboardAction::PrevProperty, WXK_UP);
    AddActionTrigger(ibPGKeyboardAction::ExpandProperty, WXK_RIGHT);
    AddActionTrigger(ibPGKeyboardAction::CollapseProperty, WXK_LEFT);
    AddActionTrigger(ibPGKeyboardAction::CancelEdit, WXK_ESCAPE);
    AddActionTrigger(ibPGKeyboardAction::PressButton, WXK_DOWN, wxMOD_ALT);
    AddActionTrigger(ibPGKeyboardAction::PressButton, WXK_F4);

    m_coloursCustomized = CustomColour_None;

    m_doubleBuffer = nullptr;

    m_lineHeight = 0;

    m_width = m_height = 0;

    /* TRANSLATORS: Text  displayed for unspecified value */
    m_commonValues.push_back(new ibPGCommonValue(_("Unspecified"), ibPGGlobalVars->m_defaultRenderer) );
    m_cvUnspecified = 0;

    m_chgInfo_changedProperty = nullptr;
}

// -----------------------------------------------------------------------

//
// Initialize after parent etc. set
//
void ibPropertyGrid::Init2()
{
    wxASSERT( !(m_iFlags & ibPG_FL_INITIALIZED ) );

#ifdef __WXMAC__
   // Smaller controls on Mac
   SetWindowVariant(wxWINDOW_VARIANT_SMALL);
#endif

    // Now create state, if one didn't exist already
    // (ibPropertyGridManager might have created it for us).
    if ( !m_pState )
    {
        m_pState = CreateState();
        m_pState->m_pPropGrid = this;
        m_iFlags |= ibPG_FL_CREATEDSTATE;
    }

    if ( !(m_windowStyle & ibPG_SPLITTER_AUTO_CENTER) )
        m_pState->m_dontCenterSplitter = true;

    if ( m_windowStyle & ibPG_HIDE_CATEGORIES )
    {
        m_pState->InitNonCatMode();

        m_pState->m_properties = m_pState->m_abcArray;
    }

    GetClientSize(&m_width,&m_height);

#ifndef ibPG_ICON_WIDTH
    // calculate average font height for bitmap centering

    m_iconWidth = s_expandbmp.GetWidth();
    m_iconHeight = s_expandbmp.GetHeight();
#else
    m_iconWidth = FromDIP(ibPG_ICON_WIDTH);
    m_iconHeight = FromDIP(ibPG_ICON_WIDTH);
#endif

    m_gutterWidth = wxMax(0, (FromDIP(16) - m_iconWidth) / 2);
    m_subgroup_extramargin = m_iconWidth + m_gutterWidth;

    m_curcursor = wxCURSOR_ARROW;
    m_cursorSizeWE = wxCursor(wxCURSOR_SIZEWE);

    // adjust bitmap icon y position so they are centered
    m_vspacing = ibPG_DEFAULT_VSPACING;

    CalculateFontAndBitmapStuff( m_vspacing );

    // Allocate cell data
    m_propertyDefaultCell.SetEmptyData();
    m_categoryDefaultCell.SetEmptyData();

    RegainColours();

    // This helps with flicker
    SetBackgroundStyle( wxBG_STYLE_PAINT );

    // Rely on native double-buffering by default.
#if wxALWAYS_NATIVE_DOUBLE_BUFFER
    SetExtraStyle(GetExtraStyle() | ibPG_EX_NATIVE_DOUBLE_BUFFERING);
#endif // wxALWAYS_NATIVE_DOUBLE_BUFFER

    // Hook the top-level parent
    m_tlpClosed = nullptr;
    m_tlpClosedTime = 0;

    // set virtual size to this window size
    wxSize clientSize = GetClientSize();
    SetVirtualSize(clientSize);

    m_iFlags |= ibPG_FL_INITIALIZED;

    wxSize wndsize = GetSize();
    m_ncWidth = wndsize.GetWidth();

    // Need to call OnResize handler or size given in constructor/Create
    // will never work.
    wxSizeEvent sizeEvent(wndsize,0);
    OnResize(sizeEvent);
}

// -----------------------------------------------------------------------

ibPropertyGrid::~ibPropertyGrid()
{
    SendDestroyEvent();

#if wxUSE_THREADS
    wxCriticalSectionLocker lock(ibPGGlobalVars->m_critSect);
#endif

    //
    // Remove grid and property pointers from live ibPropertyGridEvents.
    for ( ibPropertyGridEvent* evt : m_liveEvents )
    {
        evt->SetPropertyGrid(nullptr);
        evt->SetProperty(nullptr);
    }
    m_liveEvents.clear();

    if ( m_processedEvent )
    {
        // All right... we are being deleted while ibPropertyGrid event
        // is being sent. Make sure that event propagates as little
        // as possible (although usually this is not enough to prevent
        // a crash).
        m_processedEvent->Skip(false);
        m_processedEvent->StopPropagation();

        // Let's use a message box to make the message appear more
        // reliably (and *before* the crash can happen).
        ibMessageBox(wxS("ibPropertyGrid was being destroyed in an event ")
                       wxS("generated by it. This usually leads to a crash ")
                       wxS("so it is recommended to destroy the control ")
                       wxS("at idle time instead."));
    }

    DoSelectProperty(nullptr, ibPGSelectPropertyFlags::NoValidate| ibPGSelectPropertyFlags::DontSendEvent);

    // This should do prevent things from going too badly wrong
    m_iFlags &= ~(ibPG_FL_INITIALIZED);

    if ( m_iFlags & ibPG_FL_MOUSE_CAPTURED )
        ReleaseMouse();

    // Call with nullptr to disconnect event handling
    if ( HasExtraStyle(ibPG_EX_ENABLE_TLP_TRACKING) )
    {
        OnTLPChanging(nullptr);

        wxASSERT_MSG( !IsEditorsValueModified(),
                      wxS("Most recent change in property editor was ")
                      wxS("lost!!! (if you don't want this to happen, ")
                      wxS("close your frames and dialogs using ")
                      wxS("Close(false).)") );
    }

    if ( m_processedEvent )
    {
        // We are inside event handler and we cannot delete
        // editor objects immediately. They have to be deleted
        // later on in the global idle handler.
        while ( !m_deletedEditorObjects.empty() )
        {
            wxObject* obj = m_deletedEditorObjects.back();
            m_deletedEditorObjects.pop_back();

            wxPendingDelete.Append(obj);
        }
    }
    else
    {
        // Delete pending editor controls
        DeletePendingObjects();
    }

    if ( m_doubleBuffer )
        delete m_doubleBuffer;

    if ( m_iFlags & ibPG_FL_CREATEDSTATE )
        delete m_pState;

    // Delete common value records
    for ( ibPGCommonValue* v : m_commonValues )
    {
        delete v;
    }
}

// -----------------------------------------------------------------------

bool ibPropertyGrid::Destroy()
{
    if ( m_iFlags & ibPG_FL_MOUSE_CAPTURED )
        ReleaseMouse();

    return ibControl::Destroy();
}

// -----------------------------------------------------------------------

ibPropertyGridPageState* ibPropertyGrid::CreateState() const
{
    return new ibPropertyGridPageState();
}

// -----------------------------------------------------------------------
// ibPropertyGrid overridden wxWindow methods
// -----------------------------------------------------------------------

void ibPropertyGrid::SetWindowStyleFlag( long style )
{
    long old_style = m_windowStyle;

    if ( m_iFlags & ibPG_FL_INITIALIZED )
    {
        wxASSERT( m_pState );

        if ( !(style & ibPG_HIDE_CATEGORIES) && (old_style & ibPG_HIDE_CATEGORIES) )
        {
        // Enable categories
            EnableCategories( true );
        }
        else if ( (style & ibPG_HIDE_CATEGORIES) && !(old_style & ibPG_HIDE_CATEGORIES) )
        {
        // Disable categories
            EnableCategories( false );
        }
        if ( !(old_style & ibPG_AUTO_SORT) && (style & ibPG_AUTO_SORT) )
        {
            //
            // Auto sort enabled
            //
            if ( !IsFrozen() )
                PrepareAfterItemsAdded();
            else
                m_pState->m_itemsAdded = true;
        }
#if wxUSE_TOOLTIPS
        if ( !(old_style & ibPG_TOOLTIPS) && (style & ibPG_TOOLTIPS) )
        {
            //
            // Tooltips enabled
            //
            //wxToolTip::SetDelay(ibPG_TOOLTIP_DELAY);
        }
        else if ( (old_style & ibPG_TOOLTIPS) && !(style & ibPG_TOOLTIPS) )
        {
            //
            // Tooltips disabled
            //
            UnsetToolTip();
        }
#endif // wxUSE_TOOLTIPS
    }

    ibControl::SetWindowStyleFlag ( style );

    if ( m_iFlags & ibPG_FL_INITIALIZED )
    {
        if ( (old_style & ibPG_HIDE_MARGIN) != (style & ibPG_HIDE_MARGIN) )
        {
            CalculateFontAndBitmapStuff( m_vspacing );
            Refresh();
        }
    }
}

// -----------------------------------------------------------------------

void ibPropertyGrid::DoThaw()
{
    if ( !IsFrozen() )
    {
        ibControl::DoThaw();
        RecalculateVirtualSize();
        Refresh();

        // Force property re-selection
        // NB: We must copy the selection.
        ibArrayPGProperty selection = m_pState->m_selection;
        DoSetSelection(selection, ibPGSelectPropertyFlags::Force | ibPGSelectPropertyFlags::Nonvisible);
    }
}

// -----------------------------------------------------------------------

bool ibPropertyGrid::DoAddToSelection( ibPGProperty* prop, ibPGSelectPropertyFlags selFlags )
{
    wxCHECK( prop, false );

    if ( !HasExtraStyle(ibPG_EX_MULTIPLE_SELECTION) )
        return DoSelectProperty(prop, selFlags);

    ibArrayPGProperty& selection = m_pState->m_selection;

    if ( selection.empty() )
    {
        return DoSelectProperty(prop, selFlags);
    }
    else
    {
        // For categories, only one can be selected at a time
        if ( prop->IsCategory() || selection[0]->IsCategory() )
            return true;

        selection.push_back(prop);

        if ( !(selFlags & ibPGSelectPropertyFlags::DontSendEvent) )
        {
            SendEvent( ibEVT_PG_SELECTED, prop, nullptr );
        }

        DrawItem(prop);
    }

    return true;
}

// -----------------------------------------------------------------------

bool ibPropertyGrid::DoRemoveFromSelection( ibPGProperty* prop, ibPGSelectPropertyFlags selFlags )
{
    wxCHECK( prop, false );
    bool res;

    ibArrayPGProperty& selection = m_pState->m_selection;
    if ( selection.size() <= 1 )
    {
        res = DoSelectProperty(nullptr, selFlags);
    }
    else
    {
        m_pState->DoRemoveFromSelection(prop);
        DrawItem(prop);
        res = true;
    }

    return res;
}

// -----------------------------------------------------------------------

bool ibPropertyGrid::DoSelectAndEdit( ibPGProperty* prop,
                                      unsigned int colIndex,
                                      ibPGSelectPropertyFlags selFlags )
{
    //
    // NB: Enable following if label editor background colour is
    //     ever changed to any other than m_colSelBack.
    //
    // We use this workaround to prevent visible flicker when editing
    // a cell. At least on wxMSW, there is a difficult to find
    // (and perhaps prevent) redraw somewhere between making property
    // selected and enabling label editing.
    //
    //wxColour prevColSelBack = m_colSelBack;
    //m_colSelBack = wxSystemSettings::GetColour( wxSYS_COLOUR_WINDOW );

    bool res;

    if ( colIndex == 1 )
    {
        res = DoSelectProperty(prop, selFlags);
    }
    else
    {
        // send event
        DoClearSelection(false, ibPGSelectPropertyFlags::NoRefresh);

        if ( m_pState->m_editableColumns.find(colIndex) == m_pState->m_editableColumns.end() )
        {
            res = DoAddToSelection(prop, selFlags);
        }
        else
        {
            res = DoAddToSelection(prop, selFlags| ibPGSelectPropertyFlags::NoRefresh);

            DoBeginLabelEdit(colIndex, selFlags);
        }
    }

    //m_colSelBack = prevColSelBack;
    return res;
}

// -----------------------------------------------------------------------

bool ibPropertyGrid::AddToSelectionFromInputEvent( ibPGProperty* prop,
                                                   unsigned int colIndex,
                                                   wxMouseEvent* mouseEvent,
                                                   ibPGSelectPropertyFlags selFlags )
{
    const ibArrayPGProperty& selection = GetSelectedProperties();
    bool alreadySelected = m_pState->DoIsPropertySelected(prop);
    bool res = true;

    // Set to 2 if also add all items in between
    int addToExistingSelection = 0;

    if ( HasExtraStyle(ibPG_EX_MULTIPLE_SELECTION) )
    {
        if ( mouseEvent )
        {
            if ( mouseEvent->GetEventType() == wxEVT_RIGHT_DOWN ||
                 mouseEvent->GetEventType() == wxEVT_RIGHT_UP )
            {
                // Allow right-click for context menu without
                // disturbing the selection.
                if ( selection.size() <= 1 ||
                     !alreadySelected )
                    return DoSelectAndEdit(prop, colIndex, selFlags);
                return true;
            }
            else
            {
                if ( mouseEvent->ControlDown() )
                {
                    addToExistingSelection = 1;
                }
                else if ( mouseEvent->ShiftDown() )
                {
                    if ( !selection.empty() && !prop->IsCategory() )
                        addToExistingSelection = 2;
                    else
                        addToExistingSelection = 1;
                }
            }
        }
    }

    if ( addToExistingSelection == 1 )
    {
        // Add/remove one
        if ( !alreadySelected )
        {
            res = DoAddToSelection(prop, selFlags);
        }
        else if ( selection.size() > 1 )
        {
            res = DoRemoveFromSelection(prop, selFlags);
        }
    }
    else if ( addToExistingSelection == 2 )
    {
        // Add this, and all in between

        // Find top selected property
        ibPGProperty* topSelProp = selection[0];
        int topSelPropY = topSelProp->GetY();
        for ( unsigned int i=1; i<selection.size(); i++ )
        {
            ibPGProperty* p = selection[i];
            int y = p->GetY();
            if ( y < topSelPropY )
            {
                topSelProp = p;
                topSelPropY = y;
            }
        }

        ibPGProperty* startFrom;
        ibPGProperty* stopAt;

        if ( prop->GetY() <= topSelPropY )
        {
            // Property is above selection (or same)
            startFrom = prop;
            stopAt = topSelProp;
        }
        else
        {
            // Property is below selection
            startFrom = topSelProp;
            stopAt = prop;
        }

        // Iterate through properties in-between, and select them
        for ( ibPropertyGridIterator it = GetIterator(ibPG_ITERATE_VISIBLE, startFrom);
              !it.AtEnd();
              ++it )
        {
            ibPGProperty* p = *it;

            if ( !p->IsCategory() &&
                 !m_pState->DoIsPropertySelected(p) )
            {
                DoAddToSelection(p, selFlags);
            }

            if ( p == stopAt )
                break;
        }
    }
    else
    {
        res = DoSelectAndEdit(prop, colIndex, selFlags);
    }

    return res;
}

// -----------------------------------------------------------------------

void ibPropertyGrid::DoSetSelection( const ibArrayPGProperty& newSelection,
                                     ibPGSelectPropertyFlags selFlags )
{
    if ( !newSelection.empty() )
    {
        if ( !DoSelectProperty(newSelection[0], selFlags) )
            return;
    }
    else
    {
        DoClearSelection(false, selFlags);
    }

    for ( unsigned int i = 1; i < newSelection.size(); i++ )
    {
        DoAddToSelection(newSelection[i], selFlags);
    }

    Refresh();
}

// -----------------------------------------------------------------------

void ibPropertyGrid::MakeColumnEditable( unsigned int column,
                                         bool editable )
{
    // The second column is always editable. To make it read-only is a property
    // by property decision by setting its ibPGFlags::ReadOnly flag.
    wxASSERT_MSG
    (
         column != 1,
         wxS("Set ibPGFlags::ReadOnly property flag instead")
    );

    if ( editable )
    {
        m_pState->m_editableColumns.insert(column);
    }
    else
    {
        m_pState->m_editableColumns.erase(column);
    }
}

// -----------------------------------------------------------------------

void ibPropertyGrid::DoBeginLabelEdit( unsigned int colIndex,
                                       ibPGSelectPropertyFlags selFlags )
{
    ibPGProperty* selected = GetSelection();
    wxCHECK_RET(selected, wxS("No property selected"));
    wxCHECK_RET(colIndex != 1, wxS("Do not use this for column 1"));

    if ( !(selFlags & ibPGSelectPropertyFlags::DontSendEvent) )
    {
        if ( SendEvent( ibEVT_PG_LABEL_EDIT_BEGIN,
                        selected, nullptr, ibPGSelectPropertyFlags::Null,
                        colIndex ) )
            return;
    }

    wxString text;
    const ibPGCell* cell = nullptr;
    if ( selected->HasCell(colIndex) )
    {
        cell = &selected->GetCell(colIndex);
        if ( !cell->HasText() && colIndex == 0 )
            text = selected->GetLabel();
    }

    if ( !cell  )
    {
        if ( colIndex == 0 )
            text = selected->GetLabel();
        else
            cell = &selected->GetOrCreateCell(colIndex);
    }

    if ( cell && cell->HasText() )
        text = cell->GetText();

    DoEndLabelEdit(true, ibPGSelectPropertyFlags::NoValidate);  // send event

    m_selColumn = colIndex;

    wxRect r = GetEditorWidgetRect(selected, m_selColumn);

    wxWindow* tc = GenerateEditorTextCtrl(r.GetPosition(),
                                          r.GetSize(),
                                          text,
                                          nullptr,
                                          wxTE_PROCESS_ENTER,
                                          0,
                                          colIndex);

    tc->Bind(wxEVT_TEXT_ENTER, &ibPropertyGrid::OnLabelEditorEnterPress, this);
    tc->Bind(wxEVT_KEY_DOWN, &ibPropertyGrid::OnLabelEditorKeyPress, this);

    tc->SetFocus();

    m_labelEditor = wxStaticCast(tc, ibTextCtrl);
    // Get actual position within required rectangle
    m_labelEditorPosRel = m_labelEditor->GetPosition() - r.GetPosition();
    m_labelEditorProperty = selected;
}

// -----------------------------------------------------------------------

void
ibPropertyGrid::OnLabelEditorEnterPress( wxCommandEvent& WXUNUSED(event) )
{
    DoEndLabelEdit(true);
}

// -----------------------------------------------------------------------

void ibPropertyGrid::OnLabelEditorKeyPress( wxKeyEvent& event )
{
    int keycode = event.GetKeyCode();

    if ( keycode == WXK_ESCAPE )
    {
        DoEndLabelEdit(false);
    }
    else
    {
        HandleKeyEvent(event, true);
    }
}

// -----------------------------------------------------------------------

void ibPropertyGrid::DoEndLabelEdit( bool commit, ibPGSelectPropertyFlags selFlags )
{
    if ( !m_labelEditor )
        return;

    ibPGProperty* prop = m_labelEditorProperty;
    wxASSERT(prop);

    if ( commit )
    {
        const int labelColIdx = m_selColumn;

        if ( !(selFlags & ibPGSelectPropertyFlags::DontSendEvent) )
        {
            // Don't send ibEVT_PG_LABEL_EDIT_ENDING event recursively
            // for the same property and the same label.
            if ( m_processedEvent &&
                 m_processedEvent->GetEventType() == ibEVT_PG_LABEL_EDIT_ENDING &&
                 m_processedEvent->GetProperty() == prop )
            {
                return;
            }

            // ibPGSelectPropertyFlags::NoValidate is passed correctly in selFlags
            if ( SendEvent( ibEVT_PG_LABEL_EDIT_ENDING,
                            prop, nullptr, selFlags,
                            m_selColumn ) )
                return;
        }

        wxString text = m_labelEditor->GetValue();

        // Cell handling for label in column 0
        // is done internally in SetLabel() function
        if ( labelColIdx == 0 )
        {
            prop->SetLabel(text);
        }
        else
        {
            ibPGCell* cell = nullptr;
            if ( prop->HasCell(labelColIdx) )
            {
                cell = &prop->GetCell(labelColIdx);
            }
            else
            {
                cell = &prop->GetOrCreateCell(labelColIdx);
            }

            if ( cell && cell->HasText() )
            {
                cell->SetText(text);
            }
        }
    }

    m_selColumn = 1;
    int wasFocused = m_iFlags & ibPG_FL_FOCUSED;

    DestroyEditorWnd(m_labelEditor);

    m_labelEditor = nullptr;
    m_labelEditorProperty = nullptr;

    // Fix focus (needed at least on wxGTK)
    if ( wasFocused )
        SetFocusOnCanvas();

    DrawItem(prop);
}

// -----------------------------------------------------------------------

void ibPropertyGrid::SetExtraStyle( long exStyle )
{
    if ( exStyle & ibPG_EX_ENABLE_TLP_TRACKING )
        OnTLPChanging(::wxGetTopLevelParent(this));
    else
        OnTLPChanging(nullptr);

    if ( exStyle & ibPG_EX_NATIVE_DOUBLE_BUFFERING )
    {
        // Only apply ibPG_EX_NATIVE_DOUBLE_BUFFERING if the window
        // truly was double-buffered.
        if ( !IsDoubleBuffered() )
        {
            exStyle &= ~(ibPG_EX_NATIVE_DOUBLE_BUFFERING);
        }
        else
        {
            wxDELETE(m_doubleBuffer);
        }
    }

    ibControl::SetExtraStyle( exStyle );

    if ( exStyle & ibPG_EX_INIT_NOCAT )
        m_pState->InitNonCatMode();

    if ( exStyle & ibPG_EX_HELP_AS_TOOLTIPS )
        m_windowStyle |= ibPG_TOOLTIPS;

    // Set global style
    ibPGGlobalVars->m_extraStyle = exStyle;
}

// -----------------------------------------------------------------------

// returns the best acceptable minimal size
wxSize ibPropertyGrid::DoGetBestSize() const
{
    int lineHeight = wxMax(FromDIP(15), m_lineHeight);

    // don't make the grid too tall (limit height to 10 items) but don't
    // make it too small either
    int numLines = wxMin
                   (
                    wxMax(m_pState->DoGetRoot()->GetChildCount(), 3),
                    10
                   );

    int width = m_marginWidth;
    for ( unsigned int i = 0; i < m_pState->GetColumnCount(); i++ )
    {
        width += m_pState->GetColumnFitWidth(m_pState->DoGetRoot(), i, true);
    }

    return wxSize(width, lineHeight*numLines + 40);
}

void ibPropertyGrid::DoEnable(bool enable)
{
    ibControl::DoEnable(enable);
    Refresh(false);
}

// a disabled grid keeps its window enabled (ibWindow::DoEnable), so it refuses the mouse and the keys itself, as the
// editor does (ibStyledTextCtrl::TryBefore)
bool ibPropertyGrid::TryBefore(wxEvent& event)
{
    if ( !IsEnabled() &&
         (event.IsKindOf(wxCLASSINFO(wxMouseEvent)) || event.IsKindOf(wxCLASSINFO(wxKeyEvent))) )
        return true;

    return ibControl::TryBefore(event);
}

// -----------------------------------------------------------------------

void ibPropertyGrid::OnTLPChanging( wxWindow* newTLP )
{
    if ( newTLP == m_tlp )
        return;

    wxMilliClock_t currentTime = ::wxGetLocalTimeMillis();

    //
    // Parent changed so let's re-determine and re-hook the
    // correct top-level window.
    if ( m_tlp )
    {
        m_tlp->Unbind(wxEVT_CLOSE_WINDOW, &ibPropertyGrid::OnTLPClose, this);
        m_tlpClosed = m_tlp;
        m_tlpClosedTime = currentTime;
    }

    if ( newTLP )
    {
        // Only accept new tlp if same one was not just dismissed.
        if ( newTLP != m_tlpClosed ||
             m_tlpClosedTime+250 < currentTime )
        {
            newTLP->Bind(wxEVT_CLOSE_WINDOW, &ibPropertyGrid::OnTLPClose, this);
            m_tlpClosed = nullptr;
        }
        else
        {
            newTLP = nullptr;
        }
    }

    m_tlp = newTLP;
}

// -----------------------------------------------------------------------

void ibPropertyGrid::OnTLPClose( wxCloseEvent& event )
{
    // ClearSelection forces value validation/commit.
    if ( event.CanVeto() && !DoClearSelection() )
    {
        event.Veto();
        return;
    }

    // Ok, it can close, set tlp pointer to nullptr. Some other event
    // handler can of course veto the close, but our OnIdle() should
    // then be able to regain the tlp pointer.
    OnTLPChanging(nullptr);

    event.Skip();
}

// -----------------------------------------------------------------------

bool ibPropertyGrid::Reparent( wxWindowBase *newParent )
{
    OnTLPChanging((wxWindow*)newParent);

    bool res = ibControl::Reparent(newParent);

    return res;
}

// -----------------------------------------------------------------------

void ibPropertyGrid::ScrollWindow(int dx, int dy, const wxRect* rect)
{
    ibControl::ScrollWindow(dx, dy, rect);
    if ( dx != 0 )
    {
        // Notify ibPropertyGridManager about the grid being scrolled horizontally
        // to scroll the column header, if present.
        SendEvent(ibEVT_PG_HSCROLL, dx);
    }
}
// -----------------------------------------------------------------------

void ibPropertyGrid::SetScrollbars(int pixelsPerUnitX, int pixelsPerUnitY,
                                   int noUnitsX, int noUnitsY,
                                   int xPos, int yPos, bool noRefresh)
{
    int oldX;
    CalcUnscrolledPosition(0, 0, &oldX, nullptr);
    wxScrolled<ibControl>::SetScrollbars(pixelsPerUnitX, pixelsPerUnitY,
                                  noUnitsX, noUnitsY, xPos, yPos, noRefresh);
    int newX;
    CalcUnscrolledPosition(0, 0, &newX, nullptr);
    if ( newX != oldX )
    {
        // Notify ibPropertyGridManager about the grid being scrolled horizontally
        // to scroll the column header, if present.
        SendEvent(ibEVT_PG_HSCROLL, oldX - newX);
    }
}

// -----------------------------------------------------------------------
// ibPropertyGrid Font and Colour Methods
// -----------------------------------------------------------------------

void ibPropertyGrid::CalculateFontAndBitmapStuff( int vspacing )
{
    int x = 0, y = 0;

    m_captionFont = ibControl::GetFont();

    GetTextExtent(wxS("jG"), &x, &y, nullptr, nullptr, &m_captionFont);
    m_fontHeight = y;

#ifdef ibPG_ICON_WIDTH
    wxSize iconSize = ibThemeEngine::Get()->GetRenderer()->GetExpanderSize(this);
    m_iconWidth = iconSize.GetWidth();
    m_iconHeight = iconSize.GetHeight();
#endif

    m_gutterWidth = wxMax(0, (FromDIP(16) - m_iconWidth) / 2);

    m_subgroup_extramargin = m_iconWidth + m_gutterWidth;

    int vdiv = 6;
    if ( vspacing <= 1 ) vdiv = 12;
    else if ( vspacing >= 3 ) vdiv = 3;

    m_spacingy = m_fontHeight / vdiv;
    if ( m_spacingy < ibPG_YSPACING_MIN )
        m_spacingy = ibPG_YSPACING_MIN;

    m_marginWidth = 0;
    if ( !(m_windowStyle & ibPG_HIDE_MARGIN) )
        m_marginWidth = m_gutterWidth*2 + m_iconWidth;

    m_captionFont.SetWeight(wxFONTWEIGHT_BOLD);
    GetTextExtent(wxS("jG"), &x, &y, nullptr, nullptr, &m_captionFont);

    m_lineHeight = m_fontHeight+(2*m_spacingy)+1;

    // button spacing
    m_buttonSpacingY = (m_lineHeight - m_iconHeight) / 2;
    if ( m_buttonSpacingY < 0 ) m_buttonSpacingY = 0;

    if ( m_pState )
        m_pState->CalculateFontAndBitmapStuff(vspacing);

    SetScrollRate(ibPG_PIXELS_PER_UNIT, ibPG_PIXELS_PER_UNIT);

    if ( m_iFlags & ibPG_FL_INITIALIZED )
        RecalculateVirtualSize();

    InvalidateBestSize();
}

// -----------------------------------------------------------------------

void ibPropertyGrid::OnSysColourChanged(wxSysColourChangedEvent& event)
{
    if ((m_iFlags & ibPG_FL_INITIALIZED)!=0) {
        RegainColours();
        Refresh();
    }
    event.Skip();
}

void ibPropertyGrid::OnDPIChanged(wxDPIChangedEvent &event)
{
    CalculateFontAndBitmapStuff(m_vspacing);

    if ( !HasExtraStyle(ibPG_EX_NATIVE_DOUBLE_BUFFERING) )
    {
        // Recreate the back buffer with correct DPI.
        delete m_doubleBuffer;
        m_doubleBuffer = nullptr;
        ReallocDoubleBufferIfNeeded();
    }

    Refresh();

    if ( ibPGProperty* const selected = GetSelection() )
        RefreshProperty(selected);

    event.Skip();
}

// -----------------------------------------------------------------------

// THE GRID'S COLOURS ARE THE THEME'S (Max, 2026-11-10: "the theme... is to set the default values we draw in our
// grid"): what wx derived from the platform (wxSystemSettings, a caption darkened from the grid lines, its
// text from the caption) the fork asks of the engine's colour scheme, as the editor does (stc) — a cell is the
// content (WINDOW), the margin a control's surface (CONTROL), a category row an inactive caption (TITLEBAR and its
// text), the selection the highlight. A colour set on the grid (Set*Colour, m_coloursCustomized) still wins.
void ibPropertyGrid::RegainColours()
{
    if ( !(m_coloursCustomized & CustomColour_CaptionBg) )
    {
        m_colCapBack = wxTHEME_COLOUR(TITLEBAR);
        m_categoryDefaultCell.GetData()->SetBgCol(m_colCapBack);
    }

    if ( !(m_coloursCustomized & CustomColour_Margin) )
        m_colMargin = wxTHEME_COLOUR(CONTROL);

    if ( !(m_coloursCustomized & CustomColour_CaptionText) )
    {
        m_colCapFore = wxTHEME_COLOUR(TITLEBAR_TEXT);
        m_categoryDefaultCell.GetData()->SetFgCol(m_colCapFore);
    }

    if ( !(m_coloursCustomized & CustomColour_CellBg) )
    {
        wxColour bgCol = wxTHEME_COLOUR(WINDOW);
        m_colPropBack = bgCol;
        m_propertyDefaultCell.GetData()->SetBgCol(bgCol);
        if ( !m_unspecifiedAppearance.GetBgCol().IsOk() )
            m_unspecifiedAppearance.SetBgCol(bgCol);
    }

    if ( !(m_coloursCustomized & CustomColour_CellText) )
    {
        wxColour fgCol = wxTHEME_COLOUR(CONTROL_TEXT);
        m_colPropFore = fgCol;
        m_propertyDefaultCell.GetData()->SetFgCol(fgCol);
        if ( !m_unspecifiedAppearance.GetFgCol().IsOk() )
            m_unspecifiedAppearance.SetFgCol(fgCol);
    }

    if ( !(m_coloursCustomized & CustomColour_SelectionBg) )
        m_colSelBack = wxTHEME_COLOUR(HIGHLIGHT);

    if ( !(m_coloursCustomized & CustomColour_SelectionText) )
        m_colSelFore = wxTHEME_COLOUR(HIGHLIGHT_TEXT);

    if ( !(m_coloursCustomized & CustomColour_Line) )
        m_colLine = m_colCapBack;

    if ( !(m_coloursCustomized & CustomColour_DisabledText) )
        m_colDisPropFore = wxTHEME_COLOUR(CONTROL_TEXT_DISABLED);

    m_colEmptySpace = wxTHEME_COLOUR(WINDOW);
}

// -----------------------------------------------------------------------

void ibPropertyGrid::ResetColours()
{
    m_coloursCustomized = CustomColour_None;

    RegainColours();

    Refresh();
}

// -----------------------------------------------------------------------

bool ibPropertyGrid::SetFont( const wxFont& font )
{
    // Must disable active editor.
    DoClearSelection();

    bool res = ibControl::SetFont( font );
    if ( res && GetParent()) // may not have been Create()ed yet if SetFont called from SetWindowVariant
    {
        CalculateFontAndBitmapStuff( m_vspacing );
        Refresh();
    }

    return res;
}

// -----------------------------------------------------------------------

void ibPropertyGrid::SetLineColour( const wxColour& col )
{
    m_colLine = col;
    m_coloursCustomized |= CustomColour_Line;
    Refresh();
}

// -----------------------------------------------------------------------

void ibPropertyGrid::SetMarginColour( const wxColour& col )
{
    m_colMargin = col;
    m_coloursCustomized |= CustomColour_Margin;
    Refresh();
}

// -----------------------------------------------------------------------

void ibPropertyGrid::SetCellBackgroundColour( const wxColour& col )
{
    m_colPropBack = col;
    m_coloursCustomized |= CustomColour_CellBg;

    m_propertyDefaultCell.GetData()->SetBgCol(col);
    m_unspecifiedAppearance.SetBgCol(col);

    Refresh();
}

// -----------------------------------------------------------------------

void ibPropertyGrid::SetCellTextColour( const wxColour& col )
{
    m_colPropFore = col;
    m_coloursCustomized |= CustomColour_CellText;

    m_propertyDefaultCell.GetData()->SetFgCol(col);
    m_unspecifiedAppearance.SetFgCol(col);

    Refresh();
}

// -----------------------------------------------------------------------

void ibPropertyGrid::SetEmptySpaceColour( const wxColour& col )
{
    m_colEmptySpace = col;

    Refresh();
}

// -----------------------------------------------------------------------

void ibPropertyGrid::SetCellDisabledTextColour( const wxColour& col )
{
    m_colDisPropFore = col;
    m_coloursCustomized |= CustomColour_DisabledText;
    Refresh();
}

// -----------------------------------------------------------------------

void ibPropertyGrid::SetSelectionBackgroundColour( const wxColour& col )
{
    m_colSelBack = col;
    m_coloursCustomized |= CustomColour_SelectionBg;
    Refresh();
}

// -----------------------------------------------------------------------

void ibPropertyGrid::SetSelectionTextColour( const wxColour& col )
{
    m_colSelFore = col;
    m_coloursCustomized |= CustomColour_SelectionText;
    Refresh();
}

// -----------------------------------------------------------------------

void ibPropertyGrid::SetCaptionBackgroundColour( const wxColour& col )
{
    m_colCapBack = col;
    m_coloursCustomized |= CustomColour_CaptionBg;

    m_categoryDefaultCell.GetData()->SetBgCol(col);

    Refresh();
}

// -----------------------------------------------------------------------

void ibPropertyGrid::SetCaptionTextColour( const wxColour& col )
{
    m_colCapFore = col;
    m_coloursCustomized |= CustomColour_CaptionText;

    m_categoryDefaultCell.GetData()->SetFgCol(col);

    Refresh();
}

// -----------------------------------------------------------------------
// ibPropertyGrid property adding and removal
// -----------------------------------------------------------------------

void ibPropertyGrid::PrepareAfterItemsAdded()
{
    if ( !m_pState || !m_pState->m_itemsAdded ) return;

    m_pState->m_itemsAdded = false;

    if ( m_windowStyle & ibPG_AUTO_SORT )
        Sort(ibPGPropertyValuesFlags::SortTopLevelOnly);

    RecalculateVirtualSize();

    // Fix editor position
    CorrectEditorWidgetPosY();
}

// -----------------------------------------------------------------------
// ibPropertyGrid property operations
// -----------------------------------------------------------------------

bool ibPropertyGrid::EnsureVisible( ibPGPropArg id )
{
    ibPG_PROP_ARG_CALL_PROLOG_RETVAL(false)

    Update();

    bool changed = false;

    // Is it inside collapsed section?
    if ( !p->IsVisible() )
    {
        // expand parents
        ibPGProperty* parent = p->GetParent();
        ibPGProperty* grandparent = parent->GetParent();

        if ( grandparent && grandparent != m_pState->DoGetRoot() )
            Expand( grandparent );

        Expand( parent );
        changed = true;
    }

    // Need to scroll?
    int vx, vy;
    GetViewStart(&vx,&vy);
    vy*=ibPG_PIXELS_PER_UNIT;

    int y = p->GetY();

    if ( y < vy )
    {
        Scroll(vx, y/ibPG_PIXELS_PER_UNIT );
        m_iFlags |= ibPG_FL_SCROLLED;
        changed = true;
    }
    else if ( (y+m_lineHeight) > (vy+m_height) )
    {
        Scroll(vx, (y-m_height+(m_lineHeight*2))/ibPG_PIXELS_PER_UNIT );
        m_iFlags |= ibPG_FL_SCROLLED;
        changed = true;
    }

    if ( changed )
        DrawItems( p, p );

    return changed;
}

// -----------------------------------------------------------------------
// ibPropertyGrid helper methods called by properties
// -----------------------------------------------------------------------

// Control font changer helper.
void ibPropertyGrid::SetCurControlBoldFont()
{
    wxWindow* editor = GetEditorControl();
    editor->SetFont( m_captionFont );
}

// -----------------------------------------------------------------------

wxPoint ibPropertyGrid::GetGoodEditorDialogPosition( ibPGProperty* p,
                                                     const wxSize& sz )
{
    if ( IsSmallScreen() )
    {
        // On small-screen devices, always show dialogs with default position and size.
        return wxDefaultPosition;
    }

    int splitterX = GetSplitterPosition();
    int x = splitterX;
    int y = p->GetY();

    wxCHECK_MSG( y >= 0, wxDefaultPosition, wxS("invalid y?") );

    ImprovedClientToScreen( &x, &y );

    wxRect displayRect = wxDisplay(this).GetGeometry();

    x -= displayRect.GetX();
    y -= displayRect.GetY();

    int new_x;
    int new_y;

    if ( x > (displayRect.GetWidth()/2) )
        // left
        new_x = x + (m_width-splitterX) - sz.x;
    else
        // right
        new_x = x;

    if ( y > (displayRect.GetHeight()/2) )
        // above
        new_y = y - sz.y;
    else
        // below
        new_y = y + m_lineHeight;

    return wxPoint(new_x + displayRect.GetX(), new_y + displayRect.GetY());
}

// -----------------------------------------------------------------------

wxString& ibPropertyGrid::ExpandEscapeSequences( wxString& dst_str, const wxString& src_str )
{
    dst_str.clear();

    if ( src_str.empty() )
    {
        return dst_str;
    }

    bool prev_is_slash = false;

    for ( wxUniChar a : src_str )
    {
        if ( a != wxS('\\') )
        {
            if ( !prev_is_slash )
            {
                dst_str << a;
            }
            else
            {
                if ( a == wxS('n') )
                    dst_str << wxS('\n');
                else if ( a == wxS('r') )
                    dst_str << wxS('\r');
                else if ( a == wxS('t') )
                    dst_str << wxS('\t');
                else
                    dst_str << a;
            }
            prev_is_slash = false;
        }
        else
        {
            if ( prev_is_slash )
            {
                dst_str << wxS('\\');
                prev_is_slash = false;
            }
            else
            {
                prev_is_slash = true;
            }
        }
    }
    return dst_str;
}

// -----------------------------------------------------------------------

wxString& ibPropertyGrid::CreateEscapeSequences( wxString& dst_str, const wxString& src_str )
{
    dst_str.clear();

    if ( src_str.empty() )
    {
        return dst_str;
    }

    for( wxUniChar a : src_str )
    {
        if ( a == wxS('\r') )
            // Carriage Return.
            dst_str << wxS("\\r");
        else if ( a == wxS('\n') )
            // Line Feed.
            dst_str << wxS("\\n");
        else if ( a == wxS('\t') )
            // Tab.
            dst_str << wxS("\\t");
        else if ( a == wxS('\\') )
            // Escape character (backslash).
            dst_str << wxS("\\\\");
        else
            dst_str << a;
    }
    return dst_str;
}

// -----------------------------------------------------------------------

bool ibPropertyGrid::IsSmallScreen()
{
    return wxSystemSettings::GetScreenType() <= wxSYS_SCREEN_PDA;
}

// -----------------------------------------------------------------------

// -----------------------------------------------------------------------

ibPGProperty* ibPropertyGrid::DoGetItemAtY( int y ) const
{
    // Outside?
    if ( y < 0 )
        return nullptr;

    unsigned int a = 0;
    return m_pState->DoGetRoot()->GetItemAtY(y, m_lineHeight, &a);
}

// -----------------------------------------------------------------------
// ibPropertyGrid graphics related methods
// -----------------------------------------------------------------------

// SEAM vs wx: the paint DC is the window's - the client area begins at its origin, inside the engine's border: the
// DC's origin is moved there and clipped to the client area (the buffer, larger than it, went over the border), the
// update rectangle is moved into the client area and cut to it - a box from the window's 0 went above it, its top
// negative as the unsigned row of DrawItems: nothing drawn, the buffer blitted black but for the rows refreshed alone
void ibPropertyGrid::OnPaint( wxPaintEvent& WXUNUSED(event) )
{
    const wxPoint origin = GetClientAreaOrigin();
    const wxRect rectClient(GetClientSize());

    wxPaintDC dcPaint(this);
    dcPaint.SetDeviceOrigin(origin.x, origin.y);
    wxDCClipper clip(dcPaint, rectClient);

    // the buffer, if used: destroyed before the paint DC, it blits itself there
    std::unique_ptr<wxBufferedDC> dcBuffered;
    if ( !HasExtraStyle(ibPG_EX_NATIVE_DOUBLE_BUFFERING) && m_doubleBuffer )
        dcBuffered.reset(new wxBufferedDC(&dcPaint, *m_doubleBuffer));

    wxDC& dc = dcBuffered ? static_cast<wxDC&>(*dcBuffered) : dcPaint;
    PrepareDC(dc);
    // Unused area will be cleared when drawing the items
/*
    dc.SetBackground(m_colEmptySpace);
    dc.Clear();
*/

    // Don't paint after destruction has begun
    if ( !HasInternalFlag(ibPG_FL_INITIALIZED) )
        return;

    // Find out where the window is scrolled to
    int vx, vy;
    GetViewStart(&vx, &vy);
    vx *= ibPG_PIXELS_PER_UNIT;
    vy *= ibPG_PIXELS_PER_UNIT;

    // Update everything inside the box - the client area's part of it
    wxRect r = GetUpdateRegion().GetBox();
    r.Offset(-origin);
    r.Intersect(rectClient);
    if ( !r.IsEmpty() )
    {
        r.Offset(vx, vy);

        // Repaint this rectangle
        DrawItems(dc, r.y, r.y + r.height-1, &r);
    }

    // We assume that the size set when grid is shown
    // is what is desired.
    SetInternalFlag(ibPG_FL_GOOD_SIZE_SET);
}

// -----------------------------------------------------------------------

void ibPropertyGrid::DrawExpanderButton( wxDC& dc, const wxRect& rect,
                                         ibPGProperty* property ) const
{
    // Prepare rectangle to be used
    wxRect r(rect);
    r.Offset(m_gutterWidth, m_buttonSpacingY);
    r.width = m_iconWidth; r.height = m_iconHeight;

    // wxRenderer functions are non-mutating in nature, so it
    // should be safe to cast "const ibPropertyGrid*" to "wxWindow*".
    // Hopefully this does not cause problems.
#ifndef ibPG_ICON_WIDTH
    wxBitmap bmp = property->IsExpanded() ? s_collbmp : s_expandbmp;
    dc.DrawBitmap(bmp, r.x, r.y, true);
#else
    ibThemeEngine::Get()->GetRenderer()->DrawTreeItemButton(
            const_cast<ibPropertyGrid*>(this),
            dc,
            r,
            property->IsExpanded() ? wxCONTROL_EXPANDED : wxCONTROL_NONE
        );
#endif
}

// -----------------------------------------------------------------------

//
// This is the one called by OnPaint event handler and others.
// topy and bottomy are already unscrolled (i.e. physical)
//
void ibPropertyGrid::DrawItems( wxDC& dc,
                                unsigned int topItemY,
                                unsigned int bottomItemY,
                                const wxRect* itemsRect )
{
    if ( IsFrozen() ||
         m_height < 1 ||
         bottomItemY < topItemY ||
         !m_pState )
        return;

    m_pState->EnsureVirtualHeight();

    wxRect tempItemsRect;
    if ( !itemsRect )
    {
        tempItemsRect = wxRect(0, topItemY,
                               m_pState->GetVirtualWidth(),
                               bottomItemY - topItemY + 1);
        itemsRect = &tempItemsRect;
    }

    // items added check
    if ( m_pState->m_itemsAdded ) PrepareAfterItemsAdded();

    if ( m_pState->DoGetRoot()->GetChildCount() > 0 )
    {
        int paintFinishY = DoDrawItems(dc, itemsRect) + 1;
        int drawBottomY = itemsRect->GetBottom();

        // Clear area beyond last painted property
        if ( paintFinishY < drawBottomY )
        {
            dc.SetPen(m_colEmptySpace);
            dc.SetBrush(m_colEmptySpace);
            dc.DrawRectangle(0, paintFinishY,
                                    m_width,
                                    drawBottomY-paintFinishY+1);
        }
    }
    else
    {
        // Just clear the area
        dc.SetPen(m_colEmptySpace);
        dc.SetBrush(m_colEmptySpace);
        dc.DrawRectangle(*itemsRect);
    }
}

// -----------------------------------------------------------------------

int ibPropertyGrid::DoDrawItems( wxDC& dc,
                                 const wxRect* itemsRect ) const
{
    const ibPGProperty* firstItem = DoGetItemAtY(itemsRect->y);
    if ( !firstItem ) // Signal a need to clear entire paint area if grid is empty
        return -1;

    if ( IsFrozen() || m_height < 1 )
        return itemsRect->GetBottom();

    wxCHECK_MSG( !m_pState->m_itemsAdded, itemsRect->GetBottom(),
                 wxS("no items added") );
    wxASSERT( m_pState->DoGetRoot()->GetChildCount() );

    int lh = m_lineHeight;
    int firstItemTopY = itemsRect->y;
    int lastItemBottomY = itemsRect->GetBottom();

    // Align y coordinates to item boundaries
    firstItemTopY -= firstItemTopY % lh;
    lastItemBottomY += lh - (lastItemBottomY % lh);
    lastItemBottomY -= 1;

    // Entire range outside scrolled, visible area?
    if ( firstItemTopY >= (int)m_pState->GetVirtualHeight() ||
         lastItemBottomY <= 0 )
        return itemsRect->GetBottom();

    wxCHECK_MSG( firstItemTopY < lastItemBottomY,
                 itemsRect->GetBottom(),
                 wxS("invalid y values") );

    /*
    wxLogDebug(wxS(" -> DoDrawItems(\"%s\" -> \"%s\"")
               wxS(" %i -> %i height=%i (ch=%i), itemsRect = 0x%lX %i,%i %ix%i)"),
        firstItem->GetLabel(),
        lastItem->GetLabel(),
        firstItemTopY, lastItemBottomY,
        (int)(lastItemBottomY - firstItemTopY),
        (int)m_height,
        (unsigned long)&itemsRect,
        itemsRect->x, itemsRect->y, itemsRect->width, itemsRect->height );
    */

    long windowStyle = m_windowStyle;

    int x = m_marginWidth;

    wxFont normalFont = GetFont();

    bool reallyFocused = (m_iFlags & ibPG_FL_FOCUSED) != 0;

    bool isPgEnabled = IsEnabled();

    //
    // Prepare some pens and brushes that are often changed to.
    //

    wxBrush marginBrush(m_colMargin);
    wxPen marginPen(m_colMargin);
    wxBrush capbgbrush(m_colCapBack,wxBRUSHSTYLE_SOLID);
    wxPen linepen(m_colLine,1,wxPENSTYLE_SOLID);

    // pen that has same colour as text
    wxPen outlinepen(m_colPropFore,1,wxPENSTYLE_SOLID);

    //
    // Clear margin with background colour
    //
    dc.SetBrush( marginBrush );
    if ( !(windowStyle & ibPG_HIDE_MARGIN) )
    {
        dc.SetPen( *wxTRANSPARENT_PEN );
        dc.DrawRectangle(-1,firstItemTopY-1,x+2,lastItemBottomY-firstItemTopY+2);
    }

    const ibPGProperty* firstSelected = GetSelection();
    const ibPropertyGridPageState* state = m_pState;
    const std::vector<int>& colWidths = state->m_colWidths;
    const unsigned int colCount = state->GetColumnCount();

    dc.SetFont(normalFont);

    ibPropertyGridConstIterator it( state, ibPG_ITERATE_VISIBLE, firstItem );
    int endScanBottomY = lastItemBottomY + lh;
    int y = firstItemTopY;

    //
    // Pre-generate list of visible properties.
    std::vector<ibPGProperty*> visPropArray;
    visPropArray.reserve((m_height/m_lineHeight)+6);

    for ( ; !it.AtEnd(); it.Next() )
    {
        const ibPGProperty* p = *it;

        if ( !p->HasFlag(ibPGFlags::Hidden) )
        {
            visPropArray.push_back(const_cast<ibPGProperty*>(p));

            if ( y > endScanBottomY )
                break;

            y += lh;
        }
    }

    visPropArray.push_back(nullptr);

    ibPGProperty* nextP = visPropArray[0];

    // Calculate splitters positions
    std::vector<int> splitterPos;
    splitterPos.reserve(colCount);
    int sx = x;
    for ( int cw : colWidths )
    {
        sx += cw;
        splitterPos.push_back(sx);
    }

    int viewLeftEdge = itemsRect->x;
    int viewRightEdge = viewLeftEdge + itemsRect->width - 1;
    // Determine columns range to be drawn
    unsigned int firstCol = 0;
    while ( firstCol < colCount-1 && splitterPos[firstCol] < viewLeftEdge )
        firstCol++;
    unsigned int lastCol = firstCol;
    while ( lastCol < colCount-1 && splitterPos[lastCol] < viewRightEdge )
        lastCol++;
    // Calculate position of the right edge of the last cell
    int cellX = splitterPos[lastCol]+ 1;

    y = firstItemTopY;
    for ( unsigned int arrInd=1;
          nextP && y <= lastItemBottomY;
          arrInd++ )
    {
        ibPGProperty* p = nextP;
        nextP = visPropArray[arrInd];

        int textMarginHere = x;
        int renderFlags = 0;

        int greyDepth = m_marginWidth;
        if ( !(windowStyle & ibPG_HIDE_CATEGORIES) )
            greyDepth += (((int)p->m_depthBgCol)-1) * m_subgroup_extramargin;

        // Use basic depth if in non-categoric mode and parent is base array.
        if ( !(windowStyle & ibPG_HIDE_CATEGORIES) || p->GetParent() != m_pState->DoGetRoot() )
        {
            textMarginHere += ((p->GetDepth()-1)*m_subgroup_extramargin);
        }

        // Paint margin area
        dc.SetBrush(marginBrush);
        dc.SetPen(marginPen);
        dc.DrawRectangle( 0, y, greyDepth, lh );

        dc.SetPen( linepen );

        int y2 = y + lh;

#ifdef __WXMSW__
        // Margin Edge
        // Modified by JACS to not draw a margin if ibPG_HIDE_MARGIN is specified, since it
        // looks better, at least under Windows when we have a themed border (the themed-window-specific
        // whitespace between the real border and the propgrid margin exacerbates the double-border look).

        // Is this or its parent themed?
        bool suppressMarginEdge = (GetWindowStyle() & ibPG_HIDE_MARGIN) &&
            (((GetWindowStyle() & wxBORDER_MASK) == wxBORDER_THEME) ||
            (((GetWindowStyle() & wxBORDER_MASK) == wxBORDER_NONE) && ((GetParent()->GetWindowStyle() & wxBORDER_MASK) == wxBORDER_THEME)));
        if (suppressMarginEdge)
        {
            // Blank out the margin edge
            dc.SetPen(wxPen(GetBackgroundColour()));
            dc.DrawLine( greyDepth, y, greyDepth, y2 );
            dc.SetPen( linepen );
        }
        else
#endif // __WXMSW__
        {
            dc.DrawLine(greyDepth, y, greyDepth, y2);
        }

        // Splitters
        for (unsigned int i = firstCol; i <= lastCol; i++)
        {
            dc.DrawLine(splitterPos[i], y, splitterPos[i], y2);
        }

        // Horizontal Line, below
        //   (not if both this and next is category caption)
        if ( p->IsCategory() &&
             nextP && nextP->IsCategory() )
            dc.SetPen(m_colCapBack);

        dc.DrawLine(greyDepth, y2 - 1, cellX, y2 - 1);

        //
        // Need to override row colours?
        wxColour rowFgCol;
        wxColour rowBgCol;

        bool isSelected = state->DoIsPropertySelected(p);

        if ( !isSelected )
        {
            // Disabled may get different colour.
            if ( !p->IsEnabled() )
            {
                renderFlags |= ibPGCellRenderer::Disabled |
                               ibPGCellRenderer::DontUseCellFgCol;
                rowFgCol = m_colDisPropFore;
            }
        }
        else
        {
            renderFlags |= ibPGCellRenderer::Selected;

            if ( !p->IsCategory() )
            {
                renderFlags |= ibPGCellRenderer::DontUseCellFgCol |
                               ibPGCellRenderer::DontUseCellBgCol;

                if ( reallyFocused )
                {
                    rowFgCol = (p == firstSelected) ? m_colSelFore : m_colPropFore;
                    rowBgCol = m_colSelBack;
                }
                else if ( isPgEnabled )
                {
                    rowFgCol = m_colPropFore;
                    rowBgCol = m_colMargin;
                }
                else
                {
                    rowFgCol = m_colDisPropFore;
                    rowBgCol = m_colMargin;
                }
            }
        }

        wxBrush rowBgBrush;

        if ( rowBgCol.IsOk() )
            rowBgBrush = wxBrush(rowBgCol);

        if ( HasInternalFlag(ibPG_FL_CELL_OVERRIDES_SEL) )
            renderFlags = renderFlags & ~ibPGCellRenderer::DontUseCellColours;

        //
        // Fill additional margin area with background colour of first cell
        if ( greyDepth < textMarginHere )
        {
            if ( !(renderFlags & ibPGCellRenderer::DontUseCellBgCol) )
            {
                ibPGCell& cell = p->GetCell(0);
                rowBgCol = cell.GetBgCol();
                rowBgBrush = wxBrush(rowBgCol);
            }
            dc.SetBrush(rowBgBrush);
            dc.SetPen(rowBgCol);
            dc.DrawRectangle(greyDepth+1, y,
                             textMarginHere-greyDepth, lh-1);
        }

        bool fontChanged = false;

        // Expander button rectangle
        wxRect butRect( ((p->GetDepth() - 1) * m_subgroup_extramargin),
                        y,
                        m_marginWidth,
                        lh );

        // Default cell rect fill the entire row
        wxRect cellRect(greyDepth, y, cellX - greyDepth, lh-1);

        bool isCategory = p->IsCategory();

        if ( isCategory )
        {
            dc.SetFont(m_captionFont);
            fontChanged = true;

            if ( renderFlags & ibPGCellRenderer::DontUseCellBgCol )
            {
                dc.SetBrush(rowBgBrush);
                dc.SetPen(rowBgCol);
            }

            if ( renderFlags & ibPGCellRenderer::DontUseCellFgCol )
            {
                dc.SetTextForeground(rowFgCol);
            }
        }
        else
        {
            // Fine tune button rectangle to actually fit the cell
            if ( butRect.x > 0 )
                butRect.x += IN_CELL_EXPANDER_BUTTON_X_ADJUST;

            if ( p->HasFlag(ibPGFlags::Modified) &&
                 (windowStyle & ibPG_BOLD_MODIFIED) )
            {
                dc.SetFont(m_captionFont);
                fontChanged = true;
            }

            // Magic fine-tuning for non-category rows
            cellRect.x += 1;
        }

        int firstCellWidth = colWidths[0] - (greyDepth - m_marginWidth);
        int firstCellX = cellRect.x;

        // Calculate cellRect.x for the last cell
        cellRect.x = cellX;

        // Draw cells from back to front so that we can easily tell if the
        // cell on the right was empty from text
        bool prevFilled = true;
        unsigned int ci = lastCol + 1;
        do
        {
            ci--;

            int textXAdd = 0;

            if ( ci == 0 )
            {
                textXAdd = textMarginHere - greyDepth;
                cellRect.width = firstCellWidth;
                cellRect.x = firstCellX;
            }
            else
            {
                int colWidth = colWidths[ci];
                cellRect.width = colWidth;
                cellRect.x -= colWidth;
            }

            // Merge with column to the right?
            if ( !prevFilled && isCategory )
            {
                cellRect.width += colWidths[ci+1];
            }

            if ( !isCategory )
                cellRect.width -= 1;

            wxWindow* cellEditor = nullptr;
            int cellRenderFlags = renderFlags;

            // Tree Item Button (must be drawn before clipping is set up)
            if ( ci == 0 && !HasFlag(ibPG_HIDE_MARGIN) && p->HasVisibleChildren() )
                DrawExpanderButton( dc, butRect, p );

            // Background
            if ( isSelected && (ci == 1 || ci == m_selColumn) )
            {
                if ( p == firstSelected )
                {
                    if ( ci == 1 && m_wndEditor )
                        cellEditor = m_wndEditor;
                    else if ( ci == m_selColumn && m_labelEditor )
                        cellEditor = m_labelEditor;
                }

                if ( cellEditor )
                {
                    wxColour editorBgCol =
                        cellEditor->GetBackgroundColour();
                    dc.SetBrush(editorBgCol);
                    dc.SetPen(editorBgCol);
                    dc.SetTextForeground(m_colPropFore);
                    dc.DrawRectangle(cellRect);

                    if ( m_dragStatus != 0 ||
                         (m_iFlags & ibPG_FL_CUR_USES_CUSTOM_IMAGE) )
                        cellEditor = nullptr;
                }
                else
                {
                    dc.SetBrush(m_colPropBack);
                    dc.SetPen(m_colPropBack);
                    dc.SetTextForeground(p->IsEnabled() ? m_colPropFore : m_colDisPropFore);
                }
            }
            else
            {
                if ( renderFlags & ibPGCellRenderer::DontUseCellBgCol )
                {
                    dc.SetBrush(rowBgBrush);
                    dc.SetPen(rowBgCol);
                }

                if ( renderFlags & ibPGCellRenderer::DontUseCellFgCol )
                {
                    dc.SetTextForeground(rowFgCol);
                }
            }

            dc.SetClippingRegion(cellRect);

            cellRect.x += textXAdd;
            cellRect.width -= textXAdd;

            // Foreground
            if ( !cellEditor )
            {
                ibPGCellRenderer* renderer;
                int cmnVal = p->GetCommonValue();
                if ( cmnVal == -1 || ci != 1 )
                {
                    renderer = p->GetCellRenderer(ci);
                    prevFilled = renderer->Render(dc, cellRect, this,
                                                  p, ci, -1,
                                                  cellRenderFlags );
                }
                else
                {
                    renderer = GetCommonValue(cmnVal)->GetRenderer();
                    prevFilled = renderer->Render(dc, cellRect, this,
                                                  p, ci, -1,
                                                  cellRenderFlags );
                }
            }
            else
            {
                prevFilled = true;
            }

            dc.DestroyClippingRegion();
        }
        while ( ci > firstCol );

        if ( fontChanged )
            dc.SetFont(normalFont);

        y += lh;
    }

    // Clear empty space beyond the right edge of the grid
    dc.SetPen(wxPen(m_colEmptySpace));
    dc.SetBrush(wxBrush(m_colEmptySpace));
    dc.DrawRectangle(cellX, firstItemTopY, viewRightEdge - cellX + 1, lastItemBottomY - firstItemTopY);

    return y - 1;
}

// -----------------------------------------------------------------------

wxRect ibPropertyGrid::GetPropertyRect( const ibPGProperty* p1, const ibPGProperty* p2 ) const
{
    if ( m_width < 10 || m_height < 10 ||
         !m_pState->DoGetRoot()->GetChildCount() ||
         p1 == nullptr )
        return wxRect(0,0,0,0);

    int vy = 0;

    //
    // Return rect which encloses the given property range
    // (in logical grid coordinates)
    //

    int visTop = p1->GetY();
    int visBottom;
    if ( p2 )
        visBottom = p2->GetY() + m_lineHeight;
    else
        visBottom = m_height + visTop;

    // If selected property is inside the range, we'll extend the range to include
    // control's size.
    ibPGProperty* selected = GetSelection();
    if ( selected )
    {
        int selectedY = selected->GetY();
        if ( selectedY >= visTop && selectedY < visBottom )
        {
            wxWindow* editor = GetEditorControl();
            if ( editor )
            {
                int visBottom2 = selectedY + editor->GetSize().y;
                if ( visBottom2 > visBottom )
                    visBottom = visBottom2;
            }
        }
    }

    return wxRect(0,visTop-vy,m_pState->GetVirtualWidth(),visBottom-visTop);
}

// -----------------------------------------------------------------------

void ibPropertyGrid::DrawItems( const ibPGProperty* p1, const ibPGProperty* p2 )
{
    if ( IsFrozen() )
        return;

    if ( m_pState->m_itemsAdded )
        PrepareAfterItemsAdded();

    wxRect r = GetPropertyRect(p1, p2);
    if ( !r.IsEmpty() )
    {
        // Convert rectangle from logical grid coordinates to physical ones
        int vx, vy;
        GetViewStart(&vx, &vy);
        vx *= ibPG_PIXELS_PER_UNIT;
        vy *= ibPG_PIXELS_PER_UNIT;
        r.Offset(-vx, -vy);
        RefreshRect(r);
        Update();
    }
}

// -----------------------------------------------------------------------

void ibPropertyGrid::RefreshProperty( ibPGProperty* p )
{
    wxCHECK_RET( p, wxS("invalid property id") );

    if ( m_pState->DoIsPropertySelected(p) || p->IsChildSelected(true) )
    {
        // NB: We must copy the selection.
        ibArrayPGProperty selection = m_pState->m_selection;
        ibPGSelectPropertyFlags selFlags = ibPGSelectPropertyFlags::Force;
        // We want to keep property's editor focused.
        if ( IsEditorFocused() )
            selFlags |= ibPGSelectPropertyFlags::Focus;
        DoSetSelection(selection, selFlags);
    }

    DrawItemAndChildren(p);
}

// -----------------------------------------------------------------------

void ibPropertyGrid::DrawItemAndValueRelated( ibPGProperty* p )
{
    wxCHECK_RET( p, wxS("invalid property id") );

    if ( IsFrozen() )
        return;

    // Draw item, children, and parent too, if it is not category
    ibPGProperty* parent = p->GetParent();

    while ( parent &&
            !parent->IsCategory() && !parent->IsRoot() )
    {
         DrawItem(parent);
         parent = parent->GetParent();
    }

    DrawItemAndChildren(p);
}

void ibPropertyGrid::DrawItemAndChildren( ibPGProperty* p )
{
    wxCHECK_RET( p, wxS("invalid property id") );

    // Do not draw if in non-visible page
    if ( p->GetParentState() != m_pState )
        return;

    // do not draw a single item if multiple pending
    if ( m_pState->m_itemsAdded || IsFrozen() )
        return;

    // Update child control.
    ibPGProperty* selected = GetSelection();
    if ( selected && selected->GetParent() == p )
        RefreshEditor();

    const ibPGProperty* lastDrawn = p->GetLastVisibleSubItem();

    DrawItems(p, lastDrawn);
}

// -----------------------------------------------------------------------

void ibPropertyGrid::Refresh( bool WXUNUSED(eraseBackground),
                              const wxRect *rect )
{
    PrepareAfterItemsAdded();

    // the theme engine's: the rectangle is the client area's, moved into the window's past the border
    ibControl::Refresh(false, rect);

#if ibPG_REFRESH_CONTROLS
    // I think this really helps only GTK+1.2
    if ( m_wndEditor ) m_wndEditor->Refresh();
    if ( m_wndEditor2 ) m_wndEditor2->Refresh();
#endif
}

// -----------------------------------------------------------------------
// ibPropertyGrid global operations
// -----------------------------------------------------------------------

void ibPropertyGrid::Clear()
{
    m_pState->DoClear();

    m_propHover = nullptr;

    RecalculateVirtualSize();

    // Need to clear some area at the end
    if ( !IsFrozen() )
        RefreshRect(wxRect(0, 0, m_width, m_height));
}

// -----------------------------------------------------------------------

bool ibPropertyGrid::EnableCategories( bool enable )
{
    DoClearSelection();

    if ( enable )
    {
        //
        // Enable categories
        //

        m_windowStyle &= ~(ibPG_HIDE_CATEGORIES);
    }
    else
    {
        //
        // Disable categories
        //
        m_windowStyle |= ibPG_HIDE_CATEGORIES;
    }

    if ( !m_pState->EnableCategories(enable) )
        return false;

    if ( !IsFrozen() )
    {
        if ( m_windowStyle & ibPG_AUTO_SORT )
        {
            m_pState->m_itemsAdded = true; // force
            PrepareAfterItemsAdded();
        }
    }
    else
        m_pState->m_itemsAdded = true;

    // No need for RecalculateVirtualSize() here - it is already called in
    // ibPropertyGridPageState method above.

    Refresh();

    return true;
}

// -----------------------------------------------------------------------

void ibPropertyGrid::SwitchState( ibPropertyGridPageState* pNewState )
{
    wxASSERT( pNewState );
    wxASSERT( pNewState->GetGrid() );

    if ( pNewState == m_pState )
        return;

    ibArrayPGProperty oldSelection = m_pState->m_selection;

    // Call ClearSelection() instead of DoClearSelection()
    // so that selection clear events are not sent.
    ClearSelection();

    m_pState->m_selection = oldSelection;

    bool orig_mode = m_pState->IsInNonCatMode();
    bool new_state_mode = pNewState->IsInNonCatMode();

    m_pState = pNewState;

    // Validate width
    int pgWidth = GetClientSize().x;
    if ( HasVirtualWidth() )
    {
        if ( pNewState->GetVirtualWidth() < pgWidth )
        {
            pNewState->m_width = pgWidth;
            pNewState->CheckColumnWidths();
        }
    }
    else
    {
        //
        // Just in case, fully re-center splitter
        //if ( HasFlag( ibPG_SPLITTER_AUTO_CENTER ) )
        //    pNewState->m_fSplitterX = -1.0;

        pNewState->OnClientWidthChange(pgWidth,
                                       pgWidth - pNewState->GetVirtualWidth());
    }

    m_propHover = nullptr;

    // If necessary, convert state to correct mode.
    if ( orig_mode != new_state_mode )
    {
        // This should refresh as well.
        EnableCategories( orig_mode?false:true );
    }
    else if ( !IsFrozen() )
    {
        // Refresh, if not frozen.
        m_pState->PrepareAfterItemsAdded();

        // Reselect (Use SetSelection() instead of Do-variant so that
        // events won't be sent).
        SetSelection(m_pState->m_selection);

        RecalculateVirtualSize(0);
        Refresh();
    }
    else
        m_pState->m_itemsAdded = true;
}

// -----------------------------------------------------------------------

// Call to SetSplitterPosition will always disable splitter auto-centering
// if parent window is shown.
void ibPropertyGrid::DoSetSplitter( int newxpos,
                                    int splitterIndex,
                                    ibPGSplitterPositionFlags flags )
{
    if ( newxpos < ibPG_DRAG_MARGIN )
        return;

    if ( !!(flags & ibPGSplitterPositionFlags::FromEvent) )
        m_pState->m_dontCenterSplitter = true;

    m_pState->DoSetSplitter(newxpos, splitterIndex, flags);

    if ( !!(flags & ibPGSplitterPositionFlags::Refresh) )
    {
        if ( GetSelection() )
        {
            CorrectEditorWidgetSizeX();
        }

        Refresh();
    }

    return;
}

// -----------------------------------------------------------------------

void ibPropertyGrid::ResetColumnSizes( bool enableAutoResizing )
{
    if ( m_pState )
    {
        m_pState->ResetColumnSizes(ibPGSplitterPositionFlags::Null);
        if ( GetSelection() )
        {
            CorrectEditorWidgetSizeX();
        }
        Refresh();

        if ( enableAutoResizing && HasFlag(ibPG_SPLITTER_AUTO_CENTER) )
            m_pState->m_dontCenterSplitter = false;
    }
}

// -----------------------------------------------------------------------

void ibPropertyGrid::CenterSplitter( bool enableAutoResizing )
{
    SetSplitterPosition( m_width/2 );
    if ( enableAutoResizing && HasFlag(ibPG_SPLITTER_AUTO_CENTER) )
        m_pState->m_dontCenterSplitter = false;
}

// -----------------------------------------------------------------------
// ibPropertyGrid item iteration (GetNextProperty etc.) methods
// -----------------------------------------------------------------------

// Returns nearest paint visible property (such that will be painted unless
// window is scrolled or resized). If given property is paint visible, then
// it itself will be returned
ibPGProperty* ibPropertyGrid::GetNearestPaintVisible( ibPGProperty* p ) const
{
    int vy1;// Top left corner of client
    GetViewStart(nullptr,&vy1);
    vy1 *= ibPG_PIXELS_PER_UNIT;

    int vy2 = vy1 + m_height;
    int propY = p->GetY2(m_lineHeight);

    if ( (propY + m_lineHeight) < vy1 )
    {
    // Too high
        return DoGetItemAtY( vy1 );
    }
    else if ( propY > vy2 )
    {
    // Too low
        return DoGetItemAtY( vy2 );
    }

    // Itself paint visible
    return p;

}

// -----------------------------------------------------------------------
// Methods related to change in value, value modification and sending events
// -----------------------------------------------------------------------

// commits any changes in editor of selected property
// return true if validation did not fail
// flags are same as with DoSelectProperty
bool ibPropertyGrid::CommitChangesFromEditor(ibPGSelectPropertyFlags flags)
{
    // Committing already?
    if ( m_inCommitChangesFromEditor )
        return true;

    // Don't do this if already processing editor event. It might
    // induce recursive dialogs and crap like that.
    if ( m_iFlags & ibPG_FL_IN_HANDLECUSTOMEDITOREVENT )
    {
        if ( m_inDoPropertyChanged )
            return true;

        return false;
    }

    ibPGProperty* selected = GetSelection();

    if ( m_wndEditor &&
         IsEditorsValueModified() &&
         (m_iFlags & ibPG_FL_INITIALIZED) &&
         selected )
    {
        m_inCommitChangesFromEditor = true;

        ibVariant variant(selected->GetValueRef());
        bool valueIsPending = false;

        // JACS - necessary to avoid new focus being found spuriously within OnIdle
        // due to another window getting focus
        wxWindow* oldFocus = m_curFocused;

        bool validationFailure = false;
        bool forceSuccess = !!(flags & (ibPGSelectPropertyFlags::NoValidate | ibPGSelectPropertyFlags::Force));

        m_chgInfo_changedProperty = nullptr;

        // If truly modified, schedule value as pending.
        if ( selected->GetEditorClass()->
                GetValueFromControl( variant,
                                     selected,
                                     GetEditorControl() ) )
        {
            if ( DoEditorValidate() &&
                 PerformValidation(selected, variant) )
            {
                valueIsPending = true;
            }
            else
            {
                validationFailure = true;
            }
        }
        else
        {
            EditorsValueWasNotModified();
        }

        m_inCommitChangesFromEditor = false;

        bool res = true;

        if ( validationFailure && !forceSuccess )
        {
            if (oldFocus)
            {
                oldFocus->SetFocus();
                m_curFocused = oldFocus;
            }

            res = OnValidationFailure(selected, variant);

            // Now prevent further validation failure messages
            if ( res )
            {
                EditorsValueWasNotModified();
                OnValidationFailureReset(selected);
            }
        }
        else if ( valueIsPending )
        {
            DoPropertyChanged( selected, flags );
            EditorsValueWasNotModified();
        }

        return res;
    }

    return true;
}

// -----------------------------------------------------------------------

bool ibPropertyGrid::PerformValidation( ibPGProperty* p, ibVariant& pendingValue,
                                        int flags )
{
    //
    // Runs all validation functionality.
    // Returns true if value passes all tests.
    //

    m_validationInfo.SetFailureBehavior(m_permanentValidationFailureBehavior);
    m_validationInfo.SetFailing(true);

    //
    // Variant list a special value that cannot be validated
    // by normal means.
    if ( pendingValue.GetClassName() != ibPGVariantList::s_className )
    {
        if ( !p->ValidateValue(pendingValue, m_validationInfo) )
            return false;
    }

    //
    // Adapt list to child values, if necessary
    ibVariant* pPendingValue = &pendingValue;
    ibVariant* pList = nullptr;

    // If parent has ibPGFlags::Aggregate flag, or uses composite
    // string value, then we need treat as it was changed instead
    // (or, in addition, as is the case with composite string parent).
    // This includes creating list variant for child values.

    ibPGProperty* pwc = p->GetParent();
    ibPGProperty* changedProperty = p;
    ibPGProperty* baseChangedProperty = changedProperty;
    ibVariant bcpPendingList;

    ibVariant listValue = pendingValue;
    // the value's name on its way up: its entry's in its parent's list (wx's value carried it in itself)
    wxString listName = p->GetBaseName();

    while ( pwc &&
            (pwc->HasFlag(ibPGFlags::Aggregate) || pwc->HasFlag(ibPGFlags::ComposedValue)) )
    {
        ibPGVariantList* const lv = new ibPGVariantList();
        lv->Append(listName, listValue);
        listValue = ibVariant(lv);
        listName = pwc->GetBaseName();
        pPendingValue = &listValue;

        if ( pwc->HasFlag(ibPGFlags::Aggregate) )
        {
            baseChangedProperty = pwc;
            bcpPendingList = listValue;
        }

        changedProperty = pwc;
        pwc = pwc->GetParent();
    }

    ibVariant value;
    ibPGProperty* evtChangingProperty = changedProperty;

    if ( pPendingValue->GetClassName() != ibPGVariantList::s_className )
    {
        value = *pPendingValue;
    }
    else
    {
        // Convert list to child values
        pList = pPendingValue;
        changedProperty->AdaptListToValue( *pPendingValue, &value );
    }

    ibVariant evtChangingValue = value;

    if ( flags & SendEvtChanging )
    {
        // FIXME: After proper ValueToString()s added, remove
        // this. It is just a temporary fix, as evt_changing
        // will simply not work for ibPGFlags::ComposedValue
        // (unless it is selected, and textctrl editor is open).
        if ( changedProperty->HasFlag(ibPGFlags::ComposedValue) )
        {
            evtChangingProperty = baseChangedProperty;
            if ( evtChangingProperty != p )
            {
                evtChangingProperty->AdaptListToValue( bcpPendingList, &evtChangingValue );
            }
            else
            {
                evtChangingValue = pendingValue;
            }
        }

        if ( evtChangingProperty->HasFlag(ibPGFlags::ComposedValue) )
        {
            if ( changedProperty == GetSelection() )
            {
                wxWindow* editor = GetEditorControl();
                wxASSERT( wxDynamicCast(editor, ibTextCtrl) );
                evtChangingValue = wxStaticCast(editor, ibTextCtrl)->GetValue();
            }
            else
            {
                wxLogDebug(wxS("WARNING: ibEVT_PG_CHANGING is about to happen with old value."));
            }
        }
    }

    wxASSERT( m_chgInfo_changedProperty == nullptr );
    m_chgInfo_changedProperty = changedProperty;
    m_chgInfo_baseChangedProperty = baseChangedProperty;
    m_chgInfo_pendingValue = value;

    if ( pList )
        m_chgInfo_valueList = *pList;
    else
        m_chgInfo_valueList.Reset();

    // If changedProperty is not property which value was edited,
    // then call ibPGProperty::ValidateValue() for that as well.
    if ( p != changedProperty && value.GetClassName() != ibPGVariantList::s_className )
    {
        if ( !changedProperty->ValidateValue(value, m_validationInfo) )
            return false;
    }

    if ( flags & SendEvtChanging )
    {
        // SendEvent returns true if event was vetoed
        if ( SendEvent( ibEVT_PG_CHANGING, evtChangingProperty,
                        &evtChangingValue ) )
            return false;
    }

    if ( flags & IsStandaloneValidation )
    {
        // If called in 'generic' context, we need to reset
        // m_chgInfo_changedProperty and write back translated value.
        m_chgInfo_changedProperty = nullptr;
        pendingValue = value;
    }

    m_validationInfo.SetFailing(false);

    return true;
}

// -----------------------------------------------------------------------

#if wxUSE_STATUSBAR
wxStatusBar* ibPropertyGrid::GetStatusBar()
{
    wxWindow* topWnd = ::wxGetTopLevelParent(this);
    wxFrame* frame = wxDynamicCast(topWnd, wxFrame);
    if ( frame )
    {
        return frame->GetStatusBar();
    }
    return nullptr;
}
#endif

// -----------------------------------------------------------------------

void ibPropertyGrid::DoShowPropertyError( ibPGProperty* WXUNUSED(property), const wxString& msg )
{
    if ( msg.empty() )
        return;

#if wxUSE_STATUSBAR
    if ( !ibPGGlobalVars->m_offline )
    {
        wxStatusBar* pStatusBar = GetStatusBar();
        if ( pStatusBar )
        {
            pStatusBar->SetStatusText(msg);
            return;
        }
    }
#endif

    /* TRANSLATORS: Caption of message box displaying any property error */
    ibMessageBox(msg, _("Property Error"));
}

// -----------------------------------------------------------------------

void ibPropertyGrid::DoHidePropertyError( ibPGProperty* WXUNUSED(property) )
{
#if wxUSE_STATUSBAR
    if ( !ibPGGlobalVars->m_offline )
    {
        wxStatusBar* pStatusBar = GetStatusBar();
        if ( pStatusBar )
        {
            pStatusBar->SetStatusText(wxString());
            return;
        }
    }
#endif
}

// -----------------------------------------------------------------------

bool ibPropertyGrid::OnValidationFailure( ibPGProperty* property,
                                          ibVariant& invalidValue )
{
    if ( m_inOnValidationFailure )
        return true;

    m_inOnValidationFailure = true;
    wxON_BLOCK_EXIT_SET(m_inOnValidationFailure, false);

    wxWindow* editor = GetEditorControl();
    ibPGVFBFlags vfb = m_validationInfo.GetFailureBehavior();

    if ( m_inDoSelectProperty )
    {
        // When property selection is being changed, do not display any
        // messages, if some were already shown for this property.
        if ( property->HasFlag(ibPGFlags::InvalidValue) )
        {
            m_validationInfo.SetFailureBehavior(
                vfb & (~(ibPGVFBFlags::ShowMessage |
                        ibPGVFBFlags::ShowMessageBox |
                        ibPGVFBFlags::ShowMessageOnStatusBar)));
        }
    }

    // First call property's handler
    property->OnValidationFailure(invalidValue);

    bool res = DoOnValidationFailure(property, invalidValue);

    //
    // For non-ibTextCtrl editors, we do need to revert the value
    if ( !wxDynamicCast(editor, ibTextCtrl) &&
         property == GetSelection() )
    {
        property->GetEditorClass()->UpdateControl(property, editor);
    }

    property->SetFlag(ibPGFlags::InvalidValue);

    return res;
}

bool ibPropertyGrid::DoOnValidationFailure( ibPGProperty* property, ibVariant& WXUNUSED(invalidValue) )
{
    ibPGVFBFlags vfb = m_validationInfo.GetFailureBehavior();

    if ( !!(vfb & ibPGVFBFlags::Beep) )
        ::wxBell();

    if ( !!(vfb & ibPGVFBFlags::MarkCell) &&
         !property->HasFlag(ibPGFlags::InvalidValue) )
    {
        unsigned int colCount = m_pState->GetColumnCount();

        // We need backup marked property's cells
        m_propCellsBackup = property->m_cells;

        wxColour vfbFg = *wxWHITE;
        wxColour vfbBg = *wxRED;

        property->EnsureCells(colCount);

        for ( unsigned int i=0; i<colCount; i++ )
        {
            ibPGCell& cell = property->m_cells[i];
            cell.SetFgCol(vfbFg);
            cell.SetBgCol(vfbBg);
        }

        if ( property == GetSelection() )
        {
            SetInternalFlag(ibPG_FL_CELL_OVERRIDES_SEL);

            wxWindow* editor = GetEditorControl();
            if ( editor )
            {
                editor->SetForegroundColour(vfbFg);
                editor->SetBackgroundColour(vfbBg);
            }
        }

        DrawItemAndChildren(property);
    }

    if ( !!(vfb & (ibPGVFBFlags::ShowMessage |
                ibPGVFBFlags::ShowMessageBox |
                ibPGVFBFlags::ShowMessageOnStatusBar)) )
    {
        wxString msg = m_validationInfo.GetFailureMessage();

        if ( msg.empty() )
            msg = _("You have entered invalid value. Press ESC to cancel editing.");

    #if wxUSE_STATUSBAR
        if ( !!(vfb & ibPGVFBFlags::ShowMessageOnStatusBar) )
        {
            if ( !ibPGGlobalVars->m_offline )
            {
                wxStatusBar* pStatusBar = GetStatusBar();
                if ( pStatusBar )
                    pStatusBar->SetStatusText(msg);
            }
        }
    #endif

        // Displaying error dialog box can cause (native) focus changes
        // so let's preserve the current focus in order to restore it afterwards.
        wxWindow* focusedWnd = wxWindow::FindFocus();

        if ( !!(vfb & ibPGVFBFlags::ShowMessage) )
            DoShowPropertyError(property, msg);

        if ( !!(vfb & ibPGVFBFlags::ShowMessageBox) )
            /* TRANSLATORS: Caption of message box displaying any property error */
            ibMessageBox(msg, _("Property Error"));

        // Restore the focus
        if ( focusedWnd )
        {
            focusedWnd->SetFocus();
        }
    }

    return !(vfb & ibPGVFBFlags::StayInProperty);
}

// -----------------------------------------------------------------------

void ibPropertyGrid::DoOnValidationFailureReset( ibPGProperty* property )
{
    ibPGVFBFlags vfb = m_validationInfo.GetFailureBehavior();

    if ( !!(vfb & ibPGVFBFlags::MarkCell) )
    {
        // Revert cells
        property->m_cells = m_propCellsBackup;

        ClearInternalFlag(ibPG_FL_CELL_OVERRIDES_SEL);

        if ( property == GetSelection() && GetEditorControl() )
        {
            // Calling this will recreate the control, thus resetting its colour
            RefreshProperty(property);
        }
        else
        {
            DrawItemAndChildren(property);
        }
    }

#if wxUSE_STATUSBAR
    if ( !!(vfb & ibPGVFBFlags::ShowMessageOnStatusBar) )
    {
        if ( !ibPGGlobalVars->m_offline )
        {
            wxStatusBar* pStatusBar = GetStatusBar();
            if ( pStatusBar )
                pStatusBar->SetStatusText(wxString());
        }
    }
#endif

    if ( !!(vfb & ibPGVFBFlags::ShowMessage) )
    {
        DoHidePropertyError(property);
    }

    m_validationInfo.SetFailing(false);
}

// -----------------------------------------------------------------------

// flags are same as with DoSelectProperty
bool ibPropertyGrid::DoPropertyChanged( ibPGProperty* p, ibPGSelectPropertyFlags selFlags )
{
    if ( m_inDoPropertyChanged )
        return true;

    m_inDoPropertyChanged = true;
    wxON_BLOCK_EXIT_SET(m_inDoPropertyChanged, false);

    ibPGProperty* selected = GetSelection();

    m_pState->m_anyModified = true;

    // Maybe need to update control
    wxASSERT( m_chgInfo_changedProperty != nullptr );

    // These values were calculated in PerformValidation()
    ibPGProperty* changedProperty = m_chgInfo_changedProperty;
    ibVariant value = m_chgInfo_pendingValue;

    // If property's value is being changed, assume it is valid
    OnValidationFailureReset(selected);

    changedProperty->SetValue(value, &m_chgInfo_valueList, ibPGSetValueFlags::ByUser);

    // NB: Call GetEditorControl() as late as possible, because OnSetValue()
    //     and perhaps other user-defined virtual functions may change it.
    wxWindow* editor = GetEditorControl();

    // Set as Modified (not if dragging just began)
    if ( !p->HasFlag(ibPGFlags::Modified) )
    {
        p->SetFlag(ibPGFlags::Modified);
        if ( p == selected && (m_windowStyle & ibPG_BOLD_MODIFIED) )
        {
            if ( editor )
                SetCurControlBoldFont();
        }
    }

    // Propagate updates to parent(s)
    ibPGProperty* topPaintedProperty = changedProperty->GetMainParent();
    ibPGProperty* pwc = p;
    ibPGProperty* prevPwc = nullptr;

    while ( prevPwc != topPaintedProperty )
    {
        pwc->SetFlag(ibPGFlags::Modified);

        if ( pwc == selected && (m_windowStyle & ibPG_BOLD_MODIFIED) )
        {
            if ( editor )
                SetCurControlBoldFont();
        }

        prevPwc = pwc;
        pwc = pwc->GetParent();
    }

    // Draw the actual property
    DrawItemAndChildren( topPaintedProperty );

    //
    // If value was set by ibPGProperty::OnEvent, then update the editor
    // control.
    if ( !!(selFlags & ibPGSelectPropertyFlags::DialogVal) )
    {
        RefreshEditor();
    }
    else
    {
#if ibPG_REFRESH_CONTROLS
        if ( m_wndEditor ) m_wndEditor->Refresh();
        if ( m_wndEditor2 ) m_wndEditor2->Refresh();
#endif
    }

    // Sanity check
    wxASSERT( !changedProperty->GetParent()->HasFlag(ibPGFlags::Aggregate) );

    // If top parent has composite string value, then send to child parents,
    // starting from baseChangedProperty.
    if ( changedProperty->HasFlag(ibPGFlags::ComposedValue) )
    {
        pwc = m_chgInfo_baseChangedProperty;

        while ( pwc != changedProperty )
        {
            SendEvent( ibEVT_PG_CHANGED, pwc, nullptr );
            pwc = pwc->GetParent();
        }
    }

    SendEvent( ibEVT_PG_CHANGED, changedProperty, nullptr );

    return true;
}

// -----------------------------------------------------------------------

bool ibPropertyGrid::ChangePropertyValue( ibPGPropArg id, ibVariant newValue )
{
    ibPG_PROP_ARG_CALL_PROLOG_RETVAL(false)

    m_chgInfo_changedProperty = nullptr;

    if ( PerformValidation(p, newValue) )
    {
        DoPropertyChanged(p);
        return true;
    }
    else
    {
        OnValidationFailure(p, newValue);
    }

    return false;
}

// -----------------------------------------------------------------------

ibVariant ibPropertyGrid::GetUncommittedPropertyValue()
{
    ibPGProperty* prop = GetSelectedProperty();

    if ( !prop )
        return ibVariant();

    ibTextCtrl* tc = GetEditorTextCtrl();
    ibVariant value = prop->GetValue();

    if ( !tc || !IsEditorsValueModified() )
        return value;

    if ( !prop->StringToValue(value, tc->GetValue()) )
        return value;

    if ( !PerformValidation(prop, value, IsStandaloneValidation) )
        return prop->GetValue();

    return value;
}

// -----------------------------------------------------------------------

// Runs wxValidator for the selected property
bool ibPropertyGrid::DoEditorValidate()
{
#if wxUSE_VALIDATORS
    wxRecursionGuard guard(m_validatingEditor);
    if ( guard.IsInside() )
        return false;

    m_validationInfo.SetFailureBehavior(m_permanentValidationFailureBehavior);
    m_validationInfo.SetFailing(true);

    ibPGProperty* selected = GetSelection();
    if ( selected )
    {
        wxWindow* wnd = GetEditorControl();

        wxValidator* validator = selected->GetValidator();
        if ( validator && wnd )
        {
            validator->SetWindow(wnd);
            if ( !validator->Validate(this) )
                return false;
        }
    }

    m_validationInfo.SetFailing(false);
#endif
    return true;
}

// -----------------------------------------------------------------------

bool ibPropertyGrid::HandleCustomEditorEvent( wxEvent &event )
{
    //
    // NB: We should return true if the event was recognized as
    //     a dedicated ibPropertyGrid event, and as such was
    //     either properly handled or ignored.
    //

    // It is possible that this handler receives event even before
    // the control has been properly initialized. Let's skip the
    // event handling in that case.
    if ( !m_pState )
        return false;

    // Don't care about the event if it originated from the
    // 'label editor'. In this function we only care about the
    // property value editor.
    if ( m_labelEditor && event.GetId() == m_labelEditor->GetId() )
    {
        event.Skip();
        return true;
    }

    ibPGProperty* selected = GetSelection();

    // Somehow, event is handled after property has been deselected.
    // Possibly, but very rare.
    if ( !selected ||
          selected->HasFlag(ibPGFlags::BeingDeleted) ||
          m_inOnValidationFailure ||
          // Also don't handle editor event if ibEVT_PG_CHANGED or
          // similar is currently doing something (showing a
          // message box, for instance).
          m_processedEvent )
        return true;

    if ( m_iFlags & ibPG_FL_IN_HANDLECUSTOMEDITOREVENT )
        return true;

    ibVariant pendingValue(selected->GetValueRef());
    wxWindow* wnd = GetEditorControl();
    wxWindow* editorWnd = wxDynamicCast(event.GetEventObject(), wxWindow);
    ibPGSelectPropertyFlags selFlags = ibPGSelectPropertyFlags::Null;
    bool wasUnspecified = selected->IsValueUnspecified();
    int usesAutoUnspecified = selected->UsesAutoUnspecified();
    bool valueIsPending = false;

    m_chgInfo_changedProperty = nullptr;

    m_iFlags &= ~ibPG_FL_VALUE_CHANGE_IN_EVENT;

    //
    // Ignore focus changes within the composite editor control
    if ( event.GetEventType() == wxEVT_SET_FOCUS || event.GetEventType() == wxEVT_KILL_FOCUS )
    {
        wxFocusEvent* fevt = wxDynamicCast(&event, wxFocusEvent);
        wxWindow* win = fevt->GetWindow();
        while ( win )
        {
            if ( win == wnd )
            {
                event.Skip();
                return true;
            }

            win = win->GetParent();
        }
    }
    // Filter out excess ibTextCtrl modified events
    else if ( event.GetEventType() == wxEVT_TEXT && wnd )
    {
        if ( wxDynamicCast(wnd, ibTextCtrl) )
        {
            ibTextCtrl* tc = (ibTextCtrl*) wnd;

            wxString newTcValue = tc->GetValue();
            if ( m_prevTcValue == newTcValue )
                return true;
            m_prevTcValue = newTcValue;
        }
        else if ( wxDynamicCast(wnd, ibComboCtrl) )
        {
            // In some cases we might stumble unintentionally on
            // ibComboCtrl's embedded ibTextCtrl's events. Let's
            // avoid them.
            if ( wxDynamicCast(editorWnd, ibTextCtrl) )
                return false;

            ibComboCtrl* cc = (ibComboCtrl*) wnd;

            wxString newTcValue = cc->GetTextCtrl()->GetValue();
            if ( m_prevTcValue == newTcValue )
                return true;
            m_prevTcValue = newTcValue;
        }
    }

    SetInternalFlag(ibPG_FL_IN_HANDLECUSTOMEDITOREVENT);

    bool validationFailure = false;
    bool buttonWasHandled = false;
    bool result = false;

    //
    // Try common button handling
    if ( IsMainButtonEvent(event) )
    {
        ibPGEditorDialogAdapter* adapter = selected->GetEditorDialog();

        if ( adapter )
        {
            buttonWasHandled = true;
            // Store as res2, as previously (and still currently alternatively)
            // dialogs can be shown by handling wxEVT_BUTTON
            // in ibPGProperty::OnEvent().
            adapter->ShowDialog( this, selected );
            delete adapter;
        }
    }

    if ( !buttonWasHandled )
    {
        if ( wnd || m_wndEditor2 )
        {
            // First call editor class' event handler.
            const ibPGEditor* editor = selected->GetEditorClass();

            if ( editor->OnEvent( this, selected, editorWnd, event ) )
            {
                result = true;

                // If changes, validate them
                if ( DoEditorValidate() )
                {
                    if ( editor->GetValueFromControl( pendingValue,
                                                      selected,
                                                      wnd ) )
                        valueIsPending = true;

                    // Mark value always as pending if validation is currently
                    // failing and value was not unspecified
                    if ( !valueIsPending &&
                         pendingValue.GetType() != ibValueTypes::TYPE_EMPTY &&
                         m_validationInfo.IsFailing() )
                         valueIsPending = true;
                }
                else
                {
                    validationFailure = true;
                }
            }
        }

        // Then the property's custom handler (must be always called, unless
        // validation failed).
        if ( !validationFailure )
            buttonWasHandled = selected->OnEvent( this, editorWnd, event );
    }

    // SetValueInEvent(), as called in one of the functions referred above
    // overrides editor's value.
    if ( WasValueChangedInEvent() )
    {
        valueIsPending = true;
        pendingValue = m_changeInEventValue;
        selFlags |= ibPGSelectPropertyFlags::DialogVal;
    }

    if ( !validationFailure && valueIsPending )
        if ( !PerformValidation(selected, pendingValue) )
            validationFailure = true;

    if ( validationFailure)
    {
        OnValidationFailure(selected, pendingValue);
    }
    else if ( valueIsPending )
    {
        selFlags |= ( !wasUnspecified && selected->IsValueUnspecified() && usesAutoUnspecified )
                                                 ? ibPGSelectPropertyFlags::SetUnspec : ibPGSelectPropertyFlags::Null;

        DoPropertyChanged(selected, selFlags);
        EditorsValueWasNotModified();

        // Regardless of editor type, unfocus editor on
        // text-editing related enter press.
        if ( event.GetEventType() == wxEVT_TEXT_ENTER )
        {
            SetFocusOnCanvas();
        }
    }
    else
    {
        // No value after all

        // Regardless of editor type, unfocus editor on
        // text-editing related enter press.
        if ( event.GetEventType() == wxEVT_TEXT_ENTER )
        {
            SetFocusOnCanvas();
        }

        // Let unhandled button click events go to the parent
        if ( !buttonWasHandled && event.GetEventType() == wxEVT_BUTTON )
        {
            result = true;
            wxCommandEvent evt(wxEVT_BUTTON,GetId());
            GetEventHandler()->AddPendingEvent(evt);
        }
    }

    ClearInternalFlag(ibPG_FL_IN_HANDLECUSTOMEDITOREVENT);

    return result;
}

// -----------------------------------------------------------------------
// ibPropertyGrid editor control helper methods
// -----------------------------------------------------------------------

wxRect ibPropertyGrid::GetEditorWidgetRect( ibPGProperty* p, int column ) const
{
    int itemy = p->GetY2(m_lineHeight);
    int splitterX = m_pState->DoGetSplitterPosition(column-1);
    int colEnd = splitterX + m_pState->GetColumnWidth(column);
    int imageOffset = 0;

    int vx, vy;  // Top left corner of client
    GetViewStart(&vx, &vy);
    vx *= ibPG_PIXELS_PER_UNIT;
    vy *= ibPG_PIXELS_PER_UNIT;

    if ( column == 1 )
    {
        // TODO: If custom image detection changes from current, change this.
        if ( m_iFlags & ibPG_FL_CUR_USES_CUSTOM_IMAGE )
        {
            //m_iFlags |= ibPG_FL_CUR_USES_CUSTOM_IMAGE;
            int iw = p->OnMeasureImage().x;
            wxASSERT( iw == wxDefaultCoord || iw >= 0 );
            if ( iw == wxDefaultCoord || iw == 0 )
                iw = ibPG_CUSTOM_IMAGE_WIDTH;
            imageOffset = p->GetImageOffset(iw);
        }
    }
    else if ( column == 0 )
    {
        splitterX += (p->GetDepth() - 1) * m_subgroup_extramargin;
    }

    return wxRect
      (
        splitterX+imageOffset+ibPG_XBEFOREWIDGET+ibPG_CONTROL_MARGIN+1-vx,
        itemy-vy,
        colEnd-splitterX-ibPG_XBEFOREWIDGET-ibPG_CONTROL_MARGIN-imageOffset-1,
        m_lineHeight-1
      );
}

// -----------------------------------------------------------------------

wxRect ibPropertyGrid::GetImageRect( ibPGProperty* p, int item ) const
{
    wxSize sz = GetImageSize(p, item);
    return wxRect(ibPG_CONTROL_MARGIN + ibCC_CUSTOM_IMAGE_MARGIN1,
                  ibPG_CUSTOM_IMAGE_SPACINGY,
                  sz.x,
                  sz.y);
}

// return size of custom paint image
wxSize ibPropertyGrid::GetImageSize( ibPGProperty* p, int item ) const
{
    // If called with nullptr property, then return default image
    // size for properties that use image.
    if ( !p )
        return wxSize(ibPG_CUSTOM_IMAGE_WIDTH,ibPG_STD_CUST_IMAGE_HEIGHT(m_lineHeight));

    wxSize cis = p->OnMeasureImage(item);

    int choiceCount = p->GetChoices().GetCount();
    int comVals = p->GetDisplayedCommonValueCount();
    if ( item >= choiceCount && comVals > 0 )
    {
        unsigned int cvi = item-choiceCount;
        cis = GetCommonValue(cvi)->GetRenderer()->GetImageSize(nullptr, 1, cvi);
    }
    else if ( item >= 0 && choiceCount == 0 )
        return wxSize(0, 0);

    wxASSERT( cis.x == wxDefaultCoord || cis.x >= 0 );
    if ( cis.x == wxDefaultCoord )
    {
        cis.x = ibPG_CUSTOM_IMAGE_WIDTH;
    }
    wxASSERT( cis.y == wxDefaultCoord || cis.y >= 0 );
    if ( cis.y == wxDefaultCoord || cis.y == 0 )
    {
        cis.y = ibPG_STD_CUST_IMAGE_HEIGHT(m_lineHeight);
    }
    return cis;
}

// -----------------------------------------------------------------------

// takes scrolling into account
void ibPropertyGrid::ImprovedClientToScreen( int* px, int* py ) const
{
    wxASSERT(px && py);
    CalcScrolledPosition(*px, *py, px, py);
    // the client area's point, the window's outside the border
    const wxPoint origin = GetClientAreaOrigin();
    *px += origin.x;
    *py += origin.y;
    ClientToScreen( px, py );

}

// -----------------------------------------------------------------------

ibPropertyGridHitTestResult ibPropertyGrid::HitTest( const wxPoint& pt ) const
{
    return m_pState->HitTest(CalcUnscrolledPosition(pt));
}

// -----------------------------------------------------------------------

// custom set cursor
void ibPropertyGrid::CustomSetCursor( int type, bool override )
{
    if ( type == m_curcursor && !override ) return;

    wxCursor cursor = (type == wxCURSOR_SIZEWE) ? m_cursorSizeWE : ibPG_DEFAULT_CURSOR;
    SetCursor(cursor);

    m_curcursor = type;
}

// -----------------------------------------------------------------------

wxString
ibPropertyGrid::GetUnspecifiedValueText(ibPGPropValFormatFlags flags) const
{
    const ibPGCell& ua = GetUnspecifiedValueAppearance();

    if ( ua.HasText() &&
         !(flags & ibPGPropValFormatFlags::FullValue) &&
         !(flags & ibPGPropValFormatFlags::EditableValue) )
        return ua.GetText();

    return wxString();
}

// -----------------------------------------------------------------------
// ibPropertyGrid property selection, editor creation
// -----------------------------------------------------------------------

//
// This class forwards events from property editor controls to ibPropertyGrid.
class ibPropertyGridEditorEventForwarder : public wxEvtHandler
{
public:
    ibPropertyGridEditorEventForwarder( ibPropertyGrid* propGrid )
        : wxEvtHandler(), m_propGrid(propGrid)
    {
    }

    virtual ~ibPropertyGridEditorEventForwarder() = default;

private:
    bool ProcessEvent( wxEvent& event ) override
    {
        // Always skip
        event.Skip();

        m_propGrid->HandleCustomEditorEvent(event);

        //
        // NB: We should return true if the event was recognized as
        //     a dedicated ibPropertyGrid event, and as such was
        //     either properly handled or ignored.
        //
        if ( m_propGrid->IsMainButtonEvent(event) )
            return true;

        //
        // NB: On wxMSW, a ibTextCtrl with wxTE_PROCESS_ENTER
        //     may beep annoyingly if that event is skipped
        //     and passed to parent event handler.
        if ( event.GetEventType() == wxEVT_TEXT_ENTER )
            return true;

        return wxEvtHandler::ProcessEvent(event);
    }

    ibPropertyGrid*         m_propGrid;
};

// Setups event handling for child control
void ibPropertyGrid::SetupChildEventHandling( wxWindow* argWnd )
{
    wxWindowID id = argWnd->GetId();

    if ( argWnd == m_wndEditor )
    {
        argWnd->Bind(wxEVT_MOTION, &ibPropertyGrid::OnMouseMoveChild, this, id);
        argWnd->Bind(wxEVT_LEFT_UP, &ibPropertyGrid::OnMouseUpChild, this, id);
        argWnd->Bind(wxEVT_LEFT_DOWN, &ibPropertyGrid::OnMouseClickChild, this, id);
        argWnd->Bind(wxEVT_RIGHT_UP, &ibPropertyGrid::OnMouseRightClickChild, this, id);
        argWnd->Bind(wxEVT_ENTER_WINDOW, &ibPropertyGrid::OnMouseEntry, this, id);
        argWnd->Bind(wxEVT_LEAVE_WINDOW, &ibPropertyGrid::OnMouseEntry, this, id);
    }

    ibPropertyGridEditorEventForwarder* forwarder;
    forwarder = new ibPropertyGridEditorEventForwarder(this);
    argWnd->PushEventHandler(forwarder);

    argWnd->Bind(wxEVT_KEY_DOWN, &ibPropertyGrid::OnChildKeyDown, this, id);
}

void ibPropertyGrid::DeletePendingObjects()
{
    // Delete pending property editors and their event handlers.
    while ( !m_deletedEditorObjects.empty() )
    {
        wxObject* obj = m_deletedEditorObjects.back();
        m_deletedEditorObjects.pop_back();

        delete obj;
    }
}

void ibPropertyGrid::DestroyEditorWnd( wxWindow* wnd )
{
    if ( !wnd )
        return;

    wnd->Hide();

    // Do not free editors immediately (for sake of processing events)
    m_deletedEditorObjects.push_back(wnd);
}

void ibPropertyGrid::FreeEditors()
{
    //
    // Return focus back to canvas from children (this is required at least for
    // GTK+, which, unlike Windows, clears focus when control is destroyed
    // instead of moving it to closest parent).
    SetFocusOnCanvas();

    // Do not free editors immediately if processing events
    if ( m_wndEditor2 )
    {
        wxEvtHandler* handler = m_wndEditor2->PopEventHandler(false);
        m_wndEditor2->Hide();
        m_deletedEditorObjects.push_back(handler);
        DestroyEditorWnd(m_wndEditor2);
        m_wndEditor2 = nullptr;
    }

    if ( m_wndEditor )
    {
        wxEvtHandler* handler = m_wndEditor->PopEventHandler(false);
        m_wndEditor->Hide();
        m_deletedEditorObjects.push_back(handler);
        DestroyEditorWnd(m_wndEditor);
        m_wndEditor = nullptr;
    }
}

// Call with nullptr to de-select property
bool ibPropertyGrid::DoSelectProperty( ibPGProperty* p, ibPGSelectPropertyFlags flags )
{
    /*
    if (p)
    {
        wxLogDebug(wxS("SelectProperty( %s (%s[%i]) )"),p->GetLabel(),
            p->m_parent->GetLabel(),p->GetIndexInParent());
    }
    else
    {
        wxLogDebug(wxS("SelectProperty( nullptr, -1 )"));
    }
    */

    if ( m_inDoSelectProperty )
        return true;

    m_inDoSelectProperty = true;
    wxON_BLOCK_EXIT_SET(m_inDoSelectProperty, false);

    if ( !m_pState )
        return false;

    ibArrayPGProperty prevSelection = m_pState->m_selection;
    ibPGProperty* prevFirstSel;

    prevFirstSel = prevSelection.empty()? nullptr: prevSelection[0];

    if ( prevFirstSel && prevFirstSel->HasFlag(ibPGFlags::BeingDeleted) )
        prevFirstSel = nullptr;

    // Always send event, as this is indirect call
    DoEndLabelEdit(true, ibPGSelectPropertyFlags::NoValidate);

/*
    if ( prevFirstSel )
        wxPrintf( "Selected %s\n", prevFirstSel->GetClassInfo()->GetClassName() );
    else
        wxPrintf( "None selected\n" );

    if (p)
        wxPrintf( "P =  %s\n", p->GetClassInfo()->GetClassName() );
    else
        wxPrintf( "P = nullptr\n" );
*/

    wxWindow* primaryCtrl = nullptr;

    // If we are frozen, then just set the values.
    if ( IsFrozen() )
    {
        m_iFlags &= ~(ibPG_FL_ABNORMAL_EDITOR);
        m_editorFocused = false;
        m_pState->DoSetSelection(p);

        // If frozen, always free controls. But don't worry, as Thaw will
        // recall SelectProperty to recreate them.
        FreeEditors();

        // Prevent any further selection measures in this call
        p = nullptr;
    }
    else
    {
        // Is it the same?
        if ( prevFirstSel == p &&
             prevSelection.size() <= 1 &&
             !(flags & ibPGSelectPropertyFlags::Force) )
        {
            // Only set focus if not deselecting
            if ( p )
            {
                if ( !!(flags & ibPGSelectPropertyFlags::Focus) )
                {
                    if ( m_wndEditor )
                    {
                        m_wndEditor->SetFocus();
                        m_editorFocused = true;
                    }
                }
                else
                {
                    SetFocusOnCanvas();
                }
            }

            return true;
        }

        //
        // First, deactivate previous
        if ( prevFirstSel )
        {
            // Must double-check if this is an selected in case of force switch
            if ( p != prevFirstSel )
            {
                if ( !CommitChangesFromEditor(flags) )
                {
                    // Validation has failed, so we can't exit the previous editor
                    //::wxMessageBox(_("Please correct the value or press ESC to cancel the edit."),
                    //               _("Invalid Value"),wxOK|wxICON_ERROR);
                    return false;
                }
            }

            // This should be called after CommitChangesFromEditor(), so that
            // OnValidationFailure() still has information on property's
            // validation state.
            OnValidationFailureReset(prevFirstSel);

            FreeEditors();

            m_iFlags &= ~(ibPG_FL_ABNORMAL_EDITOR);
            EditorsValueWasNotModified();
        }

        SetInternalFlag(ibPG_FL_IN_SELECT_PROPERTY);

        m_pState->DoSetSelection(p);

        // Redraw unselected
        for( ibPGProperty* selProp : prevSelection )
        {
            DrawItem(selProp);
        }

        //
        // Then, activate the one given.
        if ( p )
        {
            int propY = p->GetY2(m_lineHeight);

            int splitterX;
            CalcScrolledPosition(GetSplitterPosition(), 0, &splitterX, nullptr);

            m_editorFocused = false;
            m_iFlags |= ibPG_FL_PRIMARY_FILLS_ENTIRE;

            wxASSERT( m_wndEditor == nullptr );

            //
            // Only create editor for non-disabled non-caption
            if ( !p->IsCategory() && !p->HasFlag(ibPGFlags::Disabled) )
            {
            // do this for non-caption items

                m_selColumn = 1;

                // Do we need to paint the custom image, if any?
                m_iFlags &= ~(ibPG_FL_CUR_USES_CUSTOM_IMAGE);
                if ( p->HasFlag(ibPGFlags::CustomImage) &&
                     !p->GetEditorClass()->CanContainCustomImage()
                   )
                    m_iFlags |= ibPG_FL_CUR_USES_CUSTOM_IMAGE;

                wxRect grect = GetEditorWidgetRect(p, m_selColumn);
                wxPoint goodPos = grect.GetPosition();

                // Editor appearance can now be considered clear
                m_editorAppearance.SetEmptyData();

                const ibPGEditor* editor = p->GetEditorClass();
                wxCHECK_MSG(editor, false,
                    wxS("null editor class not allowed"));

                m_iFlags &= ~ibPG_FL_FIXED_WIDTH_EDITOR;

                ibPGWindowList wndList =
                    editor->CreateControls(this,
                                           p,
                                           goodPos,
                                           grect.GetSize());

                m_wndEditor = wndList.GetPrimary();
                m_wndEditor2 = wndList.GetSecondary();
                // Remember actual positions within required cell.
                // These values can be used when there will be required
                // to reposition the cell.
                if ( m_wndEditor )
                {
                    m_wndEditorPosRel = m_wndEditor->GetPosition() - goodPos;
                }
                if ( m_wndEditor2 )
                {
                    m_wndEditor2PosRel = m_wndEditor2->GetPosition() - goodPos;
                }
                primaryCtrl = GetEditorControl();

                //
                // Essentially, primaryCtrl == m_wndEditor
                //

                // NOTE: It is allowed for m_wndEditor to be null - in this
                //       case value is drawn as normal, and m_wndEditor2 is
                //       assumed to be a right-aligned button that triggers
                //       a separate editorCtrl window.

                if ( m_wndEditor )
                {
                    wxASSERT_MSG( m_wndEditor->GetParent() == GetPanel(),
                                  wxS("CreateControls must use result of ")
                                  wxS("ibPropertyGrid::GetPanel() as parent ")
                                  wxS("of controls.") );

                    // Set validator, if any
                #if wxUSE_VALIDATORS
                    wxValidator* validator = p->GetValidator();
                    if ( validator )
                        primaryCtrl->SetValidator(*validator);
                #endif

                    if ( m_wndEditor->GetSize().y > (m_lineHeight+6) )
                        m_iFlags |= ibPG_FL_ABNORMAL_EDITOR;

                    // If it has modified status, use bold font
                    // (must be done before capturing m_ctrlXAdjust)
                    if ( p->HasFlag(ibPGFlags::Modified) &&
                         (m_windowStyle & ibPG_BOLD_MODIFIED) )
                        SetCurControlBoldFont();
                    // Store x relative to splitter (we'll need it).
                    m_ctrlXAdjust = m_wndEditor->GetPosition().x - splitterX;

                    // Check if background clear is not necessary
                    wxPoint pos = m_wndEditor->GetPosition();
                    if ( pos.x > (splitterX+1) || pos.y > propY )
                    {
                        m_iFlags &= ~(ibPG_FL_PRIMARY_FILLS_ENTIRE);
                    }

                    m_wndEditor->SetSizeHints(3, 3);

                    SetupChildEventHandling(primaryCtrl);

                    // Focus and select all (ibTextCtrl, wxComboBox etc.)
                    if ( !!(flags & ibPGSelectPropertyFlags::Focus) )
                    {
                        primaryCtrl->SetFocus();

                        p->GetEditorClass()->OnFocus(p, primaryCtrl);
                    }
                    else
                    {
                        if ( p->IsValueUnspecified() )
                            SetEditorAppearance(m_unspecifiedAppearance,
                                                true);
                    }
                }

                if ( m_wndEditor2 )
                {
                    wxASSERT_MSG( m_wndEditor2->GetParent() == GetPanel(),
                                  wxS("CreateControls must use result of ")
                                  wxS("ibPropertyGrid::GetPanel() as parent ")
                                  wxS("of controls.") );

                    // Get proper id for wndSecondary
                    m_wndSecId = m_wndEditor2->GetId();
                    wxWindowList children = m_wndEditor2->GetChildren();
                    wxWindowList::iterator node = children.begin();
                    if ( node != children.end() )
                        m_wndSecId = ((wxWindow*)*node)->GetId();

                    m_wndEditor2->SetSizeHints(3,3);

                    m_wndEditor2->Show();

                    SetupChildEventHandling(m_wndEditor2);

                    // If no primary editor, focus to button to allow
                    // it to interpret ENTER etc.
                    // NOTE: Due to problems focusing away from it, this
                    //       has been disabled.
                    /*
                    if ( (flags & ibPGSelectPropertyFlags::Focus) && !m_wndEditor )
                        m_wndEditor2->SetFocus();
                    */
                }

                if ( !!(flags & ibPGSelectPropertyFlags::Focus) )
                    m_editorFocused = true;

            }
            else
            {
                // Make sure focus is in grid canvas (important for wxGTK,
                // at least)
                SetFocusOnCanvas();
            }

            EditorsValueWasNotModified();

            // If it's inside collapsed section, expand parent, scroll, etc.
            // Also, if it was partially visible, scroll it into view.
            if ( !(flags & ibPGSelectPropertyFlags::Nonvisible) )
                EnsureVisible( p );

            if ( m_wndEditor )
            {
                m_wndEditor->Show(true);
            }

            if ( !(flags & ibPGSelectPropertyFlags::NoRefresh) )
                DrawItem(p);
        }
        else
        {
            // Make sure focus is in grid canvas
            SetFocusOnCanvas();
        }

        ClearInternalFlag(ibPG_FL_IN_SELECT_PROPERTY);
    }

    const wxString* pHelpString = nullptr;

    if ( p )
        pHelpString = &p->GetHelpString();

    if ( !HasExtraStyle(ibPG_EX_HELP_AS_TOOLTIPS) )
    {
#if wxUSE_STATUSBAR

        //
        // Show help text in status bar.
        //   (if found and grid not embedded in manager with help box and
        //    style ibPG_EX_HELP_AS_TOOLTIPS is not used).
        //
        wxStatusBar* statusbar = GetStatusBar();
        if ( statusbar )
        {
            if ( pHelpString && !pHelpString->empty() )
            {
                // Set help box text.
                statusbar->SetStatusText( *pHelpString );
                m_iFlags |= ibPG_FL_STRING_IN_STATUSBAR;
            }
            else if ( m_iFlags & ibPG_FL_STRING_IN_STATUSBAR )
            {
                // Clear help box - but only if it was written
                // by us at previous time.
                statusbar->SetStatusText(wxString());
                m_iFlags &= ~(ibPG_FL_STRING_IN_STATUSBAR);
            }
        }
#endif
    }
    else
    {
#if wxUSE_TOOLTIPS
        //
        // Show help as a tool tip on the editor control.
        //
        if ( pHelpString && !pHelpString->empty() &&
             primaryCtrl )
        {
            primaryCtrl->SetToolTip(*pHelpString);
        }
#endif
    }

    // call wx event handler (here so that it also occurs on deselection)
    // In case of deselection previously selected property
    // is passed to the event object.
    if (!p)
    {
        p = prevFirstSel;
    }
    if ( !(flags & ibPGSelectPropertyFlags::DontSendEvent) && p)
        SendEvent( ibEVT_PG_SELECTED, p, nullptr );

    return true;
}

// -----------------------------------------------------------------------

bool ibPropertyGrid::UnfocusEditor()
{
    ibPGProperty* selected = GetSelection();

    if ( !selected || !m_wndEditor || IsFrozen() )
        return true;

    if ( !CommitChangesFromEditor() )
        return false;

    SetFocusOnCanvas();
    DrawItem(selected);

    return true;
}

// -----------------------------------------------------------------------

void ibPropertyGrid::RefreshEditor()
{
    ibPGProperty* p = GetSelection();
    if ( !p )
        return;

    wxWindow* wnd = GetEditorControl();
    if ( !wnd )
        return;

    // Set editor font boldness - must do this before
    // calling UpdateControl().
    if ( HasFlag(ibPG_BOLD_MODIFIED) )
    {
        if ( p->HasFlag(ibPGFlags::Modified) )
            wnd->SetFont(GetCaptionFont());
        else
            wnd->SetFont(GetFont());
    }

    const ibPGEditor* editorClass = p->GetEditorClass();

    editorClass->UpdateControl(p, wnd);

    if ( p->IsValueUnspecified() )
        SetEditorAppearance(m_unspecifiedAppearance, true);
}

// -----------------------------------------------------------------------

bool ibPropertyGrid::SelectProperty( ibPGPropArg id, bool focus )
{
    ibPG_PROP_ARG_CALL_PROLOG_RETVAL(false)

    ibPGSelectPropertyFlags flags = ibPGSelectPropertyFlags::DontSendEvent;
    if ( focus )
        flags |= ibPGSelectPropertyFlags::Focus;

    return DoSelectProperty(p, flags);
}

// -----------------------------------------------------------------------
// ibPropertyGrid expand/collapse state
// -----------------------------------------------------------------------

bool ibPropertyGrid::DoCollapse( ibPGProperty* p, bool sendEvents )
{
    ibPGProperty* selected = GetSelection();

    // If active editor was inside collapsed section, then disable it
    if ( selected && selected->IsSomeParent(p) )
    {
        DoClearSelection();
    }

    // Store dont-center-splitter flag because we need to temporarily set it
    bool prevDontCenterSplitter = m_pState->m_dontCenterSplitter;
    m_pState->m_dontCenterSplitter = true;

    bool res = m_pState->DoCollapse(p);

    if ( res )
    {
        if ( sendEvents )
            SendEvent( ibEVT_PG_ITEM_COLLAPSED, p );

        RecalculateVirtualSize();
        Refresh();
    }

    m_pState->m_dontCenterSplitter = prevDontCenterSplitter;

    return res;
}

// -----------------------------------------------------------------------

bool ibPropertyGrid::DoExpand( ibPGProperty* p, bool sendEvents )
{
    wxCHECK_MSG( p, false, wxS("invalid property id") );

    // Store dont-center-splitter flag 'cause we need to temporarily set it
    bool prevDontCenterSplitter = m_pState->m_dontCenterSplitter;
    m_pState->m_dontCenterSplitter = true;

    bool res = m_pState->DoExpand(p);

    if ( res )
    {
        if ( sendEvents )
            SendEvent( ibEVT_PG_ITEM_EXPANDED, p );

        RecalculateVirtualSize();
        Refresh();
    }

    m_pState->m_dontCenterSplitter = prevDontCenterSplitter;

    return res;
}

// -----------------------------------------------------------------------

bool ibPropertyGrid::DoHideProperty( ibPGProperty* p, bool hide, ibPGPropertyValuesFlags flags )
{
    if ( IsFrozen() )
        return m_pState->DoHideProperty(p, hide, flags);

    ibArrayPGProperty selection = m_pState->m_selection;  // Must use a copy
    for( ibPGProperty* selected: selection )
    {
        if ( selected == p || selected->IsSomeParent(p) )
        {
            if ( !DoRemoveFromSelection(p) )
                return false;
        }
    }

    m_pState->DoHideProperty(p, hide, flags);

    RecalculateVirtualSize();
    Refresh();

    return true;
}


// -----------------------------------------------------------------------
// ibPropertyGrid size related methods
// -----------------------------------------------------------------------

void ibPropertyGrid::RecalculateVirtualSize( int forceXPos )
{
    // Don't check for !HasInternalFlag(ibPG_FL_INITIALIZED) here. Otherwise
    // virtual size calculation may go wrong.
    if ( HasInternalFlag(ibPG_FL_RECALCULATING_VIRTUAL_SIZE) ||
         IsFrozen() ||
         !m_pState )
        return;

    int h0 = m_pState->m_virtualHeight;
    m_pState->EnsureVirtualHeight();

    wxASSERT_LEVEL_2_MSG(
        m_pState->GetVirtualHeight() == m_pState->GetActualVirtualHeight(),
        wxS("VirtualHeight and ActualVirtualHeight should match")
    );

    m_iFlags |= ibPG_FL_RECALCULATING_VIRTUAL_SIZE;

    int w = m_pState->GetVirtualWidth();
    int h = m_pState->m_virtualHeight;
    // Now adjust virtual size.
    SetVirtualSize(w, h);
    // If virtual height was changed, then recalculate editor control position(s)
    if ( h != h0 )
    {
        CorrectEditorWidgetPosY();
    }
    if ( forceXPos != -1 )
    {
        Scroll(forceXPos, wxDefaultCoord);
    }

    // Must re-get size now
    GetClientSize(&m_width, &m_height);

    if ( !HasVirtualWidth() )
    {
        m_pState->SetVirtualWidth(m_width);
    }
    m_pState->CheckColumnWidths();

    if ( GetSelection() )
    {
        CorrectEditorWidgetSizeX();
    }

    m_iFlags &= ~ibPG_FL_RECALCULATING_VIRTUAL_SIZE;
}

// -----------------------------------------------------------------------

void ibPropertyGrid::ReallocDoubleBufferIfNeeded()
{
    if ( !HasExtraStyle(ibPG_EX_NATIVE_DOUBLE_BUFFERING) )
    {
        double scaleFactor = GetDPIScaleFactor();
        int dblh = (m_lineHeight*2);
        if ( !m_doubleBuffer )
        {
            // Create double buffer bitmap to draw on, if none
            int w = wxMax(m_width, FromDIP(250));
            int h = wxMax(m_height + dblh, FromDIP(400));
            m_doubleBuffer = new wxBitmap;
            m_doubleBuffer->CreateWithLogicalSize( w, h, scaleFactor );
        }
        else
        {
            int w = m_doubleBuffer->GetLogicalWidth();
            int h = m_doubleBuffer->GetLogicalHeight();

            // Double buffer must be large enough
            if ( w < m_width || h < (m_height+dblh) )
            {
                if ( w < m_width ) w = m_width;
                if ( h < (m_height+dblh) ) h = m_height + dblh;
                delete m_doubleBuffer;
                m_doubleBuffer = new wxBitmap;
                m_doubleBuffer->CreateWithLogicalSize( w, h, scaleFactor );
            }
        }
    }
}

void ibPropertyGrid::OnResize( wxSizeEvent& event )
{
    if ( !(m_iFlags & ibPG_FL_INITIALIZED) )
        return;

    int width, height;
    GetClientSize(&width, &height);

    m_width = width;
    m_height = height;

    ReallocDoubleBufferIfNeeded();

    m_pState->OnClientWidthChange( width, event.GetSize().x - m_ncWidth, true );
    m_ncWidth = event.GetSize().x;

    if ( !IsFrozen() )
    {
        if ( m_pState->m_itemsAdded )
            PrepareAfterItemsAdded();
        else
            // Without this, virtual size (at least under wxGTK) will be skewed
            RecalculateVirtualSize();

        Refresh();
    }
}

// -----------------------------------------------------------------------

void ibPropertyGrid::SetVirtualWidth( int width )
{
    if ( width == -1 )
    {
        // Disable virtual width
        width = GetClientSize().x;
        ClearInternalFlag(ibPG_FL_HAS_VIRTUAL_WIDTH);
    }
    else
    {
        // Enable virtual width
        SetInternalFlag(ibPG_FL_HAS_VIRTUAL_WIDTH);
    }
    m_pState->SetVirtualWidth( width );
    RecalculateVirtualSize();
    Refresh();
}

void ibPropertyGrid::SetFocusOnCanvas()
{
    // By default, to prevent ibPropertyGrid from stealing focus from
    // other controls, only move focus to the grid if it was already
    // in one of its child controls.
    // If ibPG_EX_ALWAYS_ALLOW_FOCUS flag is set then ibPropertyGrid
    // can take focus on the entire grid area (canvas) even if focus
    // is moved from another control.
    if ( HasExtraStyle(ibPG_EX_ALWAYS_ALLOW_FOCUS) )
    {
        SetFocus();
    }
    else
    {
        wxWindow* focus = wxWindow::FindFocus();
        if ( focus )
        {
            wxWindow* parent = focus->GetParent();
            while ( parent )
            {
                if ( parent == this )
                {
                    SetFocus();
                    break;
                }
                parent = parent->GetParent();
            }
        }
    }

    m_editorFocused = false;
}

// -----------------------------------------------------------------------
// ibPropertyGrid mouse event handling
// -----------------------------------------------------------------------

// selFlags uses same values DoSelectProperty's flags
// Returns true if event was vetoed.
bool ibPropertyGrid::SendEvent( wxEventType eventType, ibPGProperty* p,
                                ibVariant* pValue,
                                ibPGSelectPropertyFlags selFlags,
                                unsigned int column )
{
    // selFlags should have ibPGSelectPropertyFlags::NoValidate if event is not
    // vetoable.

    // Send property grid event of specific type and with specific property
    ibPropertyGridEvent evt( eventType, m_eventObject->GetId() );
    evt.SetPropertyGrid(this);
    evt.SetEventObject(m_eventObject);
    evt.SetProperty(p);
    evt.SetColumn(column);
    if ( eventType == ibEVT_PG_CHANGING )
    {
        wxASSERT( pValue );
        evt.SetCanVeto(true);
        m_validationInfo.SetValue(pValue ? *pValue : ibVariant());
        evt.SetupValidationInfo();
    }
    else
    {
        if ( p )
            evt.SetPropertyValue(p->GetValue());

        if ( !(selFlags & ibPGSelectPropertyFlags::NoValidate) )
            evt.SetCanVeto(true);
    }

    ibPropertyGridEvent* prevProcessedEvent = m_processedEvent;
    m_processedEvent = &evt;
    m_eventObject->HandleWindowEvent(evt);
    m_processedEvent = prevProcessedEvent;

    return evt.WasVetoed();
}

void ibPropertyGrid::SendEvent(wxEventType eventType, int intVal)
{
    ibPropertyGridEvent evt(eventType, m_eventObject->GetId());
    evt.SetPropertyGrid(this);
    evt.SetEventObject(m_eventObject);
    evt.SetProperty(nullptr);
    evt.SetColumn(0);
    evt.SetInt(intVal);

    ibPropertyGridEvent* prevProcessedEvent = m_processedEvent;
    m_processedEvent = &evt;
    m_eventObject->HandleWindowEvent(evt);
    m_processedEvent = prevProcessedEvent;
}

// -----------------------------------------------------------------------

// Return false if should be skipped
bool ibPropertyGrid::HandleMouseClick( int x, unsigned int y, wxMouseEvent &event )
{
    bool res = true;

    // Need to set focus?
    if ( !(m_iFlags & ibPG_FL_FOCUSED) )
    {
        SetFocusOnCanvas();
    }

    ibPropertyGridPageState* state = m_pState;
    int splitterHit;
    int splitterHitOffset;
    int columnHit = state->HitTestH( x, &splitterHit, &splitterHitOffset );

    ibPGProperty* p = DoGetItemAtY(y);

    if ( p )
    {
        int depth = (int)p->GetDepth() - 1;

        int marginEnds = m_marginWidth + ( depth * m_subgroup_extramargin );

        if ( x >= marginEnds )
        {
            // Outside margin.

            if ( p->IsCategory() )
            {
                // This is category.
                ibPGCategoryProperty* pwc = (ibPGCategoryProperty*)p;

                int textX = m_marginWidth + ((pwc->GetDepth()-1)*m_subgroup_extramargin);

                // Expand, collapse, activate etc. if click on text or left of splitter.
                if ( x >= textX
                     &&
                     ( x < (textX+pwc->GetTextExtent(this, m_captionFont)+(ibPG_CAPRECTXMARGIN*2)) ||
                       columnHit == 0
                     )
                    )
                {
                    if ( !AddToSelectionFromInputEvent( p,
                                                        columnHit,
                                                        &event ) )
                        return res;

                    // On double-click, expand/collapse.
                    if ( event.ButtonDClick() && !(m_windowStyle & ibPG_HIDE_MARGIN) )
                    {
                        if ( pwc->IsExpanded() ) DoCollapse( p, true );
                        else DoExpand( p, true );
                    }
                }
            }
            else if ( splitterHit == -1 )
            {
            // Click on value.
                ibPGSelectPropertyFlags selFlag = ibPGSelectPropertyFlags::Null;
                if ( columnHit == 1 )
                {
                    m_iFlags |= ibPG_FL_ACTIVATION_BY_CLICK;
                    selFlag = ibPGSelectPropertyFlags::Focus;
                }
                if ( !AddToSelectionFromInputEvent( p,
                                                    columnHit,
                                                    &event,
                                                    selFlag ) )
                    return res;

                m_iFlags &= ~(ibPG_FL_ACTIVATION_BY_CLICK);

                if ( p->GetChildCount() && !p->IsCategory() )
                    // On double-click, expand/collapse.
                    if ( event.ButtonDClick() && !(m_windowStyle & ibPG_HIDE_MARGIN) )
                    {
                        if ( p->IsExpanded() ) DoCollapse( p, true );
                        else DoExpand( p, true );
                    }

                // Do not Skip() the event after selection has been made.
                // Otherwise default event handling behaviour kicks in
                // and may revert focus back to the main canvas.
                res = true;
            }
            else
            {
            // click on splitter
                if ( !(m_windowStyle & ibPG_STATIC_SPLITTER) )
                {
                    if ( event.GetEventType() == wxEVT_LEFT_DCLICK )
                    {
                        // Double-clicking the splitter causes auto-centering
                        if ( m_pState->GetColumnCount() <= 2 )
                        {
                            ResetColumnSizes( true );

                            SendEvent(ibEVT_PG_COLS_RESIZED, ibNullProperty);
                            SendEvent(ibEVT_PG_COL_DRAGGING,
                                      m_propHover,
                                      nullptr,
                                      ibPGSelectPropertyFlags::NoValidate,
                                      0); // dragged splitter is always 0 here
                        }
                    }
                    else if ( m_dragStatus == 0 )
                    {
                        //
                        // Begin dragging the splitter
                        //

                        // send event
                        DoEndLabelEdit(true, ibPGSelectPropertyFlags::NoValidate);

                        // Allow application to veto dragging
                        if ( !SendEvent(ibEVT_PG_COL_BEGIN_DRAG,
                                        p, nullptr, ibPGSelectPropertyFlags::Null,
                                        (unsigned int)splitterHit) )
                        {
                            if ( m_wndEditor )
                            {
                                // Changes must be committed here or the
                                // value won't be drawn correctly
                                if ( !CommitChangesFromEditor() )
                                    return res;

                                m_wndEditor->Show ( false );
                            }

                            if ( !(m_iFlags & ibPG_FL_MOUSE_CAPTURED) )
                            {
                                CaptureMouse();
                                m_iFlags |= ibPG_FL_MOUSE_CAPTURED;
                            }

                            m_dragStatus = 1;
                            m_draggedSplitter = splitterHit;
                            m_dragOffset = splitterHitOffset;

                            if ( m_wndEditor2 )
                            {
                                m_wndEditor2->Hide();
                            }

                            m_startingSplitterX = x - splitterHitOffset;
                        }
                    }
                }
            }
        }
        else
        {
        // Click on margin.
            if ( p->GetChildCount() )
            {
                int nx = x + m_marginWidth - marginEnds; // Normalize x.

                // Fine tune cell button x
                if ( !p->IsCategory() )
                    nx -= IN_CELL_EXPANDER_BUTTON_X_ADJUST;

                if ( nx >= m_gutterWidth && nx < (m_gutterWidth+m_iconWidth) )
                {
                    int y2 = y % m_lineHeight;
                    if ( y2 >= m_buttonSpacingY && y2 < (m_buttonSpacingY+m_iconHeight) )
                    {
                        // On click on expander button, expand/collapse
                        if ( p->IsExpanded() )
                            DoCollapse( p, true );
                        else
                            DoExpand( p, true );
                    }
                }
            }
        }
    }
    return res;
}

// -----------------------------------------------------------------------

bool ibPropertyGrid::HandleMouseRightClick( int WXUNUSED(x),
                                            unsigned int WXUNUSED(y),
                                            wxMouseEvent& event )
{
    if ( m_propHover )
    {
        // Select property here as well
        ibPGProperty* p = m_propHover;
        AddToSelectionFromInputEvent(p, m_colHover, &event);

        // Send right click event.
        SendEvent( ibEVT_PG_RIGHT_CLICK, p );

        return true;
    }
    return false;
}

// -----------------------------------------------------------------------

bool ibPropertyGrid::HandleMouseDoubleClick( int WXUNUSED(x),
                                             unsigned int WXUNUSED(y),
                                             wxMouseEvent& event )
{
    if ( m_propHover )
    {
        // Select property here as well
        AddToSelectionFromInputEvent(m_propHover, m_colHover, &event);

        // Send double-click event.
        SendEvent( ibEVT_PG_DOUBLE_CLICK, m_propHover );

        return true;
    }
    return false;
}

// -----------------------------------------------------------------------

// Return false if should be skipped
bool ibPropertyGrid::HandleMouseMove( int x, unsigned int y,
                                      wxMouseEvent &event )
{
    // Safety check (needed because mouse capturing may
    // otherwise freeze the control)
    if ( m_dragStatus > 0 && !event.Dragging() )
    {
        HandleMouseUp(x, y, event);
    }

    ibPropertyGridPageState* state = m_pState;
    int splitterHit;
    int splitterHitOffset;
    int columnHit = state->HitTestH( x, &splitterHit, &splitterHitOffset );

    #if wxUSE_TOOLTIPS
        ibPGProperty* prevHover = m_propHover;
        int prevCol = m_colHover;
    #endif
    m_colHover = columnHit;

    if ( m_dragStatus > 0 )
    {
        const int dragMargin = FromDIP(ibPG_DRAG_MARGIN);

        if ( x > (m_marginWidth + dragMargin) &&
             x < (m_pState->GetVirtualWidth() - dragMargin) )
        {

            int splitterX = x - splitterHitOffset;
            int newSplitterX = x - m_dragOffset;

            // Splitter redraw required?
            if ( newSplitterX != splitterX )
            {
                // Move everything
                DoSetSplitter(newSplitterX,
                              m_draggedSplitter,
                              ibPGSplitterPositionFlags::Refresh |
                              ibPGSplitterPositionFlags::FromEvent);

                SendEvent(ibEVT_PG_COLS_RESIZED, ibNullProperty);
                SendEvent(ibEVT_PG_COL_DRAGGING,
                          m_propHover,
                          nullptr,
                    ibPGSelectPropertyFlags::NoValidate,
                          (unsigned int)m_draggedSplitter);
            }

            m_dragStatus = 2;
        }

        return false;
    }
    else
    {

        int ih = m_lineHeight;
        int sy = y;

        int curPropHoverY = y - (y % ih);

        // On which item it hovers
        if ( !m_propHover
             ||
             ( sy < m_propHoverY || sy >= (m_propHoverY+ih) )
           )
        {
            // Mouse moves on another property

            m_propHover = DoGetItemAtY(y);
            m_propHoverY = curPropHoverY;

            // Send hover event
            SendEvent( ibEVT_PG_HIGHLIGHTED, m_propHover );
        }

#if wxUSE_TOOLTIPS
        //
        // If tooltips are enabled, show label or value as a tip
        // in case it doesn't otherwise show in full length.
        //
        if ( m_windowStyle & ibPG_TOOLTIPS )
        {
            if ( m_propHover != prevHover || prevCol != m_colHover )
            {
                if ( m_propHover && !m_propHover->IsCategory() )
                {

                    if ( HasExtraStyle(ibPG_EX_HELP_AS_TOOLTIPS) )
                    {
                        // Show help string as a tooltip
                        wxString tipString = m_propHover->GetHelpString();

                        SetToolTip(tipString);
                    }
                    else if ( m_colHover >= 0 && m_colHover < (int)m_pState->GetColumnCount())
                    {
                        // Show cropped value string as a tooltip
                        wxString tipString;
                        ibPGCell cell;
                        int item = ( m_colHover == 1 ? m_propHover->GetChoiceSelection() : -1 );
                        m_propHover->GetDisplayInfo(m_colHover, item, 0, &tipString, &cell);
                        int space = m_pState->GetColumnWidth(m_colHover);

                        int imageWidth = 0;
                        const wxBitmap& bmp = cell.GetBitmap().GetBitmapFor(this);
                        if ( bmp.IsOk() )
                        {
                            imageWidth = bmp.GetWidth();
                            int hMax = m_lineHeight - ibPG_CUSTOM_IMAGE_SPACINGY - 1;
                            if ( bmp.GetHeight() > hMax )
                                imageWidth = int(double(imageWidth) * hMax / bmp.GetHeight());
                        }

                        if ( m_colHover == 0 )
                        {
                            if ( !(m_windowStyle & ibPG_HIDE_CATEGORIES) || m_propHover->GetParent() != m_pState->DoGetRoot() )
                                space -= (m_propHover->GetDepth()-1)*m_subgroup_extramargin;
                        }
                        else if ( m_colHover == 1 && !m_propHover->IsValueUnspecified())
                        {
                            wxSize imageSize = GetImageSize(m_propHover, -1);
                            if ( imageSize.x > 0 )
                                imageWidth = imageSize.x;
                            tipString = m_propHover->GetValueAsString();
                            if ( GetColumnCount() <= 2 )
                            {
                                wxString unitsString = m_propHover->GetAttribute(ibPG_ATTR_UNITS, wxString());
                                if ( !unitsString.empty() )
                                    tipString = wxString::Format(wxS("%s %s"), tipString, unitsString );
                            }
                        }

                        space -= m_propHover->GetImageOffset(imageWidth);
                        space -= (ibPG_XBEFORETEXT + 1);
                        int tw, th;
                        const wxFont* font = nullptr;
                        if ( (m_windowStyle & ibPG_BOLD_MODIFIED) && m_propHover->HasFlag(ibPGFlags::Modified) )
                            font = &m_captionFont;
                        if ( cell.GetFont().IsOk() )
                            font = &cell.GetFont();
                        GetTextExtent( tipString, &tw, &th, nullptr, nullptr, font );
                        if ( tw > space )
                            SetToolTip( tipString );
                    }
                    else
                    {
                        SetToolTip(wxString());
                    }
                }
                else
                {
                    SetToolTip(wxString());
                }
            }
        }
#endif // wxUSE_TOOLTIPS

        if ( splitterHit == -1 ||
             !m_propHover ||
             HasFlag(ibPG_STATIC_SPLITTER) )
        {
            // hovering on something else
            if ( m_curcursor != wxCURSOR_ARROW )
                CustomSetCursor( wxCURSOR_ARROW );
        }
        else
        {
            // Do not allow splitter cursor on caption items.
            // (also not if we were dragging and it's started
            // outside the splitter region)

            if ( !m_propHover->IsCategory() &&
                 !event.Dragging() )
            {

                // hovering on splitter

                // NB: Condition disabled since MouseLeave event (from the
                //     editor control) cannot be reliably detected.
                //if ( m_curcursor != wxCURSOR_SIZEWE )
                CustomSetCursor( wxCURSOR_SIZEWE, true );

                return false;
            }
            else
            {
                // hovering on something else
                if ( m_curcursor != wxCURSOR_ARROW )
                    CustomSetCursor( wxCURSOR_ARROW );
            }
        }

        //
        // Multi select by dragging
        //
        if ( HasExtraStyle(ibPG_EX_MULTIPLE_SELECTION) &&
             event.LeftIsDown() &&
             m_propHover &&
             GetSelection() &&
             columnHit != 1 &&
             !state->DoIsPropertySelected(m_propHover) )
        {
            // Additional requirement is that the hovered property
            // is adjacent to edges of selection.
            const ibArrayPGProperty& selection = GetSelectedProperties();

            // Since categories cannot be selected along with 'other'
            // properties, exclude them from iterator flags.
            int iterFlags = ibPG_ITERATE_VISIBLE & (~ibPGFlags::Category);

            for ( int i=(selection.size()-1); i>=0; i-- )
            {
                // TODO: This could be optimized by keeping track of
                //       which properties are at the edges of selection.
                ibPGProperty* selProp = selection[i];
                if ( state->ArePropertiesAdjacent(m_propHover, selProp,
                                                  iterFlags) )
                {
                    DoAddToSelection(m_propHover);
                    break;
                }
            }
        }
    }
    return true;
}

// -----------------------------------------------------------------------

// Also handles Leaving event
bool ibPropertyGrid::HandleMouseUp( int x, unsigned int WXUNUSED(y),
                                    wxMouseEvent &WXUNUSED(event) )
{
    ibPropertyGridPageState* state = m_pState;
    bool res = false;

    int splitterHit;
    int splitterHitOffset;
    state->HitTestH( x, &splitterHit, &splitterHitOffset );

    // No event type check - basically calling this method should
    // just stop dragging.
    // Left up after dragged?
    if ( m_dragStatus >= 1 )
    {
    //
    // End Splitter Dragging
    //
        // DO NOT ENABLE FOLLOWING LINE!
        // (it is only here as a reminder to not to do it)
        //splitterX = x;

        SendEvent(ibEVT_PG_COL_END_DRAG,
                  m_propHover,
                  nullptr,
                  ibPGSelectPropertyFlags::NoValidate,
                  (unsigned int)m_draggedSplitter);

        // Disable splitter auto-centering (but only if moved any -
        // otherwise we end up disabling auto-center even after a
        // recentering double-click).
        int posDiff = abs(m_startingSplitterX -
                          GetSplitterPosition(m_draggedSplitter));

        if ( posDiff > 1 )
            state->m_dontCenterSplitter = true;

        // This is necessary to return cursor
        if ( m_iFlags & ibPG_FL_MOUSE_CAPTURED )
        {
            ReleaseMouse();
            m_iFlags &= ~(ibPG_FL_MOUSE_CAPTURED);
        }

        // Set back the default cursor, if necessary
        if ( splitterHit == -1 ||
             !m_propHover )
        {
            CustomSetCursor( wxCURSOR_ARROW );
        }

        m_dragStatus = 0;

        // Control background needs to be cleared
        ibPGProperty* selected = GetSelection();
        if ( !(m_iFlags & ibPG_FL_PRIMARY_FILLS_ENTIRE) && selected )
            DrawItem( selected );

        if ( m_wndEditor )
        {
            m_wndEditor->Show ( true );
        }

        if ( m_wndEditor2 )
        {
            m_wndEditor2->Show(true);
        }

        // This clears the focus.
        m_editorFocused = false;

    }
    return res;
}

// -----------------------------------------------------------------------

bool ibPropertyGrid::OnMouseCommon( wxMouseEvent& event, int* px, int* py )
{
    int splitterX = GetSplitterPosition();

    int ux, uy;
    CalcUnscrolledPosition( event.m_x, event.m_y, &ux, &uy );

    wxWindow* wnd = GetEditorControl();

    // Hide popup on clicks
    if ( event.GetEventType() != wxEVT_MOTION )
        if ( wxDynamicCast(wnd, ibComboBox) )
        {
            ((ibComboBox*)wnd)->HidePopup();
        }

    wxRect r;
    if ( wnd )
        r = wnd->GetRect();
    if ( wnd == nullptr || m_dragStatus ||
         (
           ux <= (splitterX + ibPG_SPLITTERX_DETECTMARGIN2) ||
           ux >= (r.x+r.width) ||
           event.m_y < r.y ||
           event.m_y >= (r.y+r.height)
         )
       )
    {
        *px = ux;
        *py = uy;
        return true;
    }
    else
    {
        if ( m_curcursor != wxCURSOR_ARROW ) CustomSetCursor ( wxCURSOR_ARROW );
    }
    return false;
}

// -----------------------------------------------------------------------

void ibPropertyGrid::OnMouseClick( wxMouseEvent &event )
{
    int x, y;
    if ( OnMouseCommon( event, &x, &y ) )
    {
        if ( !HandleMouseClick(x, y, event) )
            event.Skip();
    }
    else
    {
        event.Skip();
    }
}

// -----------------------------------------------------------------------

void ibPropertyGrid::OnMouseRightClick( wxMouseEvent &event )
{
    int x, y;
    CalcUnscrolledPosition( event.m_x, event.m_y, &x, &y );
    HandleMouseRightClick(x,y,event);
    event.Skip();
}

// -----------------------------------------------------------------------

void ibPropertyGrid::OnMouseDoubleClick( wxMouseEvent &event )
{
    // Always run standard mouse-down handler as well
    OnMouseClick(event);

    int x, y;
    CalcUnscrolledPosition( event.m_x, event.m_y, &x, &y );
    HandleMouseDoubleClick(x,y,event);

    // Do not Skip() event here - OnMouseClick() call above
    // should have already taken care of it.
}

// -----------------------------------------------------------------------

void ibPropertyGrid::OnMouseMove( wxMouseEvent &event )
{
    int x, y;
    if ( OnMouseCommon( event, &x, &y ) )
    {
        HandleMouseMove(x,y,event);
    }
    event.Skip();
}

// -----------------------------------------------------------------------

void ibPropertyGrid::OnMouseUp( wxMouseEvent &event )
{
    int x, y;
    if ( OnMouseCommon( event, &x, &y ) )
    {
        if ( !HandleMouseUp(x, y, event) )
            event.Skip();
    }
    else
    {
        event.Skip();
    }
}

// -----------------------------------------------------------------------

void ibPropertyGrid::OnMouseEntry( wxMouseEvent &event )
{
    // This may get called from child control as well, so event's
    // mouse position cannot be relied on.

    if ( event.Entering() )
    {
        if ( !(m_iFlags & ibPG_FL_MOUSE_INSIDE) )
        {
            // TODO: Fix this (detect parent and only do
            //   cursor trick if it is a manager).
            wxASSERT( GetParent() );
            GetParent()->SetCursor(wxNullCursor);

            m_iFlags |= ibPG_FL_MOUSE_INSIDE;
        }
        else
            GetParent()->SetCursor(wxNullCursor);
    }
    else if ( event.Leaving() )
    {
        // Without this, wxSpinCtrl editor will sometimes have wrong cursor
        SetCursor( wxNullCursor );

        // Get real cursor position
        wxPoint pt = ScreenToClient(::wxGetMousePosition()) - GetClientAreaOrigin();

        if ( pt.x <= 0 || pt.y <= 0 || pt.x >= m_width || pt.y >= m_height )
        {
            {
                if ( m_iFlags & ibPG_FL_MOUSE_INSIDE )
                {
                    m_iFlags &= ~(ibPG_FL_MOUSE_INSIDE);
                }

                if ( m_dragStatus )
                    ibPropertyGrid::HandleMouseUp ( -1, 10000, event );
            }
        }
    }

    event.Skip();
}

// -----------------------------------------------------------------------

// Common code used by various OnMouseXXXChild methods.
bool ibPropertyGrid::OnMouseChildCommon( wxMouseEvent &event, int* px, int *py )
{
    wxWindow* topCtrlWnd = (wxWindow*)event.GetEventObject();
    wxASSERT( topCtrlWnd );
    int x, y;
    event.GetPosition(&x,&y);

    int splitterX = GetSplitterPosition();

    wxRect r = topCtrlWnd->GetRect();
    int ux, uy;
    CalcUnscrolledPosition(r.x + x, r.y + y, &ux, &uy);

    if ( !m_dragStatus &&
         ux > (splitterX + ibPG_SPLITTERX_DETECTMARGIN2) &&
         y >= 0 && y < r.height
       )
    {
        if ( m_curcursor != wxCURSOR_ARROW ) CustomSetCursor ( wxCURSOR_ARROW );
        event.Skip();
    }
    else
    {
        if ( px )
        {
            *px = ux;
        }
        if ( py )
        {
            *py = uy;
        }
        return true;
    }
    return false;
}

void ibPropertyGrid::OnMouseClickChild( wxMouseEvent &event )
{
    int x,y;
    if ( OnMouseChildCommon(event,&x,&y) )
    {
        bool res = HandleMouseClick(x,y,event);
        if ( !res ) event.Skip();
    }
}

// SEAM vs wx: a right click on the editor's value is the editor's, as a left one is (OnMouseChildCommon): its own menu,
// its text's selection kept. wx took it for the property's - it selected the property again, which reset the editor's
// selection, and ate the click, so the field's menu never came (WM_CONTEXTMENU follows a right button up let through)
void ibPropertyGrid::OnMouseRightClickChild( wxMouseEvent &event )
{
    int x,y;
    if ( OnMouseChildCommon(event,&x,&y) )
    {
        bool res = HandleMouseRightClick(x,y,event);
        if ( !res ) event.Skip();
    }
}

void ibPropertyGrid::OnMouseMoveChild( wxMouseEvent &event )
{
    int x,y;
    if ( OnMouseChildCommon(event,&x,&y) )
    {
        bool res = HandleMouseMove(x,y,event);
        if ( !res ) event.Skip();
    }
}

void ibPropertyGrid::OnMouseUpChild( wxMouseEvent &event )
{
    int x,y;
    if ( OnMouseChildCommon(event,&x,&y) )
    {
        bool res = HandleMouseUp(x,y,event);
        if ( !res ) event.Skip();
    }
}

// -----------------------------------------------------------------------
// ibPropertyGrid keyboard event handling
// -----------------------------------------------------------------------

std::pair<ibPGKeyboardAction, ibPGKeyboardAction> ibPropertyGrid::KeyEventToActions(const wxKeyEvent& event) const
{
    // Translates wxKeyEvent to ibPGKeyboardAction::XXX

    int keycode = event.GetKeyCode();
    int modifiers = event.GetModifiers();

    wxASSERT(!(modifiers & ~(0xFFFF)));

    int hashMapKey = (keycode & 0xFFFF) | ((modifiers & 0xFFFF) << 16);

    auto it = m_actionTriggers.find(hashMapKey);

    if ( it == m_actionTriggers.end() )
        return std::make_pair(ibPGKeyboardAction::Invalid, ibPGKeyboardAction::Invalid);

    return it->second;
}

ibPGKeyboardAction ibPropertyGrid::KeyEventToAction(wxKeyEvent& event) const
{
    return KeyEventToActions(event).first;
}

void ibPropertyGrid::AddActionTrigger(ibPGKeyboardAction action, int keycode, int modifiers)
{
    wxASSERT( !(modifiers&~(0xFFFF)) );

    int hashMapKey = (keycode & 0xFFFF) | ((modifiers & 0xFFFF) << 16);

    std::pair<ibPGKeyboardAction, ibPGKeyboardAction> curActions;

    auto it = m_actionTriggers.find(hashMapKey);
    if ( it != m_actionTriggers.end() )
    {
        // This key combination is already used
        curActions = it->second;

        // Can add secondary?
        wxASSERT_MSG( curActions.second == ibPGKeyboardAction::Invalid,
                      "You can only add up to two separate actions per key combination." );

        curActions.second = action;
    }
    else
    {
        curActions = std::make_pair(action, ibPGKeyboardAction::Invalid);
    }

    m_actionTriggers[hashMapKey] = curActions;
}

void ibPropertyGrid::ClearActionTriggers(ibPGKeyboardAction action)
{
//    wxCHECK_RET(!(action & ~(0xFFFF)), wxS("You can only clear triggers for one action at a time.")

    auto it = m_actionTriggers.begin();
    while ( it != m_actionTriggers.end() )
    {
        if ( it->second.second == action )
        {
            it->second.second = ibPGKeyboardAction::Invalid;
        }

        if ( it->second.first == action )
        {
            if ( it->second.second == ibPGKeyboardAction::Invalid )
            {
                it = m_actionTriggers.erase(it);
                continue;
            }

            it->second.first = it->second.second;
        }

        ++it;
    }
}

void ibPropertyGrid::HandleKeyEvent( wxKeyEvent &event, bool fromChild )
{
    //
    // Handles key event when editor control is not focused.
    //

    wxCHECK_RET(!IsFrozen(), "ibPropertyGrid shouldn't be frozen");

    // Traversal between items, collapsing/expanding, etc.
    ibPGProperty* selected = GetSelection();
    int keycode = event.GetKeyCode();
    bool editorFocused = IsEditorFocused();

    if ( keycode == WXK_TAB )
    {
        if ( !event.ShiftDown() )
        {
            if ( !editorFocused && m_wndEditor )
            {
                DoSelectProperty( selected, ibPGSelectPropertyFlags::Focus );
            }
            else
            {
                // Tab traversal workaround for platforms on which
                // wxWindow::Navigate() may navigate into first child
                // instead of next sibling.
            #if defined(__WXGTK__)
                SetFocus();
            #endif

                Navigate(wxNavigationKeyEvent::IsForward);
            }
        }
        else
        {
            if ( editorFocused )
            {
                UnfocusEditor();
            }
            else
            {
            #if defined(__WXGTK__)
                SetFocus();
            #endif

                Navigate(wxNavigationKeyEvent::IsBackward);
            }
        }

        return;
    }

    // Ignore Alt and Control when they are down alone
    if ( keycode == WXK_ALT ||
         keycode == WXK_CONTROL )
    {
        event.Skip();
        return;
    }

    ibPGKeyboardAction action;
    ibPGKeyboardAction secondAction;
    std::tie(action, secondAction) = KeyEventToActions(event);

    if ( editorFocused && action == ibPGKeyboardAction::CancelEdit )
    {
        //
        // Esc cancels any changes
        if ( IsEditorsValueModified() )
        {
            EditorsValueWasNotModified();

            // Update the control as well
            selected->GetEditorClass()->
                SetControlStringValue( selected,
                                       GetEditorControl(),
                                       selected->GetDisplayedString() );
        }

        OnValidationFailureReset(selected);

        UnfocusEditor();
        return;
    }

    // Except for TAB, ESC, and any keys specifically dedicated to
    // ibPropertyGrid itself, handle child control events in child control.
    if ( fromChild && m_dedicatedKeys.count(keycode) == 0 )
    {
        // Only propagate event if it had modifiers
        if ( !event.HasModifiers() )
        {
            event.StopPropagation();
        }
        event.Skip();
        return;
    }

    bool wasHandled = false;

    if ( selected )
    {
        // Show dialog?
        if ( ButtonTriggerKeyTest(action, event) )
            return;

        ibPGProperty* p = selected;

        if ( action == ibPGKeyboardAction::Edit && !editorFocused )
        {
            // Mark as handled only for editable property
            if ( !p->IsCategory() && p->IsEnabled() && !p->HasFlag(ibPGFlags::ReadOnly) )
            {
                DoSelectProperty( p, ibPGSelectPropertyFlags::Focus );
                wasHandled = true;
            }
        }

        // Travel and expand/collapse
        int selectDir = -2;

        if ( p->GetChildCount() )
        {
            if ( action == ibPGKeyboardAction::CollapseProperty || secondAction == ibPGKeyboardAction::CollapseProperty )
            {
                if ( (m_windowStyle & ibPG_HIDE_MARGIN) || DoCollapse(p, true) )
                    wasHandled = true;
            }
            else if ( action == ibPGKeyboardAction::ExpandProperty || secondAction == ibPGKeyboardAction::ExpandProperty )
            {
                if ( (m_windowStyle & ibPG_HIDE_MARGIN) || DoExpand(p, true) )
                    wasHandled = true;
            }
        }

        if ( !wasHandled )
        {
            if ( action == ibPGKeyboardAction::PrevProperty || secondAction == ibPGKeyboardAction::PrevProperty )
            {
                selectDir = -1;
            }
            else if ( action == ibPGKeyboardAction::NextProperty || secondAction == ibPGKeyboardAction::NextProperty )
            {
                selectDir = 1;
            }
        }

        if ( selectDir >= -1 )
        {
            p = ibPropertyGridIterator::OneStep( m_pState, ibPG_ITERATE_VISIBLE, p, selectDir );
            if ( p )
            {
                ibPGSelectPropertyFlags selFlags = ibPGSelectPropertyFlags::Null;
                int reopenLabelEditorCol = -1;

                if ( action == ibPGKeyboardAction::Edit )
                {
                    // Make the next editor focused as well
                    // if we are actually going to edit the property.
                    selFlags |= ibPGSelectPropertyFlags::Focus;
                }
                else
                {
                    // Also maintain the same label editor focus state
                    if ( m_labelEditor )
                        reopenLabelEditorCol = m_selColumn;
                }

                DoSelectProperty(p, selFlags);

                if ( reopenLabelEditorCol >= 0 )
                    DoBeginLabelEdit(reopenLabelEditorCol);
            }
            else if ( action == ibPGKeyboardAction::Edit )
            {
                // For first and last item just validate the value
                CommitChangesFromEditor();
            }
            wasHandled = true;
        }
    }
    else
    {
        // If nothing was selected, select the first item now
        // (or navigate out of tab).
        if ( action != ibPGKeyboardAction::CancelEdit && secondAction != ibPGKeyboardAction::CancelEdit )
        {
            ibPGProperty* p = ibPropertyGridInterface::GetFirst();
            if ( p ) DoSelectProperty(p);
            wasHandled = true;
        }
    }

    if ( !wasHandled )
        event.Skip();
}

// -----------------------------------------------------------------------

void ibPropertyGrid::OnKey( wxKeyEvent &event )
{
    // If there was editor open and focused, then this event should not
    // really be processed here.
    if ( IsEditorFocused() )
    {
        // However, if event had modifiers, it is probably still best
        // to skip it.
        if ( event.HasModifiers() )
            event.Skip();
        else
            event.StopPropagation();
        return;
    }

    HandleKeyEvent(event, false);
}

// -----------------------------------------------------------------------

bool ibPropertyGrid::ButtonTriggerKeyTest(ibPGKeyboardAction action, wxKeyEvent& event)
{
    if ( action == ibPGKeyboardAction::Invalid )
    {
        action = KeyEventToActions(event).first;
    }

    // Does the keycode trigger button?
    if ( action == ibPGKeyboardAction::PressButton &&
         m_wndEditor2 )
    {
        wxCommandEvent evt(wxEVT_BUTTON, m_wndEditor2->GetId());
        GetEventHandler()->AddPendingEvent(evt);
        return true;
    }

    return false;
}

// -----------------------------------------------------------------------

void ibPropertyGrid::OnChildKeyDown( wxKeyEvent &event )
{
    HandleKeyEvent(event, true);
}

// -----------------------------------------------------------------------
// ibPropertyGrid miscellaneous event handling
// -----------------------------------------------------------------------

void ibPropertyGrid::OnIdle( wxIdleEvent& WXUNUSED(event) )
{
    // Skip fake idle events generated e.g. by calling
    // wxYield from within event handler.
    if ( m_processedEvent )
        return;

    //
    // Check if the focus is in this control or one of its children
    wxWindow* newFocused = wxWindow::FindFocus();

    if ( newFocused != m_curFocused )
        HandleFocusChange( newFocused );

    //
    // Check if top-level parent has changed
    if ( HasExtraStyle(ibPG_EX_ENABLE_TLP_TRACKING) )
    {
        wxWindow* tlp = ::wxGetTopLevelParent(this);
        if ( tlp != m_tlp )
            OnTLPChanging(tlp);
    }

    // Delete pending property editors and their event handlers.
    DeletePendingObjects();

    //
    // Resolve pending property removals
    // In order to determine whether deletion/removal
    // was done we need to track the size of the list
    // before and after the operation.
    // (Note that lists are changed at every operation.)
    size_t cntAfter = m_deletedProperties.size();
    while ( cntAfter > 0 )
    {
        size_t cntBefore = cntAfter;

        DeleteProperty(*(m_deletedProperties.begin()));

        cntAfter = m_deletedProperties.size();
        wxASSERT_MSG( cntAfter <= cntBefore,
            wxS("Increased number of pending items after deletion") );
        // Break if deletion was not done
        if ( cntAfter >= cntBefore )
            break;
    }
    cntAfter = m_removedProperties.size();
    while ( cntAfter > 0 )
    {
        size_t cntBefore = cntAfter;

        RemoveProperty(*(m_removedProperties.begin()));

        cntAfter = m_removedProperties.size();
        wxASSERT_MSG( cntAfter <= cntBefore,
            wxS("Increased number of pending items after removal") );
        // Break if removal was not done
        if ( cntAfter >= cntBefore )
            break;
    }
}

bool ibPropertyGrid::IsEditorFocused() const
{
    wxWindow* focus = wxWindow::FindFocus();

    if ( focus == m_wndEditor || focus == m_wndEditor2 ||
         focus == GetEditorControl() ||
         // In case a combobox text control is focused
         (focus && focus->GetParent() && (focus->GetParent() == m_wndEditor)) )
         return true;

    return false;
}

// Called by focus event handlers. newFocused is the window that becomes focused.
void ibPropertyGrid::HandleFocusChange( wxWindow* newFocused )
{
    //
    // Never allow focus to be changed when handling editor event.
    // Especially because they may be displaying a dialog which
    // could cause all kinds of weird (native) focus changes.
    if ( HasInternalFlag(ibPG_FL_IN_HANDLECUSTOMEDITOREVENT) )
        return;

    unsigned int oldFlags = m_iFlags;
    bool wasEditorFocused = false;
    wxWindow* wndEditor = m_wndEditor;

    m_iFlags &= ~(ibPG_FL_FOCUSED);

    wxWindow* parent = newFocused;

    // This must be one of nextFocus' parents.
    while ( parent )
    {
        if ( parent == wndEditor )
        {
            // If editor is active consider focus set on its components
            // as a focus set on the editor itself (to prevent doing actions
            // when focus is switched between subcontrols of a compound
            // editor like e.g. ibComboCtrl).
            newFocused = wndEditor;
            wasEditorFocused = true;
        }
        // Use m_eventObject, which is either ibPropertyGrid or
        // ibPropertyGridManager, as appropriate.
        else if ( parent == m_eventObject )
        {
            m_iFlags |= ibPG_FL_FOCUSED;
            break;
        }
        parent = parent->GetParent();
    }

    // Notify editor control when it receives a focus
    if ( wasEditorFocused && m_curFocused != newFocused )
    {
        ibPGProperty* p = GetSelection();
        if ( p )
        {
            const ibPGEditor* editor = p->GetEditorClass();
            ResetEditorAppearance();
            editor->OnFocus(p, GetEditorControl());
        }
    }

    m_curFocused = newFocused;

    if ( (m_iFlags & ibPG_FL_FOCUSED) !=
         (oldFlags & ibPG_FL_FOCUSED) )
    {
        if ( !(m_iFlags & ibPG_FL_FOCUSED) )
        {
            // Need to store changed value
            CommitChangesFromEditor();
        }
        else
        {
            /*
            //
            // Preliminary code for tab-order respecting
            // tab-traversal (but should be moved to
            // OnNav handler)
            //
            wxWindow* prevFocus = event.GetWindow();
            wxWindow* useThis = this;
            if ( m_iFlags & ibPG_FL_IN_MANAGER )
                useThis = GetParent();

            if ( prevFocus &&
                 prevFocus->GetParent() == useThis->GetParent() )
            {
                wxList& children = useThis->GetParent()->GetChildren();

                wxNode* node = children.Find(prevFocus);

                if ( node->GetNext() &&
                     useThis == node->GetNext()->GetData() )
                    DoSelectProperty(GetFirst());
                else if ( node->GetPrevious () &&
                          useThis == node->GetPrevious()->GetData() )
                    DoSelectProperty(GetLastProperty());

            }
            */
        }

        // Redraw selected
        if ( m_iFlags & ibPG_FL_INITIALIZED )
        {
            const ibArrayPGProperty& sel = GetSelectedProperties();
            for( ibPGProperty* selProp : sel )
            {
                DrawItem(selProp);
            }
        }
    }
}

void ibPropertyGrid::OnFocusEvent( wxFocusEvent& event )
{
    if ( event.GetEventType() == wxEVT_SET_FOCUS )
        HandleFocusChange((wxWindow*)event.GetEventObject());
    // Line changed to "else" when applying ibPropertyGrid patch #1675902
    //else if ( event.GetWindow() )
    else
        HandleFocusChange(event.GetWindow());

    event.Skip();
}

// -----------------------------------------------------------------------

void ibPropertyGrid::OnChildFocusEvent( wxChildFocusEvent& event )
{
    HandleFocusChange((wxWindow*)event.GetEventObject());
    event.Skip();
}

// -----------------------------------------------------------------------

void ibPropertyGrid::OnScrollEvent( wxScrollWinEvent &event )
{
    m_iFlags |= ibPG_FL_SCROLLED;

    event.Skip();
}

// -----------------------------------------------------------------------

void ibPropertyGrid::OnCaptureChange( wxMouseCaptureChangedEvent& WXUNUSED(event) )
{
    if ( m_iFlags & ibPG_FL_MOUSE_CAPTURED )
    {
        m_iFlags &= ~(ibPG_FL_MOUSE_CAPTURED);
    }
}

// -----------------------------------------------------------------------
// Property editor related functions
// -----------------------------------------------------------------------

// noDefCheck = true prevents infinite recursion.
ibPGEditor* ibPropertyGrid::DoRegisterEditorClass( ibPGEditor* editorClass,
                                                   const wxString& editorName,
                                                   bool noDefCheck )
{
    wxCHECK( editorClass, nullptr );

    if ( !noDefCheck && ibPGGlobalVars->m_mapEditorClasses.empty() )
        RegisterDefaultEditors();

    wxString name = editorName.empty() ? editorClass->GetName() : editorName;

    // Existing editor under this name?
    auto vt_it = ibPGGlobalVars->m_mapEditorClasses.find(name);

    if ( vt_it != ibPGGlobalVars->m_mapEditorClasses.end() )
    {
        // If this name was already used, try class name.
        name = editorClass->GetClassInfo()->GetClassName();
        vt_it = ibPGGlobalVars->m_mapEditorClasses.find(name);
    }

    wxCHECK_MSG( vt_it == ibPGGlobalVars->m_mapEditorClasses.end(),
                 vt_it->second,
                 wxS("Editor with given name was already registered") );

    ibPGGlobalVars->m_mapEditorClasses[name] = editorClass;

    return editorClass;
}

// Use this in RegisterDefaultEditors.
#define ibPGRegisterDefaultEditorClass(EDITOR) \
    if ( ibPGEditor_##EDITOR == nullptr ) \
    { \
        ibPGEditor_##EDITOR = ibPropertyGrid::RegisterEditorClass( \
            new ibPG##EDITOR##Editor, true ); \
    }

// Registers all default editor classes
void ibPropertyGrid::RegisterDefaultEditors()
{
    ibPGRegisterDefaultEditorClass( TextCtrl );
    ibPGRegisterDefaultEditorClass( Choice );
    ibPGRegisterDefaultEditorClass( ComboBox );
    ibPGRegisterDefaultEditorClass( TextCtrlAndButton );
#if ibPG_INCLUDE_CHECKBOX
    ibPGRegisterDefaultEditorClass( CheckBox );
#endif
    ibPGRegisterDefaultEditorClass( ChoiceAndButton );

    // Register SpinCtrl etc. editors before use
    RegisterAdditionalEditors();
}

// -----------------------------------------------------------------------
// ibPGStringTokenizer
//   Needed to handle C-style string lists (e.g. "str1" "str2")
// -----------------------------------------------------------------------

ibPGStringTokenizer::ibPGStringTokenizer( const wxString& str, wxChar delimiter )
    : m_str(str), m_curPos(str.begin()), m_delimiter(delimiter)
{
}

bool ibPGStringTokenizer::HasMoreTokens()
{
    const wxString& str = m_str;

    wxString::const_iterator i = m_curPos;

    wxUniChar delim = m_delimiter;
    wxUniChar prev_a = wxS('\0');

    bool inToken = false;

    while ( i != str.end() )
    {
        wxUniChar a = *i;

        if ( !inToken )
        {
            if ( a == delim )
            {
                inToken = true;
                m_readyToken.clear();
            }
        }
        else
        {
            if ( prev_a != wxS('\\') )
            {
                if ( a != delim )
                {
                    if ( a != wxS('\\') )
                        m_readyToken << a;
                }
                else
                {
                    ++i;
                    m_curPos = i;
                    return true;
                }
                prev_a = a;
            }
            else
            {
                m_readyToken << a;
                prev_a = wxS('\0');
            }
        }
        ++i;
    }

    m_curPos = str.end();

    if ( inToken )
        return true;

    return false;
}

wxString ibPGStringTokenizer::GetNextToken()
{
    return m_readyToken;
}

// -----------------------------------------------------------------------
// ibPropertyGridEvent
// -----------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(ibPropertyGridEvent, wxCommandEvent);


wxDEFINE_EVENT( ibEVT_PG_SELECTED, ibPropertyGridEvent );
wxDEFINE_EVENT( ibEVT_PG_CHANGING, ibPropertyGridEvent );
wxDEFINE_EVENT( ibEVT_PG_CHANGED, ibPropertyGridEvent );
wxDEFINE_EVENT( ibEVT_PG_HIGHLIGHTED, ibPropertyGridEvent );
wxDEFINE_EVENT( ibEVT_PG_RIGHT_CLICK, ibPropertyGridEvent );
wxDEFINE_EVENT( ibEVT_PG_PAGE_CHANGED, ibPropertyGridEvent );
wxDEFINE_EVENT( ibEVT_PG_ITEM_EXPANDED, ibPropertyGridEvent );
wxDEFINE_EVENT( ibEVT_PG_ITEM_COLLAPSED, ibPropertyGridEvent );
wxDEFINE_EVENT( ibEVT_PG_DOUBLE_CLICK, ibPropertyGridEvent );
wxDEFINE_EVENT( ibEVT_PG_LABEL_EDIT_BEGIN, ibPropertyGridEvent );
wxDEFINE_EVENT( ibEVT_PG_LABEL_EDIT_ENDING, ibPropertyGridEvent );
wxDEFINE_EVENT( ibEVT_PG_COL_BEGIN_DRAG, ibPropertyGridEvent );
wxDEFINE_EVENT( ibEVT_PG_COL_DRAGGING, ibPropertyGridEvent );
wxDEFINE_EVENT( ibEVT_PG_COL_END_DRAG, ibPropertyGridEvent );
// Events used only internally
wxDEFINE_EVENT( ibEVT_PG_HSCROLL, ibPropertyGridEvent);
wxDEFINE_EVENT( ibEVT_PG_COLS_RESIZED, ibPropertyGridEvent);

// -----------------------------------------------------------------------

ibPropertyGridEvent::ibPropertyGridEvent(wxEventType commandType, int id)
    : wxCommandEvent(commandType,id)
    , m_property(nullptr)
    , m_pg(nullptr)
    , m_validationInfo(nullptr)
    , m_column(1)
    , m_canVeto(false)
    , m_wasVetoed(false)
{
}

// -----------------------------------------------------------------------

ibPropertyGridEvent::ibPropertyGridEvent(const ibPropertyGridEvent& event)
    : wxCommandEvent(event)
    , m_property(event.m_property)
    , m_pg(event.m_pg)
    , m_validationInfo(event.m_validationInfo)
    , m_column(event.m_column)
    , m_canVeto(event.m_canVeto)
    , m_wasVetoed(event.m_wasVetoed)
{
    OnPropertyGridSet();
}

// -----------------------------------------------------------------------

void ibPropertyGridEvent::OnPropertyGridSet()
{
    if ( !m_pg )
        return;

#if wxUSE_THREADS
    wxCriticalSectionLocker lock(ibPGGlobalVars->m_critSect);
#endif
    m_pg->m_liveEvents.push_back(this);
}

// -----------------------------------------------------------------------

ibPropertyGridEvent::~ibPropertyGridEvent()
{
    if ( m_pg )
    {
    #if wxUSE_THREADS
        wxCriticalSectionLocker lock(ibPGGlobalVars->m_critSect);
    #endif

        // Use iterate from the back since it is more likely that the event
        // being destroyed is at the end of the array.
        std::vector<ibPropertyGridEvent*>& liveEvents = m_pg->m_liveEvents;

        for ( std::vector<ibPropertyGridEvent*>::reverse_iterator rit = liveEvents.rbegin(); rit != liveEvents.rend(); ++rit )
        {
            if ( *rit == this )
            {
                // The base iterator refers to the element that is next
                // to the element the reverse_iterator is currently pointing to.
                liveEvents.erase(rit.base() - 1);
                break;
            }
        }
    }
}

// -----------------------------------------------------------------------

wxEvent* ibPropertyGridEvent::Clone() const
{
    return new ibPropertyGridEvent( *this );
}

// -----------------------------------------------------------------------
// ibPropertyGridPopulator
// -----------------------------------------------------------------------

ibPropertyGridPopulator::ibPropertyGridPopulator()
    : m_pg(nullptr)
    , m_state(nullptr)
{
    ibPGGlobalVars->m_offline++;
}

// -----------------------------------------------------------------------

void ibPropertyGridPopulator::SetState( ibPropertyGridPageState* state )
{
    m_state = state;
    m_propHierarchy.clear();
}

// -----------------------------------------------------------------------

void ibPropertyGridPopulator::SetGrid( ibPropertyGrid* pg )
{
    m_pg = pg;
    pg->Freeze();
}

// -----------------------------------------------------------------------

ibPropertyGridPopulator::~ibPropertyGridPopulator()
{
    // Free unused sets of choices
    for( const auto& it : m_dictIdChoices )
    {
        it.second->DecRef();
    }

    if ( m_pg )
    {
        m_pg->Thaw();
        m_pg->GetPanel()->Refresh();
    }
    ibPGGlobalVars->m_offline--;
}

// -----------------------------------------------------------------------

ibPGProperty* ibPropertyGridPopulator::Add( const wxString& propClass,
                                            const wxString& propLabel,
                                            const wxString& propName,
                                            const wxString* propValue,
                                            ibPGChoices* pChoices )
{
    wxClassInfo* classInfo = wxClassInfo::FindClass(propClass);
    ibPGProperty* parent = GetCurParent();

    if ( parent->HasFlag(ibPGFlags::Aggregate) )
    {
        ProcessError(wxString::Format(wxS("new children cannot be added to '%s'"),parent->GetName()));
        return nullptr;
    }

    if ( !classInfo || !classInfo->IsKindOf(wxCLASSINFO(ibPGProperty)) )
    {
        ProcessError(wxString::Format(wxS("'%s' is not valid property class"),propClass));
        return nullptr;
    }

    ibPGProperty* property = (ibPGProperty*) classInfo->CreateObject();

    property->SetLabel(propLabel);
    property->DoSetName(propName);

    if ( pChoices && pChoices->IsOk() )
        property->SetChoices(*pChoices);

    m_state->DoInsert(parent, -1, property);

    if ( propValue )
        property->SetValueFromString( *propValue, ibPGPropValFormatFlags::FullValue|
                                                  ibPGPropValFormatFlags::ProgrammaticValue );

    return property;
}

// -----------------------------------------------------------------------

void ibPropertyGridPopulator::AddChildren( ibPGProperty* property )
{
    // Preserve inherited attributes to be able to restore them later:
    // attributes recursively set for the children of this property shouldn't
    // be inherited by its siblings.
    const auto inheritedAttributesOrig = m_inheritedAttributes;

    // Apply inherited attributes to the property.
    for ( const auto& it : m_inheritedAttributes )
    {
        property->SetAttribute(it.first, it.second);
    }

    m_propHierarchy.push_back(property);
    DoScanForChildren();
    m_propHierarchy.pop_back();

    m_inheritedAttributes = std::move(inheritedAttributesOrig);
}

// -----------------------------------------------------------------------

ibPGChoices ibPropertyGridPopulator::ParseChoices( const wxString& choicesString,
                                                   const wxString& idString )
{
    // Using id?
    if ( choicesString[0] == wxT('@') )
    {
        wxString ids = choicesString.substr(1);
        auto it = m_dictIdChoices.find(ids);
        if ( it == m_dictIdChoices.end() )
        {
            ProcessError(wxString::Format(wxS("No choices defined for id '%s'"), ids));
            return ibPGChoices();
        }

        return ibPGChoices(it->second);
    }

    if ( !idString.empty() )
    {
        auto it = m_dictIdChoices.find(idString);
        if ( it != m_dictIdChoices.end() )
        {
            return ibPGChoices(it->second);
        }
    }

    // Parse choices string
    ibPGChoices choices;
    wxString label;
    wxString value;
    int state = 0;
    bool labelValid = false;

    for ( wxUniChar c : choicesString )
    {
        if ( state != 1 )
        {
            if ( c == wxS('"') )
            {
                if ( labelValid )
                {
                    long l;
                    if ( !value.ToLong(&l, 0) ) l = ibPG_INVALID_VALUE;
                    choices.Add(label, l);
                }
                labelValid = false;
                //wxLogDebug(wxS("%s, %s"),label,value);
                value.clear();
                label.clear();
                state = 1;
            }
            else if ( c == wxS('=') )
            {
                if ( labelValid )
                {
                    state = 2;
                }
            }
            else if ( state == 2 && (wxIsalnum(c) || c == wxS('x')) )
            {
                value << c;
            }
        }
        else
        {
            if ( c == wxS('"') )
            {
                state = 0;
                labelValid = true;
            }
            else
                label << c;
        }
    }

    if ( labelValid )
    {
        long l;
        if ( !value.ToLong(&l, 0) ) l = ibPG_INVALID_VALUE;
        choices.Add(label, l);
    }

    if ( !choices.IsOk() )
    {
        choices.EnsureData();
    }

    // Assign to id
    if ( !idString.empty() )
        m_dictIdChoices[idString] = choices.GetData();

    return choices;
}

// -----------------------------------------------------------------------

bool ibPropertyGridPopulator::ToLongPCT( const wxString& s, long* pval, long max )
{
    if ( s.Last() == wxT('%') )
    {
        wxString s2 = s.substr(0,s.length()-1);
        long val;
        if ( s2.ToLong(&val, 10) )
        {
            *pval = (val*max)/100;
            return true;
        }
        return false;
    }

    return s.ToLong(pval, 10);
}

// -----------------------------------------------------------------------

bool ibPropertyGridPopulator::AddAttribute( const wxString& name,
                                            const wxString& type,
                                            const wxString& value,
                                            ibPGPropertyValuesFlags flags )
{
    if ( m_propHierarchy.empty() )
        return false;

    ibPGProperty* p = m_propHierarchy.back();
    wxString valuel = value.Lower();
    ibVariant variant;

    if ( type.empty() )
    {
        long v;

        // Auto-detect type
        if ( valuel == wxS("true") || valuel == wxS("yes") || valuel == wxS("1") )
            variant = true;
        else if ( valuel == wxS("false") || valuel == wxS("no") || valuel == wxS("0") )
            variant = false;
        else if ( value.ToLong(&v, 0) )
            variant = v;
        else
            variant = value;
    }
    else
    {
        if ( type == wxS("string") )
        {
            variant = value;
        }
        else if ( type == wxS("int") )
        {
            long v = 0;
            value.ToLong(&v, 0);
            variant = v;
        }
        else if ( type == wxS("bool") )
        {
            if ( valuel == wxS("true") || valuel == wxS("yes") || valuel == wxS("1") )
                variant = true;
            else
                variant = false;
        }
        else
        {
            ProcessError(wxString::Format(wxS("Invalid attribute type '%s'"),type));
            return false;
        }
    }

    if ( !!(flags & ibPGPropertyValuesFlags::Recurse) )
    {
        m_inheritedAttributes[name] = variant;
    }

    p->SetAttribute( name, variant );

    return true;
}

// -----------------------------------------------------------------------

void ibPropertyGridPopulator::ProcessError( const wxString& msg )
{
    wxLogError(_("Error in resource: %s"),msg);
}

// -----------------------------------------------------------------------

#endif  // wxUSE_PROPGRID
