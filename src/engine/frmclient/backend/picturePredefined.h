#ifndef _FRMCLIENT_BACKEND_PICTURE_PREDEFINED_H__
#define _FRMCLIENT_BACKEND_PICTURE_PREDEFINED_H__

#include "frmclient/backend/backend_core.h"
#include "core/clsid.h"
#include "frmclient/win/picture.h"   // ibPictureID

// THE ENGINE'S OWN PICTURES, by number — its backend/picturePredefined.h, whole. The picture itself is the server's:
// it is sent with what shows it, and kept by its number (ibBackendPicture).

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

// The composer's four verbs, and saving a setting.
constexpr ibPictureID g_picComposeCLSID = picture_to_clsid("PC_CMPSE");
constexpr ibPictureID g_picComposerSettingsCLSID = picture_to_clsid("PC_CMSET");
constexpr ibPictureID g_picVariantsCLSID = picture_to_clsid("PC_VARNT");
constexpr ibPictureID g_picRestoreSettingsCLSID = picture_to_clsid("PC_RSTOR");
constexpr ibPictureID g_picSaveSettingsCLSID = picture_to_clsid("PC_SVSET");

constexpr ibPictureID g_picFilterCLSID = picture_to_clsid("PC_FLTER");
constexpr ibPictureID g_picFilterSetCLSID = picture_to_clsid("PC_FLTES");
constexpr ibPictureID g_picFilterClearCLSID = picture_to_clsid("PC_FLTEC");

// The row-order verbs.
constexpr ibPictureID g_picMoveUpCLSID = picture_to_clsid("PC_MVEUP");
constexpr ibPictureID g_picMoveDownCLSID = picture_to_clsid("PC_MVEDN");
constexpr ibPictureID g_picSortAscCLSID = picture_to_clsid("PC_SRTAS");
constexpr ibPictureID g_picSortDescCLSID = picture_to_clsid("PC_SRTDS");

constexpr ibPictureID g_picCloneCLSID = picture_to_clsid("PC_CLONE");
constexpr ibPictureID g_picSaveCLSID = picture_to_clsid("PC_SAVE");
constexpr ibPictureID g_picPostCLSID = picture_to_clsid("PC_POST");
constexpr ibPictureID g_picMarkAsDeleteCLSID = picture_to_clsid("PC_MDEL");
constexpr ibPictureID g_picGenerateCLSID = picture_to_clsid("PC_GENTE");
constexpr ibPictureID g_picExecuteJobCLSID = picture_to_clsid("PC_JOBRN");
constexpr ibPictureID g_picPrintCLSID = picture_to_clsid("PC_PRINT");
constexpr ibPictureID g_picHierarchyCLSID = picture_to_clsid("PC_HRCHY");
constexpr ibPictureID g_picEventCLSID = picture_to_clsid("PC_EVENT");

constexpr ibPictureID g_picUserCLSID = picture_to_clsid("PC_USER");
constexpr ibPictureID g_picUserActiveCLSID = picture_to_clsid("PC_USRAC");
constexpr ibPictureID g_picUserListCLSID = picture_to_clsid("PC_USRLS");

constexpr ibPictureID g_picAuthenticationCLSID = picture_to_clsid("PC_ATTON");

constexpr ibPictureID g_picHomePageCLSID = picture_to_clsid("PC_HOMEP");
constexpr ibPictureID g_picAssistantCLSID = picture_to_clsid("PC_ASSIS");

// The row's state — what a list's first column shows at its left.
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
