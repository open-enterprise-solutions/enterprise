// FORKED from wxWidgets' wxPropertyGrid (wx 3.3.4, include/wx/propgrid/editors.h): wx -> ib prefixes
// applied mechanically to the names it defines (docs/private/wx-fork.md).

/////////////////////////////////////////////////////////////////////////////
// Name:        wx/propgrid/editors.h
// Purpose:     ibPropertyGrid editors
// Author:      Jaakko Salli
// Created:     2007-04-14
// Copyright:   (c) Jaakko Salli
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _DESKGUI_UNIV_PROPGRID_EDITORS_H_
#define _DESKGUI_UNIV_PROPGRID_EDITORS_H_

#include "wx/defs.h"

#if wxUSE_PROPGRID

#include "wx/window.h"

#include "deskgui/univ/window.h"   // ibWindow — the buttons' strip (ibPGMultiButton)
#include "deskgui/univ/propgrid/propgriddefs.h"

#include <vector>

class WXDLLIMPEXP_FWD_CORE wxBitmapBundle;
class DESKGUI_API ibPGCell;
class DESKGUI_API ibPGProperty;
class DESKGUI_API ibPropertyGrid;

// -----------------------------------------------------------------------
// ibPGWindowList contains list of editor windows returned by CreateControls.

class ibPGWindowList
{
public:
    ibPGWindowList(wxWindow* primary, wxWindow* secondary = nullptr)
        : m_primary(primary)
        , m_secondary(secondary)
    {
    }

    void SetSecondary(wxWindow* secondary) { m_secondary = secondary; }

    wxWindow* GetPrimary() const { return m_primary; }
    wxWindow* GetSecondary() const { return m_secondary; }

    wxWindow*   m_primary;
    wxWindow*   m_secondary;
};

// -----------------------------------------------------------------------

// Base class for custom ibPropertyGrid editors.
// - Names of builtin property editors are: TextCtrl, Choice,
//   ComboBox, CheckBox, TextCtrlAndButton, and ChoiceAndButton. Additional
//   editors include SpinCtrl and DatePickerCtrl, but using them requires
//   calling ibPropertyGrid::RegisterAdditionalEditors() prior use.
// - Pointer to builtin editor is available as ibPGEditor_EditorName
//   (e.g. ibPGEditor_TextCtrl).
// - To add new editor you need to register it first using static function
//   ibPropertyGrid::RegisterEditorClass(), with code like this:
//      ibPGEditor *editorPointer = ibPropertyGrid::RegisterEditorClass(
//                                     new MyEditorClass(), "MyEditor");
//   After that, ibPropertyGrid will take ownership of the given object, but
//   you should still store editorPointer somewhere, so you can pass it to
//   ibPGProperty::SetEditor(), or return it from
//   ibPGEditor::DoGetEditorClass().
class DESKGUI_API ibPGEditor : public wxObject
{
    wxDECLARE_ABSTRACT_CLASS(ibPGEditor);
public:

    // Constructor.
    ibPGEditor()
        : wxObject()
        , m_clientData(nullptr)
    {
    }

    // Destructor.
    virtual ~ibPGEditor() = default;

    // Returns pointer to the name of the editor. For example,
    // ibPGEditor_TextCtrl has name "TextCtrl". If you don't need to access
    // your custom editor by string name, then you do not need to implement
    // this function.
    virtual wxString GetName() const;

    // Instantiates editor controls.
    //  propgrid- ibPropertyGrid to which the property belongs
    //    (use as parent for control).
    // property - Property for which this method is called.
    // pos - Position, inside ibPropertyGrid, to create control(s) to.
    // size - Initial size for control(s).
    // Unlike in previous version of ibPropertyGrid, it is no longer
    // necessary to call wxEvtHandler::Connect() for interesting editor
    // events. Instead, all events from control are now automatically
    // forwarded to ibPGEditor::OnEvent() and ibPGProperty::OnEvent().
    virtual ibPGWindowList CreateControls(ibPropertyGrid* propgrid,
                                          ibPGProperty* property,
                                          const wxPoint& pos,
                                          const wxSize& size) const = 0;

    // Loads value from property to the control.
    virtual void UpdateControl( ibPGProperty* property,
                                wxWindow* ctrl ) const = 0;

    // Used to get the renderer to draw the value with when the control is
    // hidden.
    // Default implementation returns g_wxPGDefaultRenderer.
    //virtual ibPGCellRenderer* GetCellRenderer() const;

    // Draws value for given property.
    virtual void DrawValue( wxDC& dc,
                            const wxRect& rect,
                            ibPGProperty* property,
                            const wxString& text ) const;

    // Handles events. Returns true if value in control was modified
    // (see ibPGProperty::OnEvent for more information).
    // ibPropertyGrid will automatically unfocus the editor when
    // wxEVT_TEXT_ENTER is received and when it results in
    // property value being modified. This happens regardless of
    // editor type (i.e. behaviour is same for any ibTextCtrl and
    // wxComboBox based editor).
    virtual bool OnEvent( ibPropertyGrid* propgrid, ibPGProperty* property,
        wxWindow* wnd_primary, wxEvent& event ) const = 0;

    // Returns value from control, via parameter 'variant'.
    // Usually ends up calling property's StringToValue or IntToValue.
    // Returns true if value was different.
    virtual bool GetValueFromControl( ibVariant& variant,
                                      ibPGProperty* property,
                                      wxWindow* ctrl ) const;

    // Sets new appearance for the control. Default implementation
    // sets foreground colour, background colour, font, plus text
    // for ibTextCtrl and ibComboCtrl.
    // appearance - New appearance to be applied.
    // oldAppearance - Previously applied appearance. Used to detect
    //   which control attributes need to be changed (e.g. so we only
    //   change background colour if really needed).
    // unspecified - true if the new appearance represents an unspecified
    // property value.
    virtual void SetControlAppearance( ibPropertyGrid* pg,
                                       ibPGProperty* property,
                                       wxWindow* ctrl,
                                       const ibPGCell& appearance,
                                       const ibPGCell& oldAppearance,
                                       bool unspecified ) const;

    // Sets value in control to unspecified.
    virtual void SetValueToUnspecified( ibPGProperty* property,
                                        wxWindow* ctrl ) const;

    // Sets control's value specifically from string.
    virtual void SetControlStringValue( ibPGProperty* property,
                                        wxWindow* ctrl,
                                        const wxString& txt ) const;

    // Sets control's value specifically from int (applies to choice etc.).
    virtual void SetControlIntValue( ibPGProperty* property,
                                     wxWindow* ctrl,
                                     int value ) const;

    // Inserts item to existing control. Index -1 means appending.
    // Default implementation does nothing. Returns index of item added.
    virtual int InsertItem( wxWindow* ctrl,
                            const wxString& label,
                            int index ) const;

    // Deletes item from existing control.
    // Default implementation does nothing.
    virtual void DeleteItem( wxWindow* ctrl, int index ) const;

    // Sets items of existing control.
    // Default implementation does nothing.
    virtual void SetItems(wxWindow* ctrl,  const wxArrayString& labels) const;

    // Extra processing when control gains focus. For example, ibTextCtrl
    // based controls should select all text.
    virtual void OnFocus( ibPGProperty* property, wxWindow* wnd ) const;

    // Returns true if control itself can contain the custom image. Default is
    // to return false.
    virtual bool CanContainCustomImage() const;

    //
    // This member is public so scripting language bindings
    // wrapper code can access it freely.
    void*       m_clientData;
};


#define IB_PG_IMPLEMENT_INTERNAL_EDITOR_CLASS(EDITOR,CLASSNAME,BASECLASS) \
wxIMPLEMENT_DYNAMIC_CLASS(CLASSNAME, BASECLASS); \
wxString CLASSNAME::GetName() const \
{ \
    return wxS(#EDITOR); \
} \
ibPGEditor* ibPGEditor_##EDITOR = nullptr;


//
// Following are the built-in editor classes.
//

class DESKGUI_API ibPGTextCtrlEditor : public ibPGEditor
{
    wxDECLARE_DYNAMIC_CLASS(ibPGTextCtrlEditor);
public:
    ibPGTextCtrlEditor() = default;
    virtual ~ibPGTextCtrlEditor();

    virtual ibPGWindowList CreateControls(ibPropertyGrid* propgrid,
                                          ibPGProperty* property,
                                          const wxPoint& pos,
                                          const wxSize& size) const override;
    virtual void UpdateControl( ibPGProperty* property,
                                wxWindow* ctrl ) const override;
    virtual bool OnEvent( ibPropertyGrid* propgrid,
                          ibPGProperty* property,
                          wxWindow* primaryCtrl,
                          wxEvent& event ) const override;
    virtual bool GetValueFromControl( ibVariant& variant,
                                      ibPGProperty* property,
                                      wxWindow* ctrl ) const override;

    virtual wxString GetName() const override;

    //virtual ibPGCellRenderer* GetCellRenderer() const;
    virtual void SetControlStringValue( ibPGProperty* property,
                                        wxWindow* ctrl,
                                        const wxString& txt ) const override;
    virtual void OnFocus( ibPGProperty* property, wxWindow* wnd ) const override;

    // Provided so that, for example, ComboBox editor can use the same code
    // (multiple inheritance would get way too messy).
    static bool OnTextCtrlEvent( ibPropertyGrid* propgrid,
                                 ibPGProperty* property,
                                 wxWindow* ctrl,
                                 wxEvent& event );

    static bool GetTextCtrlValueFromControl( ibVariant& variant,
                                             ibPGProperty* property,
                                             wxWindow* ctrl );

};


class DESKGUI_API ibPGChoiceEditor : public ibPGEditor
{
    wxDECLARE_DYNAMIC_CLASS(ibPGChoiceEditor);
public:
    ibPGChoiceEditor() = default;
    virtual ~ibPGChoiceEditor();

    virtual ibPGWindowList CreateControls(ibPropertyGrid* propgrid,
                                          ibPGProperty* property,
                                          const wxPoint& pos,
                                          const wxSize& size) const override;
    virtual void UpdateControl( ibPGProperty* property,
                                wxWindow* ctrl ) const override;
    virtual bool OnEvent( ibPropertyGrid* propgrid,
                          ibPGProperty* property,
                          wxWindow* primaryCtrl,
                          wxEvent& event ) const override;
    virtual bool GetValueFromControl( ibVariant& variant,
                                      ibPGProperty* property,
                                      wxWindow* ctrl ) const override;
    virtual void SetValueToUnspecified( ibPGProperty* property,
                                        wxWindow* ctrl ) const override;
    virtual wxString GetName() const override;

    virtual void SetControlIntValue( ibPGProperty* property,
                                     wxWindow* ctrl,
                                     int value ) const override;
    virtual void SetControlStringValue( ibPGProperty* property,
                                        wxWindow* ctrl,
                                        const wxString& txt ) const override;

    virtual int InsertItem( wxWindow* ctrl,
                            const wxString& label,
                            int index ) const override;
    virtual void DeleteItem( wxWindow* ctrl, int index ) const override;
    virtual void SetItems(wxWindow* ctrl, const wxArrayString& labels) const override;

    virtual bool CanContainCustomImage() const override;

    // CreateControls calls this with CB_READONLY in extraStyle
    wxWindow* CreateControlsBase( ibPropertyGrid* propgrid,
                                  ibPGProperty* property,
                                  const wxPoint& pos,
                                  const wxSize& sz,
                                  long extraStyle ) const;

};


class DESKGUI_API ibPGComboBoxEditor : public ibPGChoiceEditor
{
    wxDECLARE_DYNAMIC_CLASS(ibPGComboBoxEditor);
public:
    ibPGComboBoxEditor() = default;
    virtual ~ibPGComboBoxEditor();

    virtual ibPGWindowList CreateControls(ibPropertyGrid* propgrid,
                                          ibPGProperty* property,
                                          const wxPoint& pos,
                                          const wxSize& size) const override;

    virtual wxString GetName() const override;

    virtual void UpdateControl( ibPGProperty* property, wxWindow* ctrl ) const override;

    virtual bool OnEvent( ibPropertyGrid* propgrid, ibPGProperty* property,
        wxWindow* ctrl, wxEvent& event ) const override;

    virtual bool GetValueFromControl( ibVariant& variant,
                                      ibPGProperty* property,
                                      wxWindow* ctrl ) const override;

    virtual void OnFocus( ibPGProperty* property, wxWindow* wnd ) const override;

};


class DESKGUI_API ibPGChoiceAndButtonEditor : public ibPGChoiceEditor
{
public:
    ibPGChoiceAndButtonEditor() = default;
    virtual ~ibPGChoiceAndButtonEditor();
    virtual wxString GetName() const override;

    virtual ibPGWindowList CreateControls(ibPropertyGrid* propgrid,
                                          ibPGProperty* property,
                                          const wxPoint& pos,
                                          const wxSize& size) const override;

    wxDECLARE_DYNAMIC_CLASS(ibPGChoiceAndButtonEditor);
};

class DESKGUI_API
ibPGTextCtrlAndButtonEditor : public ibPGTextCtrlEditor
{
public:
    ibPGTextCtrlAndButtonEditor() = default;
    virtual ~ibPGTextCtrlAndButtonEditor();
    virtual wxString GetName() const override;

    virtual ibPGWindowList CreateControls(ibPropertyGrid* propgrid,
                                          ibPGProperty* property,
                                          const wxPoint& pos,
                                          const wxSize& size) const override;

    wxDECLARE_DYNAMIC_CLASS(ibPGTextCtrlAndButtonEditor);
};


#if ibPG_INCLUDE_CHECKBOX

//
// Use custom check box code instead of native control
// for cleaner (i.e. more integrated) look.
//
class DESKGUI_API ibPGCheckBoxEditor : public ibPGEditor
{
    wxDECLARE_DYNAMIC_CLASS(ibPGCheckBoxEditor);
public:
    ibPGCheckBoxEditor() = default;
    virtual ~ibPGCheckBoxEditor();

    virtual wxString GetName() const override;
    virtual ibPGWindowList CreateControls(ibPropertyGrid* propgrid,
                                          ibPGProperty* property,
                                          const wxPoint& pos,
                                          const wxSize& size) const override;
    virtual void UpdateControl( ibPGProperty* property,
                                wxWindow* ctrl ) const override;
    virtual bool OnEvent( ibPropertyGrid* propgrid,
                          ibPGProperty* property,
                          wxWindow* primaryCtrl,
                          wxEvent& event ) const override;
    virtual bool GetValueFromControl( ibVariant& variant,
                                      ibPGProperty* property,
                                      wxWindow* ctrl ) const override;
    virtual void SetValueToUnspecified( ibPGProperty* property,
                                        wxWindow* ctrl ) const override;

    virtual void DrawValue( wxDC& dc,
                            const wxRect& rect,
                            ibPGProperty* property,
                            const wxString& text ) const override;
    //virtual ibPGCellRenderer* GetCellRenderer() const;

    virtual void SetControlIntValue( ibPGProperty* property,
                                     wxWindow* ctrl,
                                     int value ) const override;
};

#endif


// -----------------------------------------------------------------------
// Editor class registration macro (mostly for internal use)

#define ibPGRegisterEditorClass(EDITOR) \
    if ( ibPGEditor_##EDITOR == nullptr ) \
    { \
        ibPGEditor_##EDITOR = ibPropertyGrid::RegisterEditorClass( \
                new ibPG##EDITOR##Editor ); \
    }

// -----------------------------------------------------------------------

// Derive a class from this to adapt an existing editor dialog or function to
// be used when editor button of a property is pushed.
// You only need to derive class and implement DoShowDialog() to create and
// show the dialog, and finally submit the value returned by the dialog
// via SetValue().
class DESKGUI_API ibPGEditorDialogAdapter : public wxObject
{
    wxDECLARE_ABSTRACT_CLASS(ibPGEditorDialogAdapter);
public:
    ibPGEditorDialogAdapter()
        : wxObject()
        , m_clientData(nullptr)
    {
    }

    virtual ~ibPGEditorDialogAdapter() = default;

    bool ShowDialog( ibPropertyGrid* propGrid, ibPGProperty* property );

    virtual bool DoShowDialog( ibPropertyGrid* propGrid,
                               ibPGProperty* property ) = 0;

    void SetValue( const ibVariant& value )
    {
        m_value = value;
    }

    // This method is typically only used if deriving class from existing
    // adapter with value conversion purposes.
    ibVariant& GetValue() { return m_value; }

    // This member is public so scripting language bindings
    // wrapper code can access it freely.
    void*               m_clientData;

private:
    ibVariant           m_value;
};

// -----------------------------------------------------------------------


// This class can be used to have multiple buttons in a property editor.
// You will need to create a new property editor class, override
// CreateControls, and have it return ibPGMultiButton instance in
// ibPGWindowList::SetSecondary().
class DESKGUI_API ibPGMultiButton : public ibWindow
{
public:
    ibPGMultiButton( ibPropertyGrid* pg, const wxSize& sz );
    virtual ~ibPGMultiButton() = default;

    wxWindow* GetButton( unsigned int i ) { return m_buttons[i]; }
    const wxWindow* GetButton( unsigned int i ) const
        { return m_buttons[i]; }

    // Utility function to be used in event handlers.
    int GetButtonId( unsigned int i ) const { return GetButton(i)->GetId(); }

    // Returns number of buttons.
    unsigned int GetCount() const { return (unsigned int) m_buttons.size(); }

    void Add( const wxString& label, int id = wxID_ANY );
#if wxUSE_BMPBUTTON
    void Add( const wxBitmapBundle& bitmap, int id = wxID_ANY );
#endif

    wxSize GetPrimarySize() const
    {
        return wxSize(m_fullEditorSize.x - m_buttonsWidth, m_fullEditorSize.y);
    }

    void Finalize( ibPropertyGrid* propGrid, const wxPoint& pos );

protected:

    void DoAddButton( wxWindow* button, const wxSize& sz );

    std::vector<wxWindow*> m_buttons;
    wxSize          m_fullEditorSize;
    int             m_buttonsWidth;
};

// -----------------------------------------------------------------------

#endif // wxUSE_PROPGRID

#endif // _DESKGUI_UNIV_PROPGRID_EDITORS_H_
