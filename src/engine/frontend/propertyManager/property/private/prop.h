#ifndef __PROP_PRIVATE_H__
#define __PROP_PRIVATE_H__

// wxPGFlags and friends. This used to arrive from the BACKEND — typeconv.h included
// propgrid and backend_core.h included typeconv.h — so the frontend's own grid header
// leaned on the core to hand it the grid library. The backend is propgrid-free now, so
// the dependency is declared where it actually lives.
#include <wx/propgrid/propgrid.h>

template <typename T1, typename T2>
static inline void AssocProperty(T2&& callback) { 
	callback = [](auto ...arg) -> wxObject* { return new T1(arg...); }; 
}

#define ibPG_IMPLEMENT_PROPERTY_CALLBACK(name, value) \
	AssocProperty<name>(value); \

// Flags used only internally

// wxBoolProperty, wxFlagsProperty specific flags
constexpr wxPGFlags wxPGPropertyFlags_UseCheckBox = wxPGFlags::Reserved_1;
// DCC = Double Click Cycles
constexpr wxPGFlags wxPGPropertyFlags_UseDCC = wxPGFlags::Reserved_2;

// wxStringProperty flag
constexpr wxPGFlags wxPGPropertyFlags_Password = wxPGFlags::Reserved_2;

// wxColourProperty flag - if set, then match from list is searched for a custom colour.
constexpr wxPGFlags wxPGPropertyFlags_TranslateCustom = wxPGFlags::Reserved_1;

// wxCursorProperty, wxSystemColourProperty - If set, then selection of choices is static
// and should not be changed (i.e. returns nullptr in GetPropertyChoices).
constexpr wxPGFlags wxPGPropertyFlags_StaticChoices = wxPGFlags::Reserved_1;

// wxSystemColourProperty - wxEnumProperty based classes cannot use wxPGFlags::Reserved_1
constexpr wxPGFlags wxPGPropertyFlags_HideCustomColour = wxPGFlags::Reserved_2;
constexpr wxPGFlags wxPGPropertyFlags_ColourHasAlpha = wxPGFlags::Reserved_3;

// wxFileProperty - if set, full path is shown in wxFileProperty.
constexpr wxPGFlags wxPGPropertyFlags_ShowFullFileName = wxPGFlags::Reserved_1;

// wxLongStringProperty - flag used to mark that edit button
// should be enabled even in the read-only mode.
constexpr wxPGFlags wxPGPropertyFlags_ActiveButton = wxPGFlags::Reserved_3;


#endif 