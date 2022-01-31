#ifndef _ADVPROPS_H__
#define _ADVPROPS_H__

#include "common/fontcontainer.h"

#include <wx/propgrid/propgrid.h>
#include <wx/propgrid/advprops.h>

// -----------------------------------------------------------------------
// wxPGSizeProperty
// -----------------------------------------------------------------------

class wxPGSizeProperty : public wxPGProperty
{
	WX_PG_DECLARE_PROPERTY_CLASS(wxPGSizeProperty)
public:
	wxPGSizeProperty(const wxString& label = wxPG_LABEL,
		const wxString& name = wxPG_LABEL,
		const wxSize& value = wxSize());

	wxVariant ChildChanged(wxVariant& thisValue, int childIndex,
		wxVariant& childValue) const override;

	void RefreshChildren() override;

protected:
	void DoSetValue(const wxSize& value) { m_value = WXVARIANT(value); }
};

// -----------------------------------------------------------------------
// wxPGPointProperty
// -----------------------------------------------------------------------

class wxPGPointProperty : public wxPGProperty
{
	WX_PG_DECLARE_PROPERTY_CLASS(wxPGPointProperty)
public:
	wxPGPointProperty(const wxString& label = wxPG_LABEL,
		const wxString& name = wxPG_LABEL,
		const wxPoint&  value = wxPoint());
	~wxPGPointProperty() override;

	wxVariant ChildChanged(wxVariant& thisValue, int childIndex,
		wxVariant& childValue) const override;

	void RefreshChildren() override;

protected:
	void DoSetValue(const wxPoint& value) { m_value = WXVARIANT(value); }
};

// -----------------------------------------------------------------------
// wxPGBitmapProperty
// -----------------------------------------------------------------------

class wxPGBitmapProperty : public wxPGProperty
{
	WX_PG_DECLARE_PROPERTY_CLASS(wxPGBitmapProperty)

public:
	wxPGBitmapProperty(const wxString& label = wxPG_LABEL,
		const wxString& name = wxPG_LABEL,
		const wxString& value = wxString());

	virtual ~wxPGBitmapProperty();

	wxPGProperty *CreatePropertySource(int sourceIndex = 0);

	wxPGProperty *CreatePropertyArtId();
	wxPGProperty *CreatePropertyArtClient();

	wxString SetupImage(const wxString &imgPath = wxEmptyString);
	wxString SetupResource(const wxString &resName = wxEmptyString);

	int prevSrc;
	void SetPrevSource(int src) { prevSrc = src; }

	virtual wxVariant ChildChanged(wxVariant& thisValue, int childIndex,
		wxVariant& childValue) const override;

	virtual void OnSetValue() override;
	void CreateChildren();

	void UpdateChildValues(const wxString& value);
protected:

	void GetChildValues(const wxString& parentValue, wxArrayString& childValues) const;

	static wxArrayString m_ids;
	static wxArrayString m_clients;
	wxArrayString m_strings;
};

// -----------------------------------------------------------------------
// wxPGFontProperty
// -----------------------------------------------------------------------

class wxPGFontProperty : public wxPGProperty
{
	WX_PG_DECLARE_PROPERTY_CLASS(wxPGFontProperty)
public:

	wxPGFontProperty(const wxString& label = wxPG_LABEL, const wxString& name = wxPG_LABEL, const wxFontContainer& value = *wxNORMAL_FONT);
	~wxPGFontProperty() override;

	wxVariant ChildChanged(wxVariant& thisValue, int childIndex,
		wxVariant& childValue) const override;

	void RefreshChildren() override;

	void OnSetValue() override;
	wxString GetValueAsString(int argFlags = 0) const override;

	bool OnEvent(wxPropertyGrid* propgrid, wxWindow* primary, wxEvent& event) override;
};

// -----------------------------------------------------------------------
// wxStringControlProperty
// -----------------------------------------------------------------------

class wxStringControlProperty : public wxStringProperty
{
	WX_PG_DECLARE_PROPERTY_CLASS(wxStringControlProperty)

public:

	wxStringControlProperty(const wxString& label = wxPG_LABEL,
		const wxString& name = wxPG_LABEL,
		const wxString& value = wxEmptyString) : wxStringProperty(label, name, value) {}

	virtual wxString ValueToString(wxVariant& value, int argFlags = 0) const override;
	virtual bool StringToValue(wxVariant& variant,
		const wxString& text,
		int argFlags = 0) const override;
};

// -----------------------------------------------------------------------
// wxEventControlProperty
// -----------------------------------------------------------------------

class wxEventControlProperty : public wxStringProperty
{
	WX_PG_DECLARE_PROPERTY_CLASS(wxEventControlProperty)

public:

	wxEventControlProperty(const wxString& label = wxPG_LABEL,
		const wxString& name = wxPG_LABEL,
		const wxString& value = wxEmptyString) : wxStringProperty(label, name, value) {

		m_flags |= wxPG_PROP_ACTIVE_BTN; // Property button always enabled.
	}

	virtual wxString ValueToString(wxVariant& value, int argFlags = 0) const override;
	virtual bool StringToValue(wxVariant& variant,
		const wxString& text,
		int argFlags = 0) const override;

	virtual wxPGEditorDialogAdapter* GetEditorDialog() const override;
};

// -----------------------------------------------------------------------
// wxPGToolActionProperty
// -----------------------------------------------------------------------

#include "frontend/visualView/controls/baseControl.h"

class wxPGToolActionProperty : public wxPGProperty
{
	WX_PG_DECLARE_PROPERTY_CLASS(wxPGToolActionProperty)

private:

	IObjectBase *GetObject() const { return m_currentObject; }

	mutable class toolData_t {
		bool m_bCustomEvent = false;
		int m_toolID = wxNOT_FOUND;
		wxString m_strEvent = wxEmptyString;
	public:
		toolData_t() {}
		~toolData_t() {}
		void SetNumber(long number) {
			m_bCustomEvent = false; m_toolID = number; m_strEvent = wxEmptyString;
		}
		int GetNumber() const { return m_toolID; }
		void SetString(const wxString &string) {
			m_bCustomEvent = true; m_strEvent = string; m_toolID = wxNOT_FOUND;
		}
		wxString GetString() const { return m_strEvent; }
		bool IsCustomEvent() const {
			return m_bCustomEvent;
		}

	} m_toolData;

public:

	int GetNumber() const { return m_toolData.GetNumber(); }
	wxString GetString() const { return m_toolData.GetString(); }

	bool IsCustomEvent() const {
		return m_toolData.IsCustomEvent();
	}

	wxPGToolActionProperty(const wxString& label = wxPG_LABEL,
		const wxString& name = wxPG_LABEL,
		wxPGChoices &choices = wxPGChoices(),
		const wxString& value = wxEmptyString,
		IObjectBase *currentObject = NULL);

	virtual ~wxPGToolActionProperty();

	virtual wxString ValueToString(wxVariant& value, int argFlags = 0) const override;

	virtual bool StringToValue(wxVariant& variant,
		const wxString& text,
		int argFlags = 0) const override;

	virtual bool IntToValue(wxVariant& value,
		int number,
		int argFlags = 0) const override;

	virtual void OnSetValue() override;

	virtual wxPGEditorDialogAdapter* GetEditorDialog() const override;

protected:

	IObjectBase *m_currentObject;
};

// -----------------------------------------------------------------------
// wxPGSourceProperty
// -----------------------------------------------------------------------

class wxPGSourceProperty : public wxPGProperty
{
	WX_PG_DECLARE_PROPERTY_CLASS(wxPGSourceProperty)

private:

	IObjectBase *GetObject() const { return m_currentObject; }

public:

	wxPGSourceProperty(const wxString& label = wxPG_LABEL,
		const wxString& name = wxPG_LABEL,
		const long& value = 0,
		IObjectBase *currentObject = NULL);

	virtual ~wxPGSourceProperty();

	virtual wxString ValueToString(wxVariant& value, int argFlags = 0) const override;

	virtual bool StringToValue(wxVariant& variant,
		const wxString& text,
		int argFlags = 0) const override;

	virtual wxPGEditorDialogAdapter* GetEditorDialog() const override;

protected:

	IObjectBase *m_currentObject;
};

// -----------------------------------------------------------------------
// wxPGTypeSelectorProperty
// -----------------------------------------------------------------------

class wxPGTypeSelectorProperty : public wxEnumProperty
{
	WX_PG_DECLARE_PROPERTY_CLASS(wxPGTypeSelectorProperty)

private:

	IObjectBase *GetObject() const { return m_currentObject; }

public:

	wxPGTypeSelectorProperty(const wxString& label = wxPG_LABEL,
		const wxString& name = wxPG_LABEL,
		wxPGChoices& choices = wxPGChoices(),
		int value = 0,
		IObjectBase *currentObject = NULL) : wxEnumProperty(label, name, choices, value), m_currentObject(currentObject)
	{
	}

	virtual wxString ValueToString(wxVariant& value, int argFlags = 0) const override;
	virtual bool StringToValue(wxVariant& variant,
		const wxString& text,
		int argFlags = 0) const override;

	virtual wxPGEditorDialogAdapter* GetEditorDialog() const override;

protected:

	IObjectBase *m_currentObject;
};

#endif
