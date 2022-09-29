#ifndef _SIZER_H_
#define _SIZER_H_

#include "baseControl.h"

class IValueSizer : public IValueControl {
	wxDECLARE_ABSTRACT_CLASS(IValueSizer);
protected:

	PropertyCategory* m_categorySizer = IPropertyObject::CreatePropertyCategory({ "sizerItem", _("sizer") });
	Property* m_propertyMinSize = IPropertyObject::CreateProperty(m_categorySizer, { "minimum_size" , "minimum size", "Sets the minimum size of the window, to indicate to the sizer layout mechanism that this is the minimum required size." }, PropertyType::PT_WXSIZE, wxDefaultSize);

public:

	IValueSizer() : IValueControl() {}

	virtual int GetComponentType() const override {
		return COMPONENT_TYPE_SIZER;
	}

	//load & save object in control 
	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());

protected:

	void UpdateSizer(wxSizer* sizer);
};

//////////////////////////////////////////////////////////////////////////////

class CValueSizerItem : public IValueFrame {
	wxDECLARE_DYNAMIC_CLASS(CValueSizerItem);
private:
	OptionList* GetDefaultOptionBorder(PropertyBitlist* property);
	OptionList* GetDefaultOptionState(PropertyOption* property);
private:
	PropertyCategory* m_categorySizerItem = IPropertyObject::CreatePropertyCategory({ "sizerItem", _("sizer item") });
	Property* m_propertyProportion = IPropertyObject::CreateProperty(m_categorySizerItem, { "proportion", "proportion" }, PropertyType::PT_INT, 0);
	Property* m_propertyFlagBorder = IPropertyObject::CreateProperty(m_categorySizerItem, { "flag_border", "flag border" }, &CValueSizerItem::GetDefaultOptionBorder, wxDirection::wxALL);
	Property* m_propertyFlagState = IPropertyObject::CreateProperty(m_categorySizerItem, { "flag_state", "flag state" }, &CValueSizerItem::GetDefaultOptionState, wxStretch::wxSHRINK);
	Property* m_propertyBorder = IPropertyObject::CreateProperty(m_categorySizerItem, { "border", "border" }, PropertyType::PT_INT, 5);
public:

	void SetProportion(int proportion) {
		m_propertyProportion->SetValue(proportion);
	}

	int GetProportion() const {
		return m_propertyProportion->GetValueAsInteger();
	}

	void SetFlagBorder(long flag_border) const {
		m_propertyFlagBorder->SetValue(flag_border);
	}

	long GetFlagBorder() const {
		return m_propertyFlagBorder->GetValueAsInteger();
	}

	void SetFlagState(long flag_state) const {
		m_propertyFlagState->SetValue(flag_state);
	}

	long GetFlagState() const {
		return m_propertyFlagState->GetValueAsInteger();
	}

	int GetBorder() const {
		return m_propertyBorder->GetValueAsInteger();
	}

	void SetBorder(long border) const {
		m_propertyBorder->SetValue(border);
	}

	CValueSizerItem();

	virtual void OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost, bool first—reated) override;
	virtual void OnUpdated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost) override;

	virtual wxString GetClassName() const override {
		return wxT("sizerItem");
	}

	virtual wxString GetObjectTypeName() const override {
		return wxT("sizer");
	}

	virtual int GetComponentType() const override {
		return COMPONENT_TYPE_SIZERITEM;
	}

	//get metadata
	virtual IMetadata* GetMetaData() const override;

	/**
	* Support form
	*/
	virtual wxString GetControlName() const {
		return GetObjectTypeName();
	};

	virtual CValueForm* GetOwnerForm() const {
		return m_formOwner;
	}

	virtual void SetOwnerForm(CValueForm* ownerForm) {
		m_formOwner = ownerForm;
	}

	// allow getting value in control
	virtual bool HasValueInControl() const {
		return false;
	}

	/**
	* Can delete object
	*/
	virtual bool CanDeleteControl() const {
		return true;
	}

	//runtime 
	virtual CProcUnit* GetFormProcUnit() const;

	/**
	* Get type form
	*/
	virtual form_identifier_t GetTypeForm() const;

	//load & save object in control 
	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());

	friend class CVisualEditorDatabase;

protected:

	//frame owner 
	CValueForm* m_formOwner;
};

//////////////////////////////////////////////////////////////////////////////

class CValueBoxSizer : public IValueSizer {
	wxDECLARE_DYNAMIC_CLASS(CValueBoxSizer);
public:
	Property* m_propertyOrient = IPropertyObject::CreateProperty(m_categorySizer, "orient", &CValueBoxSizer::GetOrient, wxVERTICAL);
public:

	CValueBoxSizer();

	virtual wxObject* Create(wxObject* /*parent*/, IVisualHost* /*visualHost*/) override;
	virtual void OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost, bool first—reated) override;
	virtual void Update(wxObject* wxobject, IVisualHost* visualHost) override;
	virtual void Cleanup(wxObject* obj, IVisualHost* visualHost) override;

	virtual wxString GetClassName() const override {
		return wxT("boxsizer");
	}

	virtual wxString GetObjectTypeName() const override {
		return wxT("sizer");
	}

	//load & save object in control 
	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());
};

#include <wx/wrapsizer.h>

class CValueWrapSizer : public IValueSizer {
	wxDECLARE_DYNAMIC_CLASS(CValueWrapSizer);
protected:
	Property* m_propertyOrient = IPropertyObject::CreateProperty(m_categorySizer, "orient", &CValueWrapSizer::GetOrient, wxHORIZONTAL);
public:

	CValueWrapSizer();

	virtual wxObject* Create(wxObject* /*parent*/, IVisualHost* /*visualHost*/) override;
	virtual void OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost, bool first—reated) override;
	virtual void Update(wxObject* wxobject, IVisualHost* visualHost) override;
	virtual void Cleanup(wxObject* obj, IVisualHost* visualHost) override;

	virtual wxString GetClassName() const override {
		return wxT("wrapsizer");
	}

	virtual wxString GetObjectTypeName() const override {
		return wxT("sizer");
	}

	//load & save object in control 
	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());
};

class CValueStaticBoxSizer : public IValueSizer {
	wxDECLARE_DYNAMIC_CLASS(CValueStaticBoxSizer);
protected:
	Property* m_propertyOrient = IPropertyObject::CreateProperty(m_categorySizer, "orient", &CValueStaticBoxSizer::GetOrient, wxHORIZONTAL);
	Property* m_propertyLabel = IPropertyObject::CreateProperty(m_categorySizer, "label", PropertyType::PT_WXSTRING);
	Property* m_propertyFont = IPropertyObject::CreateProperty(m_categorySizer, { "font" , "font", "Sets the font for this window. This should not be use for a parent window if you don't want its font to be inherited by its children" }, PropertyType::PT_WXFONT);
	Property* m_propertyFG = IPropertyObject::CreateProperty(m_categorySizer, { "fg", "fg", "Sets the foreground colour of the window." }, PropertyType::PT_WXCOLOUR, wxColour(0, 120, 215));
	Property* m_propertyBG = IPropertyObject::CreateProperty(m_categorySizer, { "bg", "bg", "Sets the background colour of the window." }, PropertyType::PT_WXCOLOUR, wxColour(240, 240, 240));
	Property* m_propertyTooltip = IPropertyObject::CreateProperty(m_categorySizer, { "tooltip", "tooltip", "Attach a tooltip to the window." }, PropertyType::PT_WXSTRING);
	Property* m_propertyContextMenu = IPropertyObject::CreateProperty(m_categorySizer, { "context_menu", "context menu", "Generates event handler for displaying of menu assigned to this widgets as a context menu." }, PropertyType::PT_BOOL);
	Property* m_propertyContextHelp = IPropertyObject::CreateProperty(m_categorySizer, { "context_help", "context help", "Attach context-sensitive help to the window. Note: The Project's &quot;help_provider&quot; property must be set for context-sensitive help to work." }, PropertyType::PT_WXSTRING);
	Property* m_propertyEnabled = IPropertyObject::CreateProperty(m_categorySizer, { "enabled", "enabled", "Enable or disable the window for user input. Note that when a parent window is disabled, all of its children are disabled as well and they are reenabled again when the parent is." }, PropertyType::PT_BOOL, true);
	Property* m_propertyVisible = IPropertyObject::CreateProperty(m_categorySizer, { "visible", "visible", "Indicates that a pane caption should be visible." }, PropertyType::PT_BOOL, true);
public:

	CValueStaticBoxSizer();

	virtual wxObject* Create(wxObject* parent, IVisualHost* visualHost) override;
	virtual void OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost, bool first—reated) override;
	virtual void Update(wxObject* wxobject, IVisualHost* visualHost) override;
	virtual void Cleanup(wxObject* obj, IVisualHost* visualHost) override;

	virtual wxString GetClassName() const override {
		return wxT("staticboxsizer");
	}

	virtual wxString GetObjectTypeName() const override {
		return wxT("sizer");
	}

	//load & save object in control 
	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());
};

class CValueGridSizer : public IValueSizer {
	wxDECLARE_DYNAMIC_CLASS(CValueGridSizer);
protected:
	Property* m_propertyRows = IPropertyObject::CreateProperty(m_categorySizer, "rows", PropertyType::PT_INT, 0);
	Property* m_propertyCols = IPropertyObject::CreateProperty(m_categorySizer, "cols", PropertyType::PT_INT, 2);
public:

	CValueGridSizer();

	virtual wxObject* Create(wxObject* /*parent*/, IVisualHost* /*visualHost*/) override;
	virtual void OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost, bool first—reated) override;
	virtual void Update(wxObject* wxobject, IVisualHost* visualHost) override;
	virtual void Cleanup(wxObject* obj, IVisualHost* visualHost) override;

	virtual wxString GetClassName() const override {
		return wxT("gridsizer");
	}

	virtual wxString GetObjectTypeName() const override {
		return wxT("sizer");
	}

	//load & save object in control 
	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());
};

#endif 