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

// THE COMPOSER'S FOUR VERBS, each with a picture of its own. Three of them used to borrow: compose took
// the document's Generate, settings took Structure, and VARIANTS AND RESTORE SHARED THE SELECT TICK —
// two different questions under one picture, which is what a bar of ticks reads as (Max, 2026-09-25:
// "it looks unrelated"). Borrowing is what made repainting impossible: Select is pressed in a dozen
// other places and means "take this one".
//
// Art: drawn in the debug toolbar's manner — flat solid shapes, no keyline, one colour each. Compose is
// the green run-triangle and the rows it produces; settings is a gear; variants are three arrangements
// with one taken; restore is the arrow OUT of the store, the mirror of Save's arrow into it, told apart
// by direction exactly as the debugger's step-into and step-out are.
constexpr ibPictureID g_picComposeCLSID = picture_to_clsid("PC_CMPSE");
constexpr ibPictureID g_picComposerSettingsCLSID = picture_to_clsid("PC_CMSET");
constexpr ibPictureID g_picVariantsCLSID = picture_to_clsid("PC_VARNT");
constexpr ibPictureID g_picRestoreSettingsCLSID = picture_to_clsid("PC_RSTOR");
// …and its twin. Saving a SETTING is not saving the object — that one is g_picSaveCLSID and is pressed
// on every form there is, so it could not be repainted into half of a pair without changing what Save
// looks like everywhere. Here the two are one picture told apart by the arrow's direction, the way the
// debugger tells step-into from step-out.
constexpr ibPictureID g_picSaveSettingsCLSID = picture_to_clsid("PC_SVSET");

constexpr ibPictureID g_picFilterCLSID = picture_to_clsid("PC_FLTER");
constexpr ibPictureID g_picFilterSetCLSID = picture_to_clsid("PC_FLTES");
constexpr ibPictureID g_picFilterClearCLSID = picture_to_clsid("PC_FLTEC");

// THE ROW-ORDER VERBS — move a row by hand, or order every row by one column. Only a table that OWNS
// its rows has them (a RAM table); a cursor's order comes from the read, and there is nothing to move.
// Art: the two arrows are the frontend's own 32px block arrows downscaled to 16; the two sort icons are
// drawn in the same two colours the 32px sort art uses.
constexpr ibPictureID g_picMoveUpCLSID = picture_to_clsid("PC_MVEUP");
constexpr ibPictureID g_picMoveDownCLSID = picture_to_clsid("PC_MVEDN");
constexpr ibPictureID g_picSortAscCLSID = picture_to_clsid("PC_SRTAS");
constexpr ibPictureID g_picSortDescCLSID = picture_to_clsid("PC_SRTDS");

constexpr ibPictureID g_picCloneCLSID = picture_to_clsid("PC_CLONE");
constexpr ibPictureID g_picSaveCLSID = picture_to_clsid("PC_SAVE");
constexpr ibPictureID g_picPostCLSID = picture_to_clsid("PC_POST");
constexpr ibPictureID g_picMarkAsDeleteCLSID = picture_to_clsid("PC_MDEL");
constexpr ibPictureID g_picGenerateCLSID = picture_to_clsid("PC_GENTE");
// RUN A SCHEDULED JOB NOW — an alarm clock, not the posting tick: what the button does is start
// unattended work ahead of its schedule, and borrowing Post's picture would say "this records
// something", which it does not.
constexpr ibPictureID g_picExecuteJobCLSID = picture_to_clsid("PC_JOBRN");
constexpr ibPictureID g_picPrintCLSID = picture_to_clsid("PC_PRINT");
constexpr ibPictureID g_picHierarchyCLSID = picture_to_clsid("PC_HRCHY");

constexpr ibPictureID g_picUserCLSID = picture_to_clsid("PC_USER");
constexpr ibPictureID g_picUserActiveCLSID = picture_to_clsid("PC_USRAC");
constexpr ibPictureID g_picUserListCLSID = picture_to_clsid("PC_USRLS");

constexpr ibPictureID g_picAuthenticationCLSID = picture_to_clsid("PC_ATTON");

// The start page — the tab, the designer's workspace editor and its menu item.
// Art: icons8 ("switch host"), downscaled to 16px — see the note in picturePredefined.cpp.
constexpr ibPictureID g_picHomePageCLSID = picture_to_clsid("PC_HOMEP");

// The assistant — its tab and its menu item. One window, one icon.
// Art: icons8 ("ai"), downscaled from 96px — see the note in picturePredefined.cpp.
constexpr ibPictureID g_picAssistantCLSID = picture_to_clsid("PC_ASSIS");

// THE ROW'S STATE — what a list's first column shows at its left (ibDataViewModel::GetRowPicture). Chosen by
// the row's own kind from its cells: a catalog's or a chart's record and its group (each marked for deletion or
// not), a document (posted, not, marked for deletion), a register's record (active, or the same greyed).
constexpr ibPictureID g_picRowItemCLSID = picture_to_clsid("PC_ROWITEM");
constexpr ibPictureID g_picRowItemDeletedCLSID = picture_to_clsid("PC_ROWITEMDEL");
constexpr ibPictureID g_picRowFolderCLSID = picture_to_clsid("PC_ROWFOLDER");
constexpr ibPictureID g_picRowFolderDeletedCLSID = picture_to_clsid("PC_ROWFOLDERDEL");
constexpr ibPictureID g_picRowDocumentCLSID = picture_to_clsid("PC_ROWDOC");
constexpr ibPictureID g_picRowDocumentPostedCLSID = picture_to_clsid("PC_ROWDOCPOST");
constexpr ibPictureID g_picRowDocumentDeletedCLSID = picture_to_clsid("PC_ROWDOCDEL");
constexpr ibPictureID g_picRowRecordCLSID = picture_to_clsid("PC_ROWREC");
constexpr ibPictureID g_picRowRecordInactiveCLSID = picture_to_clsid("PC_ROWRECOFF");

#endif 
