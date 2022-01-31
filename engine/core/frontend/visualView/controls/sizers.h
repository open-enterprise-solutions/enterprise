#ifndef _SIZER_H_
#define _SIZER_H_

#include "baseControl.h"

class IValueSizer : public IValueControl
{
	wxDECLARE_ABSTRACT_CLASS(IValueSizer);

protected:

	OptionList *GetOrient(Property *property)
	{
		OptionList *optList = new OptionList();
		optList->AddOption("Vertical", wxVERTICAL);
		optList->AddOption("Horizontal", wxHORIZONTAL);
		return optList;
	}

public:

	wxSize m_minimum_size;

public:

	IValueSizer(bool sizerItem = false) : IValueControl(),
		m_minimum_size(wxDefaultSize)
	{
		if (!sizerItem) {
			PropertyContainer *categorySizer = IObjectBase::CreatePropertyContainer("Sizer");
			categorySizer->AddProperty("minimum_size", PropertyType::PT_WXSIZE);
			m_category->AddCategory(categorySizer);
		}
	}

	virtual int GetComponentType() override { return COMPONENT_TYPE_SIZER; }
	virtual bool IsItem() override { return false; }

	//load & save object in control 
	virtual bool LoadData(CMemoryReader &reader);
	virtual bool SaveData(CMemoryWriter &writer = CMemoryWriter());

	//property
	virtual void ReadProperty() override;
	virtual void SaveProperty() override;

protected:

	void UpdateSizer(wxSizer* sizer);
};

//////////////////////////////////////////////////////////////////////////////

class CValueSizerItem : public IValueSizer
{
	wxDECLARE_DYNAMIC_CLASS(CValueSizerItem);

private:

	OptionList *GetDefaultOptionBorder(Property *property);
	OptionList *GetDefaultOptionState(Property *property);

public:

	int m_proportion;
	long m_flag_border;
	long m_flag_state;
	int m_border;

public:

	CValueSizerItem();

	virtual void OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost *visualHost, bool first—reated) override;
	virtual void OnUpdated(wxObject* wxobject, wxWindow* wxparent, IVisualHost *visualHost) override;

	virtual wxString GetClassName() const override { return wxT("sizerItem"); }
	virtual wxString GetObjectTypeName() const override { return wxT("sizer"); }

	virtual int GetComponentType() override { return COMPONENT_TYPE_SIZERITEM; }
	virtual bool IsItem() override { return true; }

	//load & save object in control 
	virtual bool LoadData(CMemoryReader &reader);
	virtual bool SaveData(CMemoryWriter &writer = CMemoryWriter());

	//property
	virtual void ReadProperty() override;
	virtual void SaveProperty() override;

	friend class CVisualEditorDatabase;
};

//////////////////////////////////////////////////////////////////////////////

class CValueBoxSizer : public IValueSizer {
	wxDECLARE_DYNAMIC_CLASS(CValueBoxSizer);
public:
	wxOrientation m_orient;
	long m_flags;
public:

	CValueBoxSizer();

	virtual wxObject* Create(wxObject* /*parent*/, IVisualHost* /*visualHost*/) override;
	virtual void OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost *visualHost, bool first—reated) override;
	virtual void Update(wxObject* wxobject, IVisualHost *visualHost) override;
	virtual void Cleanup(wxObject* obj, IVisualHost *visualHost) override;

	virtual wxString GetClassName() const override { return wxT("boxsizer"); }
	virtual wxString GetObjectTypeName() const override { return wxT("sizer"); }

	//load & save object in control 
	virtual bool LoadData(CMemoryReader &reader);
	virtual bool SaveData(CMemoryWriter &writer = CMemoryWriter());

	//property
	virtual void ReadProperty() override;
	virtual void SaveProperty() override;

	/**
	* Set property data
	*/
	virtual void SetPropertyData(Property *property, const CValue &srcValue);

	/**
	* Get property data
	*/
	virtual CValue GetPropertyData(Property *property);
};

#include <wx/wrapsizer.h>

class CValueWrapSizer : public IValueSizer {
	wxDECLARE_DYNAMIC_CLASS(CValueWrapSizer);
protected:
	wxOrientation m_orient = wxHORIZONTAL;
public:

	CValueWrapSizer();

	virtual wxObject* Create(wxObject* /*parent*/, IVisualHost* /*visualHost*/) override;
	virtual void OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost *visualHost, bool first—reated) override;
	virtual void Update(wxObject* wxobject, IVisualHost *visualHost) override;
	virtual void Cleanup(wxObject* obj, IVisualHost *visualHost) override;

	virtual wxString GetClassName() const override { return wxT("wrapsizer"); }
	virtual wxString GetObjectTypeName() const override { return wxT("sizer"); }

	//load & save object in control 
	virtual bool LoadData(CMemoryReader &reader);
	virtual bool SaveData(CMemoryWriter &writer = CMemoryWriter());

	//property
	virtual void ReadProperty() override;
	virtual void SaveProperty() override;

	/**
	* Set property data
	*/
	virtual void SetPropertyData(Property *property, const CValue &srcValue);

	/**
	* Get property data
	*/
	virtual CValue GetPropertyData(Property *property);
};

class CValueStaticBoxSizer : public IValueSizer {
	wxDECLARE_DYNAMIC_CLASS(CValueStaticBoxSizer);
protected:	
	wxOrientation m_orient = wxHORIZONTAL;
	wxString m_label;
	wxString m_id;
	bool m_parent;
	wxFont m_font;
	wxColour m_fg = RGB(0, 120, 215);
	wxColour m_bg = RGB(240, 240, 240);
	wxString m_tooltip;
	bool m_context_menu;
	wxString m_context_help;
	bool m_enabled = true;
	bool m_visible = true;
public:

	CValueStaticBoxSizer();

	virtual wxObject* Create(wxObject* parent, IVisualHost *visualHost) override;
	virtual void OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost *visualHost, bool first—reated) override;
	virtual void Update(wxObject* wxobject, IVisualHost *visualHost) override;
	virtual void Cleanup(wxObject* obj, IVisualHost *visualHost) override;

	virtual wxString GetClassName() const override { return wxT("staticboxsizer"); }
	virtual wxString GetObjectTypeName() const override { return wxT("sizer"); }

	//load & save object in control 
	virtual bool LoadData(CMemoryReader &reader);
	virtual bool SaveData(CMemoryWriter &writer = CMemoryWriter());

	//property
	virtual void ReadProperty() override;
	virtual void SaveProperty() override;

	/**
	* Set property data
	*/
	virtual void SetPropertyData(Property *property, const CValue &srcValue);

	/**
	* Get property data
	*/
	virtual CValue GetPropertyData(Property *property);
};

class CValueGridSizer : public IValueSizer {
	wxDECLARE_DYNAMIC_CLASS(CValueGridSizer);
protected:
	int m_row = 0;
	int m_cols = 2;
public:

	CValueGridSizer();

	virtual wxObject* Create(wxObject* /*parent*/, IVisualHost* /*visualHost*/) override;
	virtual void OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost *visualHost, bool first—reated) override;
	virtual void Update(wxObject* wxobject, IVisualHost *visualHost) override;
	virtual void Cleanup(wxObject* obj, IVisualHost *visualHost) override;

	virtual wxString GetClassName() const override { return wxT("gridsizer"); }
	virtual wxString GetObjectTypeName() const override { return wxT("sizer"); }

	//load & save object in control 
	virtual bool LoadData(CMemoryReader &reader);
	virtual bool SaveData(CMemoryWriter &writer = CMemoryWriter());

	//property
	virtual void ReadProperty() override;
	virtual void SaveProperty() override;
};

#endif 