#ifndef __PROP_PRIVATE_H__
#define __PROP_PRIVATE_H__

// ibPGFlags and friends. This used to arrive from the BACKEND — typeconv.h included
// propgrid and backend_core.h included typeconv.h — so the frontend's own grid header
// leaned on the core to hand it the grid library. The backend is propgrid-free now, so
// the dependency is declared where it actually lives.
#include "deskgui/univ/propgrid/propgrid.h"

// AssocProperty / ibPG_IMPLEMENT_PROPERTY_CALLBACK lived here: they filled the backend's
// ms_property* slots by unpacking whatever arguments a slot happened to declare. Both are
// gone with the slots — a maker now takes the property itself and reads what it needs
// (private/propertyRegistry.h). What remains is what this header was always about: the
// grid flags below.

// Flags used only internally

// ibBoolProperty, ibFlagsProperty specific flags
constexpr ibPGFlags ibPGPropertyFlags_UseCheckBox = ibPGFlags::Reserved_1;
// DCC = Double Click Cycles
constexpr ibPGFlags ibPGPropertyFlags_UseDCC = ibPGFlags::Reserved_2;

// ibStringProperty flag
constexpr ibPGFlags ibPGPropertyFlags_Password = ibPGFlags::Reserved_2;

// ibColourProperty flag - if set, then match from list is searched for a custom colour.
constexpr ibPGFlags ibPGPropertyFlags_TranslateCustom = ibPGFlags::Reserved_1;

// ibCursorProperty, ibSystemColourProperty - If set, then selection of choices is static
// and should not be changed (i.e. returns nullptr in GetPropertyChoices).
constexpr ibPGFlags ibPGPropertyFlags_StaticChoices = ibPGFlags::Reserved_1;

// ibSystemColourProperty - ibEnumProperty based classes cannot use ibPGFlags::Reserved_1
constexpr ibPGFlags ibPGPropertyFlags_HideCustomColour = ibPGFlags::Reserved_2;
constexpr ibPGFlags ibPGPropertyFlags_ColourHasAlpha = ibPGFlags::Reserved_3;

// ibFileProperty - if set, full path is shown in ibFileProperty.
constexpr ibPGFlags ibPGPropertyFlags_ShowFullFileName = ibPGFlags::Reserved_1;

// ibLongStringProperty - flag used to mark that edit button
// should be enabled even in the read-only mode.
constexpr ibPGFlags ibPGPropertyFlags_ActiveButton = ibPGFlags::Reserved_3;


#endif 