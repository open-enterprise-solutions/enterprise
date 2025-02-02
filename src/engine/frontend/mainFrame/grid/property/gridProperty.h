#ifndef _gridProperty_H__
#define _gridProperty_H__

#include <wx/grid.h>
#include "backend/wrapper/propertyInfo.h"

class CGrid;

class CPropertyObjectGrid : public IPropertyObject {
	CGrid* m_ownerGrid;
private:

	PropertyCategory* m_categoryGeneral = IPropertyObject::CreatePropertyCategory({"general", _("general")});
	Property* m_propertyName = IPropertyObject::CreateProperty(m_categoryGeneral, "name", PropertyType::PT_WXNAME);
	Property* m_propertyText = IPropertyObject::CreateProperty(m_categoryGeneral, "text", PropertyType::PT_WXSTRING);

	PropertyCategory* m_categoryAlignment = IPropertyObject::CreatePropertyCategory({"alignment", _("alignment")});
	Property* m_propertyAlignHorz = IPropertyObject::CreateProperty(m_categoryAlignment, { "align_horz", "align horz" }, &CPropertyObjectGrid::GetAlignment);
	Property* m_propertyAlignVert = IPropertyObject::CreateProperty(m_categoryAlignment, { "align_vert", "align vert" }, &CPropertyObjectGrid::GetAlignment);
	Property* m_propertyOrient = IPropertyObject::CreateProperty(m_categoryAlignment, "orient_text", &CPropertyObjectGrid::GetOrient);

	PropertyCategory* m_categoryAppearance = IPropertyObject::CreatePropertyCategory({ "appearance", "appearance" });
	Property* m_propertyFont = IPropertyObject::CreateProperty(m_categoryAppearance, "font", PropertyType::PT_WXFONT);
	Property* m_propertyBackgroundColour = IPropertyObject::CreateProperty(m_categoryAppearance, { "background_colour", "background colour" }, PropertyType::PT_WXCOLOUR);
	Property* m_propertyTextColour = IPropertyObject::CreateProperty(m_categoryAppearance, { "text_colour", "text colour" }, PropertyType::PT_WXCOLOUR);

	PropertyCategory* m_categoryBorder = IPropertyObject::CreatePropertyCategory({"border", _("border")});
	Property* m_propertyLeftBorder = IPropertyObject::CreateProperty(m_categoryBorder, { "left_border", "left border" }, &CPropertyObjectGrid::GetBorder, wxPENSTYLE_TRANSPARENT);
	Property* m_propertyRightBorder = IPropertyObject::CreateProperty(m_categoryBorder, { "right_border", "right border" }, &CPropertyObjectGrid::GetBorder, wxPENSTYLE_TRANSPARENT);
	Property* m_propertyTopBorder = IPropertyObject::CreateProperty(m_categoryBorder, { "top_border", "top border" }, &CPropertyObjectGrid::GetBorder, wxPENSTYLE_TRANSPARENT);
	Property* m_propertyBottomBorder = IPropertyObject::CreateProperty(m_categoryBorder, { "bottom_border", "bottom border" }, &CPropertyObjectGrid::GetBorder, wxPENSTYLE_TRANSPARENT);
	Property* m_propertyColourBorder = IPropertyObject::CreateProperty(m_categoryBorder, { "border_colour", "border colour" }, PropertyType::PT_WXCOLOUR, *wxBLACK);

private:

	std::vector<wxGridBlockCoords> m_currentBlocks;

	OptionList* GetAlignment(PropertyOption* property) {
		OptionList* optList = new OptionList();
		optList->AddOption(_("left"), wxALIGN_LEFT);
		optList->AddOption(_("right"), wxALIGN_RIGHT);
		optList->AddOption(_("top"), wxALIGN_TOP);
		optList->AddOption(_("bottom"), wxALIGN_BOTTOM);
		optList->AddOption(_("center"), wxALIGN_CENTER);
		return optList;
	}

	OptionList* GetOrient(PropertyOption* property) {
		OptionList* optList = new OptionList();
		optList->AddOption(_("vertical"), wxVERTICAL);
		optList->AddOption(_("horizontal"), wxHORIZONTAL);
		return optList;
	}

	OptionList* GetBorder(PropertyOption* property) {
		OptionList* optList = new OptionList();
		optList->AddOption(_("none"), wxPENSTYLE_TRANSPARENT);
		optList->AddOption(_("solid"), wxPENSTYLE_SOLID);
		optList->AddOption(_("dotted"), wxPENSTYLE_DOT);

		optList->AddOption(_("thin dashed"), wxPENSTYLE_SHORT_DASH);
		optList->AddOption(_("thick dashed"), wxPENSTYLE_DOT_DASH);
		optList->AddOption(_("large dashed"), wxPENSTYLE_LONG_DASH);

		return optList;
	}

	void ClearSelectedCell() {
		m_currentBlocks.clear();
	}

	void AddSelectedCell(const wxGridBlockCoords& coords, bool afterErase = false);
	void ShowProperty();

	void OnPropertyCreated(Property* property, const wxGridBlockCoords& coords);
	void OnPropertyChanged(Property* property, const wxGridBlockCoords& coords);

	friend class CGrid;

public:

	CPropertyObjectGrid(CGrid* ownerGrid);
	virtual ~CPropertyObjectGrid();

	//system override 
	virtual int GetComponentType() const {
		return COMPONENT_TYPE_ABSTRACT;
	}

	virtual wxString GetObjectTypeName() const override {
		return wxT("cells");
	}

	virtual wxString GetClassName() const {
		return wxT("cells");
	}

	/**
	* Property events
	*/
	virtual void OnPropertyCreated(Property* property);
	virtual void OnPropertyChanged(Property* property, const wxVariant& oldValue, const wxVariant& newValue);

};

#endif 