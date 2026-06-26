///////////////////////////////////////////////////////////////////////////////
// Name:        wx/dvrenderers.h
// Purpose:     Declare all ibDataViewCtrl classes
// Author:      Robert Roebling, Vadim Zeitlin
// Created:     2009-11-08 (extracted from wx/dataview.h)
// Copyright:   (c) 2006 Robert Roebling
//              (c) 2009 Vadim Zeitlin <vadim@wxwidgets.org>
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

#ifndef _WX_DVRENDERERS_H_
#define _WX_DVRENDERERS_H_

/*
	Note about the structure of various headers: they're organized in a more
	complicated way than usual because of the various dependencies which are
	different for different ports. In any case the only public header, i.e. the
	one which can be included directly is wx/dataview.h. It, in turn, includes
	this one to define all the renderer classes.

	We define the base ibDataViewRendererBase class first and then include a
	port-dependent wx/xxx/dvrenderer.h which defines ibDataViewRenderer itself.
	After this we can define ibDataViewRendererCustomBase (and maybe in the
	future base classes for other renderers if the need arises, i.e. if there
	is any non-trivial code or API which it makes sense to keep in common code)
	and include wx/xxx/dvrenderers.h (notice the plural) which defines all the
	rest of the renderer classes.
 */

class FRONTEND_API ibDataViewCustomRenderer;

// ----------------------------------------------------------------------------
// ibDataViewIconText: helper class used by ibDataViewIconTextRenderer
// ----------------------------------------------------------------------------

class FRONTEND_API ibDataViewIconText : public wxObject
{
public:
	ibDataViewIconText(const wxString& text = wxEmptyString,
		const wxBitmapBundle& bitmap = wxBitmapBundle())
		: m_text(text),
		m_bitmap(bitmap)
	{
	}

	void SetText(const wxString& text) { m_text = text; }
	wxString GetText() const { return m_text; }

	void SetBitmapBundle(const wxBitmapBundle& bitmap) { m_bitmap = bitmap; }
	const wxBitmapBundle& GetBitmapBundle() const { return m_bitmap; }

	// These methods exist for compatibility, prefer using the methods above.
	void SetIcon(const wxIcon& icon) { m_bitmap = wxBitmapBundle(icon); }
	wxIcon GetIcon() const { return m_bitmap.GetIcon(wxDefaultSize); }

	bool IsSameAs(const ibDataViewIconText& other) const
	{
		return m_text == other.m_text && m_bitmap.IsSameAs(other.m_bitmap);
	}

	bool operator==(const ibDataViewIconText& other) const
	{
		return IsSameAs(other);
	}

	bool operator!=(const ibDataViewIconText& other) const
	{
		return !IsSameAs(other);
	}

private:
	wxString    m_text;
	wxBitmapBundle m_bitmap;

	wxDECLARE_DYNAMIC_CLASS(ibDataViewIconText);
};

DECLARE_VARIANT_OBJECT_EXPORTED(ibDataViewIconText, FRONTEND_API)

// ----------------------------------------------------------------------------
// ibDataViewCheckIconText: value class used by ibDataViewCheckIconTextRenderer
// ----------------------------------------------------------------------------

class FRONTEND_API ibDataViewCheckIconText : public ibDataViewIconText
{
public:
	ibDataViewCheckIconText(const wxString& text = wxString(),
		const wxBitmapBundle& icon = wxBitmapBundle(),
		wxCheckBoxState checkedState = wxCHK_UNDETERMINED)
		: ibDataViewIconText(text, icon),
		m_checkedState(checkedState)
	{
	}

	wxCheckBoxState GetCheckedState() const { return m_checkedState; }
	void SetCheckedState(wxCheckBoxState state) { m_checkedState = state; }

private:
	wxCheckBoxState m_checkedState;

	wxDECLARE_DYNAMIC_CLASS(ibDataViewCheckIconText);
};

DECLARE_VARIANT_OBJECT_EXPORTED(ibDataViewCheckIconText, FRONTEND_API)

// ----------------------------------------------------------------------------
// ibDataViewRendererBase
// ----------------------------------------------------------------------------

enum ibDataViewCellMode
{
	wxDATAVIEW_CELL_INERT,
	wxDATAVIEW_CELL_ACTIVATABLE,
	wxDATAVIEW_CELL_EDITABLE
};

enum ibDataViewCellRenderState
{
	wxDATAVIEW_CELL_SELECTED = 1,
	wxDATAVIEW_CELL_PRELIT = 2,
	wxDATAVIEW_CELL_INSENSITIVE = 4,
	wxDATAVIEW_CELL_FOCUSED = 8
};

// helper for fine-tuning rendering of values depending on row's state
class FRONTEND_API ibDataViewValueAdjuster
{
public:
	virtual ~ibDataViewValueAdjuster() {}

	// changes the value to have appearance suitable for highlighted rows
	virtual wxVariant MakeHighlighted(const wxVariant& value) const { return value; }
};

class FRONTEND_API ibDataViewRendererBase : public wxObject
{
public:
	ibDataViewRendererBase(const wxString& varianttype,
		ibDataViewCellMode mode = wxDATAVIEW_CELL_INERT,
		int alignment = wxDVR_DEFAULT_ALIGNMENT);
	virtual ~ibDataViewRendererBase();

	virtual bool Validate(wxVariant& WXUNUSED(value))
	{
		return true;
	}

	void SetOwner(ibDataViewColumn* owner) { m_owner = owner; }
	ibDataViewColumn* GetOwner() const { return m_owner; }

	// renderer value and attributes: SetValue() and SetAttr() are called
	// before a cell is rendered using this renderer
	virtual bool SetValue(const wxVariant& value) = 0;
	virtual bool GetValue(wxVariant& value) const = 0;
#if wxUSE_ACCESSIBILITY
	virtual wxString GetAccessibleDescription() const = 0;
#endif // wxUSE_ACCESSIBILITY

	wxString GetVariantType() const { return m_variantType; }

	// Check if the given variant type is compatible with the type expected by
	// this renderer: by default, just compare it with GetVariantType(), but
	// can be overridden to accept other types that can be converted to the
	// type needed by the renderer.
	virtual bool IsCompatibleVariantType(const wxString& variantType) const
	{
		return variantType == GetVariantType();
	}

	// Prepare for rendering the value of the corresponding item in the given
	// column taken from the provided non-null model.
	//
	// Notice that the column must be the same as GetOwner()->GetModelColumn(),
	// it is only passed to this method because the existing code already has
	// it and should probably be removed in the future.
	//
	// Return true if this cell is non-empty or false otherwise (and also if
	// the model returned a value of the wrong type, i.e. such that our
	// IsCompatibleVariantType() returned false for it, in which case a debug
	// error is also logged).
	bool PrepareForItem(const ibDataViewModel* model,
		const ibDataViewItem& item,
		unsigned column);

	// renderer properties:
	virtual void SetMode(ibDataViewCellMode mode) = 0;
	virtual ibDataViewCellMode GetMode() const = 0;

	// NOTE: Set/GetAlignment do not take/return a wxAlignment enum but
	//       rather an "int"; that's because for rendering cells it's allowed
	//       to combine alignment flags (e.g. wxALIGN_LEFT|wxALIGN_BOTTOM)
	virtual void SetAlignment(int align) = 0;
	virtual int GetAlignment() const = 0;

	// enable or disable (if called with wxELLIPSIZE_NONE) replacing parts of
	// the item text (hence this only makes sense for renderers showing
	// text...) with ellipsis in order to make it fit the column width
	virtual void EnableEllipsize(wxEllipsizeMode mode = wxELLIPSIZE_MIDDLE) = 0;
	void DisableEllipsize() { EnableEllipsize(wxELLIPSIZE_NONE); }

	virtual wxEllipsizeMode GetEllipsizeMode() const = 0;

	// in-place editing
	virtual bool HasEditorCtrl() const
	{
		return false;
	}
	virtual wxWindow* CreateEditorCtrl(wxWindow* WXUNUSED(parent),
		wxRect WXUNUSED(labelRect),
		const wxVariant& WXUNUSED(value))
	{
		return NULL;
	}
	virtual bool GetValueFromEditorCtrl(wxWindow* WXUNUSED(editor),
		wxVariant& WXUNUSED(value))
	{
		return false;
	}

	virtual bool StartEditing(const ibDataViewItem& item, wxRect labelRect);
	virtual void CancelEditing();
	virtual bool FinishEditing();

	wxWindow* GetEditorCtrl() const { return m_editorCtrl; }

	virtual bool IsCustomRenderer() const { return false; }


	// Implementation only from now on.

	// Return the alignment of this renderer if it's specified (i.e. has value
	// different from the default wxDVR_DEFAULT_ALIGNMENT) or the alignment of
	// the column it is used for otherwise.
	//
	// Unlike GetAlignment(), this always returns a valid combination of
	// wxALIGN_XXX flags (although possibly wxALIGN_NOT) and never returns
	// wxDVR_DEFAULT_ALIGNMENT.
	int GetEffectiveAlignment() const;

	// Like GetEffectiveAlignment(), but returns wxDVR_DEFAULT_ALIGNMENT if
	// the owner isn't set and GetAlignment() is default.
	int GetEffectiveAlignmentIfKnown() const;

	// Send wxEVT_DATAVIEW_ITEM_EDITING_STARTED event.
	void NotifyEditingStarted(const ibDataViewItem& item);

	// Sets the transformer for fine-tuning rendering of values depending on row's state
	void SetValueAdjuster(ibDataViewValueAdjuster* transformer)
	{
		delete m_valueAdjuster; m_valueAdjuster = transformer;
	}

protected:
	// These methods are called from PrepareForItem() and should do whatever is
	// needed for the current platform to ensure that the item is rendered
	// using the given attributes and enabled/disabled state.
	virtual void SetAttr(const ibDataViewItemAttr& attr) = 0;
	virtual void SetEnabled(bool enabled) = 0;

	// Return whether the currently rendered item is on a highlighted row
	// (typically selection with dark background). For internal use only.
	virtual bool IsHighlighted() const = 0;

	// Helper of PrepareForItem() also used in StartEditing(): returns the
	// value checking that its type matches our GetVariantType().
	//
	// VIRTUAL (fork power): the per-cell value fetch is the single front-side choke point that
	// still carries (model, item, column). A renderer overrides it to resolve a dot-path column
	// THROUGH the column's own binding — first hop via the (dumb) model, deeper hops on the front —
	// without the model knowing about paths and without touching the provider / notifier.
	virtual wxVariant CheckedGetValue(const ibDataViewModel* model,
		const ibDataViewItem& item,
		unsigned column) const;

	// Validates the given value (if it is non-null) and sends (in any case)
	// ITEM_EDITING_DONE event and, finally, updates the model with the value
	// (f it is valid, of course) if the event wasn't vetoed.
	bool DoHandleEditingDone(wxVariant* value);


	wxString                m_variantType;
	ibDataViewColumn* m_owner;
	wxWeakRef<wxWindow>     m_editorCtrl;
	ibDataViewItem          m_item; // Item being currently edited, if valid.

	ibDataViewValueAdjuster* m_valueAdjuster;

	// internal utility, may be used anywhere the window associated with the
	// renderer is required
	ibDataViewCtrl* GetView() const;

private:
	// Called from {Called,Finish}Editing() and dtor to cleanup m_editorCtrl
	void DestroyEditControl();

	wxDECLARE_DYNAMIC_CLASS_NO_COPY(ibDataViewRendererBase);
};

// in the generic implementation there is no real ibDataViewRenderer, all
// renderers are custom so it's the same as ibDataViewCustomRenderer and
// ibDataViewCustomRendererBase derives from ibDataViewRendererBase directly
//
// this is a rather ugly hack but unfortunately it just doesn't seem to be
// possible to have the same class hierarchy in all ports and avoid
// duplicating the entire ibDataViewCustomRendererBase in the generic
// ibDataViewRenderer class (well, we could use a mix-in but this would
// make classes hierarchy non linear and arguably even more complex)
#define ibDataViewCustomRendererRealBase ibDataViewRendererBase

// ----------------------------------------------------------------------------
// ibDataViewCustomRendererBase
// ----------------------------------------------------------------------------

class FRONTEND_API ibDataViewCustomRendererBase
	: public ibDataViewCustomRendererRealBase
{
public:
	// Constructor must specify the usual renderer parameters which we simply
	// pass to the base class
	ibDataViewCustomRendererBase(const wxString& varianttype = wxASCII_STR("string"),
		ibDataViewCellMode mode = wxDATAVIEW_CELL_INERT,
		int align = wxDVR_DEFAULT_ALIGNMENT)
		: ibDataViewCustomRendererRealBase(varianttype, mode, align)
	{
	}


	// Render the item using the current value (returned by GetValue()).
	virtual bool Render(wxRect cell, wxDC* dc, int state) = 0;

	// Return the size of the item appropriate to its current value.
	virtual wxSize GetSize() const = 0;

	// Define virtual function which are called when a key is pressed on the
	// item, clicked or the user starts to drag it: by default they all simply
	// return false indicating that the events are not handled

	virtual bool ActivateCell(const wxRect& cell,
		ibDataViewModel* model,
		const ibDataViewItem& item,
		unsigned int col,
		const wxMouseEvent* mouseEvent);

	virtual bool StartDrag(const wxPoint& WXUNUSED(cursor),
		const wxRect& WXUNUSED(cell),
		ibDataViewModel* WXUNUSED(model),
		const ibDataViewItem& WXUNUSED(item),
		unsigned int WXUNUSED(col))
	{
		return false;
	}

	// Helper which can be used by Render() implementation in the derived
	// classes: it will draw the text in the same manner as the standard
	// renderers do.
	virtual void RenderText(const wxString& text,
		int xoffset,
		wxRect cell,
		wxDC* dc,
		int state);


	// Override the base class virtual method to simply store the attribute so
	// that it can be accessed using GetAttr() from Render() if needed.
	virtual void SetAttr(const ibDataViewItemAttr& attr) wxOVERRIDE { m_attr = attr; }
	const ibDataViewItemAttr& GetAttr() const { return m_attr; }

	// Store the enabled state of the item so that it can be accessed from
	// Render() via GetEnabled() if needed.
	virtual void SetEnabled(bool enabled) wxOVERRIDE;
	bool GetEnabled() const { return m_enabled; }


	// Implementation only from now on

	// Retrieve the DC to use for drawing. This is implemented in derived
	// platform-specific classes.
	virtual wxDC* GetDC() = 0;

	// To draw background use the background colour in ibDataViewItemAttr
	virtual void RenderBackground(wxDC* dc, const wxRect& rect);

	// Prepare DC to use attributes and call Render().
	void WXCallRender(wxRect rect, wxDC* dc, int state);

	virtual bool IsCustomRenderer() const wxOVERRIDE { return true; }

protected:
	// helper for GetSize() implementations, respects attributes
	wxSize GetTextExtent(const wxString& str) const;

private:
	ibDataViewItemAttr m_attr;
	bool m_enabled;

	wxDECLARE_NO_COPY_CLASS(ibDataViewCustomRendererBase);
};

// ----------------------------------------------------------------------------
// wxDataViewRenderer
// ----------------------------------------------------------------------------

class FRONTEND_API ibDataViewRenderer : public ibDataViewCustomRendererBase
{
public:
	ibDataViewRenderer(const wxString& varianttype,
		ibDataViewCellMode mode = wxDATAVIEW_CELL_INERT,
		int align = wxDVR_DEFAULT_ALIGNMENT);
	virtual ~ibDataViewRenderer();

	virtual wxDC* GetDC() wxOVERRIDE;

	virtual void SetAlignment(int align) wxOVERRIDE;
	virtual int GetAlignment() const wxOVERRIDE;

	virtual void EnableEllipsize(wxEllipsizeMode mode = wxELLIPSIZE_MIDDLE) wxOVERRIDE
	{
		m_ellipsizeMode = mode;
	}
	virtual wxEllipsizeMode GetEllipsizeMode() const wxOVERRIDE
	{
		return m_ellipsizeMode;
	}

	virtual void SetMode(ibDataViewCellMode mode) wxOVERRIDE
	{
		m_mode = mode;
	}
	virtual ibDataViewCellMode GetMode() const wxOVERRIDE
	{
		return m_mode;
	}

	// implementation

	// This callback is used by generic implementation of wxDVC itself.  It's
	// different from the corresponding ActivateCell() method which should only
	// be overridable for the custom renderers while the generic implementation
	// uses this one for all of them, including the standard ones.

	virtual bool WXActivateCell(const wxRect& WXUNUSED(cell),
		ibDataViewModel* WXUNUSED(model),
		const ibDataViewItem& WXUNUSED(item),
		unsigned int WXUNUSED(col),
		const wxMouseEvent* WXUNUSED(mouseEvent))
	{
		return false;
	}

	void SetState(int state) { m_state = state; }

protected:
	virtual bool IsHighlighted() const wxOVERRIDE
	{
		return m_state & wxDATAVIEW_CELL_SELECTED;
	}

private:
	int                          m_align;
	ibDataViewCellMode           m_mode;

	wxEllipsizeMode m_ellipsizeMode;

	wxDC* m_dc;

	int m_state;

	wxDECLARE_DYNAMIC_CLASS_NO_COPY(ibDataViewRenderer);
};

// ---------------------------------------------------------
// ibDataViewCustomRenderer
// ---------------------------------------------------------

class FRONTEND_API ibDataViewCustomRenderer : public ibDataViewRenderer
{
public:
	static wxString GetDefaultType() { return wxS("string"); }

	ibDataViewCustomRenderer(const wxString& varianttype = GetDefaultType(),
		ibDataViewCellMode mode = wxDATAVIEW_CELL_INERT,
		int align = wxDVR_DEFAULT_ALIGNMENT);


	// see the explanation of the following WXOnXXX() methods in wx/generic/dvrenderer.h

	virtual bool WXActivateCell(const wxRect& cell,
		ibDataViewModel* model,
		const ibDataViewItem& item,
		unsigned int col,
		const wxMouseEvent* mouseEvent) wxOVERRIDE
	{
		return ActivateCell(cell, model, item, col, mouseEvent);
	}

#if wxUSE_ACCESSIBILITY
	virtual wxString GetAccessibleDescription() const wxOVERRIDE;
#endif // wxUSE_ACCESSIBILITY

private:
	wxDECLARE_DYNAMIC_CLASS_NO_COPY(ibDataViewCustomRenderer);
};


// ---------------------------------------------------------
// ibDataViewTextRenderer
// ---------------------------------------------------------

class FRONTEND_API ibDataViewTextRenderer : public ibDataViewRenderer
{
public:
	static wxString GetDefaultType() { return wxS("string"); }

	ibDataViewTextRenderer(const wxString& varianttype = GetDefaultType(),
		ibDataViewCellMode mode = wxDATAVIEW_CELL_INERT,
		int align = wxDVR_DEFAULT_ALIGNMENT);
	virtual ~ibDataViewTextRenderer();

#if wxUSE_MARKUP
	void EnableMarkup(bool enable = true);
#endif // wxUSE_MARKUP

	virtual bool SetValue(const wxVariant& value) wxOVERRIDE;
	virtual bool GetValue(wxVariant& value) const wxOVERRIDE;
#if wxUSE_ACCESSIBILITY
	virtual wxString GetAccessibleDescription() const wxOVERRIDE;
#endif // wxUSE_ACCESSIBILITY

	virtual bool Render(wxRect cell, wxDC* dc, int state) wxOVERRIDE;
	virtual wxSize GetSize() const wxOVERRIDE;

	// in-place editing
	virtual bool HasEditorCtrl() const wxOVERRIDE;
	virtual wxWindow* CreateEditorCtrl(wxWindow* parent, wxRect labelRect,
		const wxVariant& value) wxOVERRIDE;
	virtual bool GetValueFromEditorCtrl(wxWindow* editor, wxVariant& value) wxOVERRIDE;

protected:
	wxString   m_text;

private:
#if wxUSE_MARKUP
	class wxItemMarkupText* m_markupText;
#endif // wxUSE_MARKUP

	wxDECLARE_DYNAMIC_CLASS_NO_COPY(ibDataViewTextRenderer);
};

// ---------------------------------------------------------
// ibDataViewBitmapRenderer
// ---------------------------------------------------------

class FRONTEND_API ibDataViewBitmapRenderer : public ibDataViewRenderer
{
public:
	static wxString GetDefaultType() { return wxS("wxBitmapBundle"); }

	ibDataViewBitmapRenderer(const wxString& varianttype = GetDefaultType(),
		ibDataViewCellMode mode = wxDATAVIEW_CELL_INERT,
		int align = wxDVR_DEFAULT_ALIGNMENT);

	virtual bool SetValue(const wxVariant& value) wxOVERRIDE;
	virtual bool GetValue(wxVariant& value) const wxOVERRIDE;

	virtual
		bool IsCompatibleVariantType(const wxString& variantType) const wxOVERRIDE;

#if wxUSE_ACCESSIBILITY
	virtual wxString GetAccessibleDescription() const wxOVERRIDE;
#endif // wxUSE_ACCESSIBILITY

	virtual bool Render(wxRect cell, wxDC* dc, int state) wxOVERRIDE;
	virtual wxSize GetSize() const wxOVERRIDE;

private:
	wxBitmapBundle m_bitmapBundle;

protected:
	wxDECLARE_DYNAMIC_CLASS_NO_COPY(ibDataViewBitmapRenderer);
};

// ---------------------------------------------------------
// ibDataViewToggleRenderer
// ---------------------------------------------------------

class FRONTEND_API ibDataViewToggleRenderer : public ibDataViewRenderer
{
public:
	static wxString GetDefaultType() { return wxS("bool"); }

	ibDataViewToggleRenderer(const wxString& varianttype = GetDefaultType(),
		ibDataViewCellMode mode = wxDATAVIEW_CELL_INERT,
		int align = wxDVR_DEFAULT_ALIGNMENT);

	void ShowAsRadio() { m_radio = true; }

	virtual bool SetValue(const wxVariant& value) wxOVERRIDE;
	virtual bool GetValue(wxVariant& value) const wxOVERRIDE;
#if wxUSE_ACCESSIBILITY
	virtual wxString GetAccessibleDescription() const wxOVERRIDE;
#endif // wxUSE_ACCESSIBILITY

	virtual bool Render(wxRect cell, wxDC* dc, int state) wxOVERRIDE;
	virtual wxSize GetSize() const wxOVERRIDE;

	// Implementation only, don't use nor override
	virtual bool WXActivateCell(const wxRect& cell,
		ibDataViewModel* model,
		const ibDataViewItem& item,
		unsigned int col,
		const wxMouseEvent* mouseEvent) wxOVERRIDE;
private:
	bool    m_toggle;
	bool    m_radio;

protected:
	wxDECLARE_DYNAMIC_CLASS_NO_COPY(ibDataViewToggleRenderer);
};

// ---------------------------------------------------------
// ibDataViewProgressRenderer
// ---------------------------------------------------------

class FRONTEND_API ibDataViewProgressRenderer : public ibDataViewRenderer
{
public:
	static wxString GetDefaultType() { return wxS("long"); }

	ibDataViewProgressRenderer(const wxString& label = wxEmptyString,
		const wxString& varianttype = GetDefaultType(),
		ibDataViewCellMode mode = wxDATAVIEW_CELL_INERT,
		int align = wxDVR_DEFAULT_ALIGNMENT);

	virtual bool SetValue(const wxVariant& value) wxOVERRIDE;
	virtual bool GetValue(wxVariant& value) const wxOVERRIDE;
#if wxUSE_ACCESSIBILITY
	virtual wxString GetAccessibleDescription() const wxOVERRIDE;
#endif // wxUSE_ACCESSIBILITY

	virtual bool Render(wxRect cell, wxDC* dc, int state) wxOVERRIDE;
	virtual wxSize GetSize() const wxOVERRIDE;

private:
	wxString    m_label;
	int         m_value;

protected:
	wxDECLARE_DYNAMIC_CLASS_NO_COPY(ibDataViewProgressRenderer);
};

// ---------------------------------------------------------
// ibDataViewIconTextRenderer
// ---------------------------------------------------------

class FRONTEND_API ibDataViewIconTextRenderer : public ibDataViewRenderer
{
public:
	static wxString GetDefaultType() { return wxS("ibDataViewIconText"); }

	ibDataViewIconTextRenderer(const wxString& varianttype = GetDefaultType(),
		ibDataViewCellMode mode = wxDATAVIEW_CELL_INERT,
		int align = wxDVR_DEFAULT_ALIGNMENT);

	virtual bool SetValue(const wxVariant& value) wxOVERRIDE;
	virtual bool GetValue(wxVariant& value) const wxOVERRIDE;
#if wxUSE_ACCESSIBILITY
	virtual wxString GetAccessibleDescription() const wxOVERRIDE;
#endif // wxUSE_ACCESSIBILITY

	virtual bool Render(wxRect cell, wxDC* dc, int state) wxOVERRIDE;
	virtual wxSize GetSize() const wxOVERRIDE;

	virtual bool HasEditorCtrl() const wxOVERRIDE { return true; }
	virtual wxWindow* CreateEditorCtrl(wxWindow* parent, wxRect labelRect,
		const wxVariant& value) wxOVERRIDE;
	virtual bool GetValueFromEditorCtrl(wxWindow* editor, wxVariant& value) wxOVERRIDE;

private:
	ibDataViewIconText   m_value;

protected:
	wxDECLARE_DYNAMIC_CLASS_NO_COPY(ibDataViewIconTextRenderer);
};

#if wxUSE_SPINCTRL

// ----------------------------------------------------------------------------
// ibDataViewSpinRenderer
// ----------------------------------------------------------------------------

class FRONTEND_API ibDataViewSpinRenderer : public ibDataViewCustomRenderer
{
public:
	ibDataViewSpinRenderer(int min, int max,
		ibDataViewCellMode mode = wxDATAVIEW_CELL_EDITABLE,
		int alignment = wxDVR_DEFAULT_ALIGNMENT);
	virtual bool HasEditorCtrl() const wxOVERRIDE { return true; }
	virtual wxWindow* CreateEditorCtrl(wxWindow* parent, wxRect labelRect, const wxVariant& value) wxOVERRIDE;
	virtual bool GetValueFromEditorCtrl(wxWindow* editor, wxVariant& value) wxOVERRIDE;
	virtual bool Render(wxRect rect, wxDC* dc, int state) wxOVERRIDE;
	virtual wxSize GetSize() const wxOVERRIDE;
	virtual bool SetValue(const wxVariant& value) wxOVERRIDE;
	virtual bool GetValue(wxVariant& value) const wxOVERRIDE;
#if wxUSE_ACCESSIBILITY
	virtual wxString GetAccessibleDescription() const wxOVERRIDE;
#endif // wxUSE_ACCESSIBILITY

private:
	long    m_data;
	long    m_min, m_max;
};

#endif // wxUSE_SPINCTRL

// ----------------------------------------------------------------------------
// ibDataViewChoiceRenderer
// ----------------------------------------------------------------------------

class FRONTEND_API ibDataViewChoiceRenderer : public ibDataViewCustomRenderer
{
public:
	ibDataViewChoiceRenderer(const wxArrayString& choices,
		ibDataViewCellMode mode = wxDATAVIEW_CELL_EDITABLE,
		int alignment = wxDVR_DEFAULT_ALIGNMENT);
	virtual bool HasEditorCtrl() const wxOVERRIDE { return true; }
	virtual wxWindow* CreateEditorCtrl(wxWindow* parent, wxRect labelRect, const wxVariant& value) wxOVERRIDE;
	virtual bool GetValueFromEditorCtrl(wxWindow* editor, wxVariant& value) wxOVERRIDE;
	virtual bool Render(wxRect rect, wxDC* dc, int state) wxOVERRIDE;
	virtual wxSize GetSize() const wxOVERRIDE;
	virtual bool SetValue(const wxVariant& value) wxOVERRIDE;
	virtual bool GetValue(wxVariant& value) const wxOVERRIDE;
#if wxUSE_ACCESSIBILITY
	virtual wxString GetAccessibleDescription() const wxOVERRIDE;
#endif // wxUSE_ACCESSIBILITY

	wxString GetChoice(size_t index) const { return m_choices[index]; }
	const wxArrayString& GetChoices() const { return m_choices; }

private:
	wxArrayString m_choices;
	wxString      m_data;
};

// ----------------------------------------------------------------------------
// ibDataViewChoiceByIndexRenderer
// ----------------------------------------------------------------------------

class FRONTEND_API ibDataViewChoiceByIndexRenderer : public ibDataViewChoiceRenderer
{
public:
	ibDataViewChoiceByIndexRenderer(const wxArrayString& choices,
		ibDataViewCellMode mode = wxDATAVIEW_CELL_EDITABLE,
		int alignment = wxDVR_DEFAULT_ALIGNMENT);

	virtual wxWindow* CreateEditorCtrl(wxWindow* parent, wxRect labelRect, const wxVariant& value) wxOVERRIDE;
	virtual bool GetValueFromEditorCtrl(wxWindow* editor, wxVariant& value) wxOVERRIDE;

	virtual bool SetValue(const wxVariant& value) wxOVERRIDE;
	virtual bool GetValue(wxVariant& value) const wxOVERRIDE;
#if wxUSE_ACCESSIBILITY
	virtual wxString GetAccessibleDescription() const wxOVERRIDE;
#endif // wxUSE_ACCESSIBILITY
};

// ----------------------------------------------------------------------------
// ibDataViewDateRenderer
// ----------------------------------------------------------------------------

#if wxUSE_DATEPICKCTRL
class FRONTEND_API ibDataViewDateRenderer : public ibDataViewCustomRenderer
{
public:
	static wxString GetDefaultType() { return wxS("datetime"); }

	ibDataViewDateRenderer(const wxString& varianttype = GetDefaultType(),
		ibDataViewCellMode mode = wxDATAVIEW_CELL_EDITABLE,
		int align = wxDVR_DEFAULT_ALIGNMENT);

	virtual bool HasEditorCtrl() const wxOVERRIDE { return true; }
	virtual wxWindow* CreateEditorCtrl(wxWindow* parent, wxRect labelRect, const wxVariant& value) wxOVERRIDE;
	virtual bool GetValueFromEditorCtrl(wxWindow* editor, wxVariant& value) wxOVERRIDE;
	virtual bool SetValue(const wxVariant& value) wxOVERRIDE;
	virtual bool GetValue(wxVariant& value) const wxOVERRIDE;
#if wxUSE_ACCESSIBILITY
	virtual wxString GetAccessibleDescription() const wxOVERRIDE;
#endif // wxUSE_ACCESSIBILITY
	virtual bool Render(wxRect cell, wxDC* dc, int state) wxOVERRIDE;
	virtual wxSize GetSize() const wxOVERRIDE;

private:
	wxString FormatDate() const;

	wxDateTime    m_date;
};
#else // !wxUSE_DATEPICKCTRL
typedef ibDataViewTextRenderer ibDataViewDateRenderer;
#endif

// ----------------------------------------------------------------------------
// ibDataViewCheckIconTextRenderer: 3-state checkbox + text + optional icon
// ----------------------------------------------------------------------------

class FRONTEND_API ibDataViewCheckIconTextRenderer
	: public ibDataViewCustomRenderer
{
public:
	static wxString GetDefaultType() { return wxS("ibDataViewCheckIconText"); }

	explicit ibDataViewCheckIconTextRenderer
	(
		ibDataViewCellMode mode = wxDATAVIEW_CELL_ACTIVATABLE,
		int align = wxDVR_DEFAULT_ALIGNMENT
	);

	// This renderer can always display the 3rd ("indeterminate") checkbox
	// state if the model contains cells with wxCHK_UNDETERMINED value, but it
	// doesn't allow the user to set it by default. Call this method to allow
	// this to happen.
	void Allow3rdStateForUser(bool allow = true);

	virtual bool SetValue(const wxVariant& value) wxOVERRIDE;
	virtual bool GetValue(wxVariant& value) const wxOVERRIDE;

#if wxUSE_ACCESSIBILITY
	virtual wxString GetAccessibleDescription() const wxOVERRIDE;
#endif // wxUSE_ACCESSIBILITY

	virtual wxSize GetSize() const wxOVERRIDE;
	virtual bool Render(wxRect cell, wxDC* dc, int state) wxOVERRIDE;
	virtual bool ActivateCell(const wxRect& cell,
		ibDataViewModel* model,
		const ibDataViewItem& item,
		unsigned int col,
		const wxMouseEvent* mouseEvent) wxOVERRIDE;

private:
	wxSize GetCheckSize() const;

	// Just some arbitrary constants defining margins, in pixels.
	enum
	{
		MARGIN_CHECK_ICON = 3,
		MARGIN_ICON_TEXT = 4
	};

	ibDataViewCheckIconText m_value;

	bool m_allow3rdStateForUser;

	wxDECLARE_DYNAMIC_CLASS_NO_COPY(ibDataViewCheckIconTextRenderer);
};

// this class is obsolete, its functionality was merged in
// ibDataViewTextRenderer itself now, don't use it any more
#define ibDataViewTextRendererAttr ibDataViewTextRenderer

#endif // _WX_DVRENDERERS_H_

