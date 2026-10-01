#ifndef __META_COMMAND_GROUP_OBJECT_H__
#define __META_COMMAND_GROUP_OBJECT_H__

#include "metaObject.h"

// WHICH PANEL a command group is shown in. Every group belongs to one: the platform's own by what they are —
// Important and Normal to the navigation panel, Create, Reports and Service to the actions panel — and a
// group the configuration declares by its Category.
//
// The FORM COMMAND BAR is the third: a group there is not a heading on a section page but ONE BUTTON on the
// bar of a form, and pressing it opens a submenu of its commands — the object's own, and every common command
// typed for the object the form shows. That is how commands of many origins end up under one "Print".
enum ibCommandGroupCategory {
	ibCommandGroupCategory_Navigation = 0,
	ibCommandGroupCategory_Actions,
	ibCommandGroupCategory_FormCommandBar,
};

class ibValueEnumCommandGroupCategory : public ibValueEnumeration<ibCommandGroupCategory> {
public:
	ibValueEnumCommandGroupCategory() : ibValueEnumeration() {}
	virtual void CreateEnumeration() {
		AddEnumeration(ibCommandGroupCategory_Navigation,     wxT("NavigationPanel"), _("Navigation panel"));
		AddEnumeration(ibCommandGroupCategory_Actions,        wxT("ActionsPanel"),    _("Actions panel"));
		AddEnumeration(ibCommandGroupCategory_FormCommandBar, wxT("FormCommandBar"),  _("Form command bar"));
	}
};

// ⭐ THE PLATFORM'S OWN GROUPS, IN THE ORDER THEY ARE SHOWN — the navigation panel's two, then the actions
// panel's three. One list, because five surfaces wrote it out by hand and no two agreed: the section page of
// the running application never drew Important at all, and the list of every item a section holds left it
// out too, so a command marked Important vanished from the one page it was marked for.
inline constexpr ibInterfaceCommandSection g_platformCommandGroups[] = {
	ibInterfaceCommandSection_Important, ibInterfaceCommandSection_Default,
	ibInterfaceCommandSection_Create,    ibInterfaceCommandSection_Report, ibInterfaceCommandSection_Service,
};

// The panel a platform group belongs to — said once, so the property list, the section page and the form's
// command picker cannot put Create in two different panels.
inline ibCommandGroupCategory ibCommandGroupCategoryOf(ibInterfaceCommandSection area) {
	return (area == ibInterfaceCommandSection_Important || area == ibInterfaceCommandSection_Default)
		? ibCommandGroupCategory_Navigation : ibCommandGroupCategory_Actions;
}

// What a person reads over a platform group — the area enumeration's own caption, the one place it is
// written.
BACKEND_API wxString ibCommandGroupCaption(ibInterfaceCommandSection area);
// …the panel's caption, and the LABEL a group is offered under: «panel.group». It is how a person reads where a
// command will stand, the word the Group list shows, and the word metadata_set takes back — one spelling, so the
// tools and the inspector cannot name one group two ways.
BACKEND_API wxString ibCommandGroupCategoryCaption(ibCommandGroupCategory category);
BACKEND_API wxString ibCommandGroupLabel(ibInterfaceCommandSection area);
// …and a platform group's picture, drawn once (commandGroup/* in tools/pictures/render.js). 16 x 16.
BACKEND_API wxBitmap ibCommandGroupPicture(ibInterfaceCommandSection area);

//********************************************************************************************
//*                         Command group — a place in the command interface                 *
//********************************************************************************************

// ⭐⭐ A GROUP HOLDS NOTHING. It is a PLACE a command is filed under, beside the platform's own groups: a
// command names it in its Group property, the way it names Normal or Reports. A section shows the commands it
// includes under that group's caption, in the group's panel; a form command bar shows the group as one button
// with a submenu. So commands of different objects can stand together under one caption, and none of them
// moves in the tree to get there.
//
// NOT the command that holds commands (a command with sub-commands, which a button opens as a dropdown).
// That one OWNS its children; this one is only pointed at.
class BACKEND_API ibValueMetaObjectCommandGroup : public ibValueMetaObject {
public:

	ibValueMetaObjectCommandGroup(const wxString& name = wxEmptyString, const wxString& synonym = wxEmptyString, const wxString& comment = wxEmptyString);

	ibCommandGroupCategory GetCategory() const { return m_propertyCategory->GetValueAsEnum(); }
	// «panel.synonym» — see ibCommandGroupLabel.
	wxString GetLabel() const { return ibCommandGroupCategoryCaption(GetCategory()) + wxT(".") + GetSynonym(); }

	bool IsEmptyPicture() const { return m_propertyPicture->IsEmptyProperty(); }
	wxBitmap GetPictureAsBitmap() const { return m_propertyPicture->GetValueAsBitmap(); }
	// What a person reads when the pointer rests on the group — its own tooltip, the synonym when it has none.
	wxString GetToolTip() const {
		const wxString tooltip = m_propertyTooltip->GetValueAsTranslateString();
		return tooltip.IsEmpty() ? GetSynonym() : tooltip;
	}

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

protected:

	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

private:

	ibPropertyCategory* m_categoryGroup = ibPropertyObject::CreatePropertyCategory(wxT("CommandGroup"), _("Command group"));
	ibPropertyEnum<ibValueEnumCommandGroupCategory>* m_propertyCategory = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumCommandGroupCategory>>(m_categoryGroup, wxT("Category"), _("Category"),
		_("Where the group is shown: the navigation panel of a section, beside Important and Normal; its actions panel, beside Create, Reports and Service; or the command bar of a form, as one button that opens a submenu of the group's commands - the object's own, and the common commands typed for that object."),
		ibCommandGroupCategory_Navigation);
	ibPropertyPicture* m_propertyPicture = ibPropertyObject::CreateProperty<ibPropertyPicture>(m_categoryGroup, wxT("Picture"), _("Picture"),
		_("The icon beside the group's caption: at its heading on a section page, and on its button on a form command bar. Empty: caption only."));
	ibPropertyTString* m_propertyTooltip = ibPropertyObject::CreateProperty<ibPropertyTString>(m_categoryGroup, wxT("Tooltip"), _("Tooltip"),
		_("The hint shown when the pointer rests on the group's heading or on its button, one text per language. Empty: the synonym is shown."), wxEmptyString);
};

#endif
