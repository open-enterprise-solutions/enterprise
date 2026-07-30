#ifndef __PICTURE_PREDEFINED_H__
#define __PICTURE_PREDEFINED_H__

#include "backend/backend_core.h"

//*******************************************************************************
//*                          define common pic							        *
//*******************************************************************************

//COMMON PICTURES
constexpr ibPictureID g_picStructureCLSID = picture_to_clsid("PC_STRCT");
constexpr ibPictureID g_picErrorCLSID = picture_to_clsid("PC_ERROR");

constexpr ibPictureID g_picCloseFormCLSID = picture_to_clsid("PC_CLOSE");
constexpr ibPictureID g_picUpdateFormCLSID = picture_to_clsid("PC_REFRE");
constexpr ibPictureID g_picHelpFormCLSID = picture_to_clsid("PC_HELP");

constexpr ibPictureID g_picChangeFormCLSID = picture_to_clsid("PC_CHAGF");

constexpr ibPictureID g_picAddCLSID = picture_to_clsid("PC_ADDVL");
constexpr ibPictureID g_picEditCLSID = picture_to_clsid("PC_EDITV");
constexpr ibPictureID g_picCopyCLSID = picture_to_clsid("PC_COPYV");
constexpr ibPictureID g_picDeleteCLSID = picture_to_clsid("PC_DELVL");

constexpr ibPictureID g_picAddFolderCLSID = picture_to_clsid("PC_ADDFV");
constexpr ibPictureID g_picSelectCLSID = picture_to_clsid("PC_SELVL");

constexpr ibPictureID g_picFilterCLSID = picture_to_clsid("PC_FLTER");
constexpr ibPictureID g_picFilterSetCLSID = picture_to_clsid("PC_FLTES");
constexpr ibPictureID g_picFilterClearCLSID = picture_to_clsid("PC_FLTEC");

constexpr ibPictureID g_picCloneCLSID = picture_to_clsid("PC_CLONE");
constexpr ibPictureID g_picSaveCLSID = picture_to_clsid("PC_SAVE");
constexpr ibPictureID g_picPostCLSID = picture_to_clsid("PC_POST");
constexpr ibPictureID g_picMarkAsDeleteCLSID = picture_to_clsid("PC_MDEL");
constexpr ibPictureID g_picGenerateCLSID = picture_to_clsid("PC_GENTE");
constexpr ibPictureID g_picPrintCLSID = picture_to_clsid("PC_PRINT");
constexpr ibPictureID g_picHierarchyCLSID = picture_to_clsid("PC_HRCHY");

constexpr ibPictureID g_picUserCLSID = picture_to_clsid("PC_USER");
constexpr ibPictureID g_picUserActiveCLSID = picture_to_clsid("PC_USRAC");
constexpr ibPictureID g_picUserListCLSID = picture_to_clsid("PC_USRLS");

constexpr ibPictureID g_picAuthenticationCLSID = picture_to_clsid("PC_ATTON");

// The start page — the tab, the designer's workspace editor and its menu item.
// Art: icons8 ("switch host"), downscaled to 16px — see the note in picturePredefined.cpp.
constexpr ibPictureID g_picHomePageCLSID = picture_to_clsid("PC_HOMEP");

#endif 
