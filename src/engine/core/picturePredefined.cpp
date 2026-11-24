#include "picturePredefined.h"
#include "core/pictureRegistry.h"   // RegisterPicture

#pragma region __predefined_svg__

static const ibString s_structure_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><path d='M3 1 H10 L14 5 V14 Q14 15 13 15 H3 Q2 15 2 14 V2 Q2 1 3 1 Z' fill='#5a7b95'/><path d='M10 1 V4.2 Q10 5 10.8 5 H14 Z' fill='#a8bac8'/><path d='M5.8 7.5 V12.6 H8 M5.8 10 H8' fill='none' stroke='#ffffff' stroke-width='1.1' stroke-linecap='butt' stroke-linejoin='round'/><rect x='4.2' y='5' width='3.4' height='2.6' rx='0.5' fill='#fcc419'/><rect x='8' y='8.9' width='3.6' height='2.2' rx='0.5' fill='#ffffff'/><rect x='8' y='11.6' width='3.6' height='2.2' rx='0.5' fill='#ffffff'/></svg>";

static const ibString s_error_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><circle cx='8' cy='8' r='7' fill='#e03131'/><rect x='4' y='7' width='8' height='2' rx='0.5' fill='#ffffff'/></svg>";

static const ibString s_closeForm_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><path d='M4 4 L12 12' stroke='#e03131' stroke-width='2.2' stroke-linecap='round' stroke-linejoin='round' fill='none'/><path d='M12 4 L4 12' stroke='#e03131' stroke-width='2.2' stroke-linecap='round' stroke-linejoin='round' fill='none'/></svg>";

static const ibString s_updateForm_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><path d='M13 7 A5 5 0 0 0 3.8 5.3' stroke='#2f9e44' stroke-width='1.8' stroke-linecap='round' stroke-linejoin='round' fill='none'/><path d='M3 2.8 L3.6 5.5 L6.3 5' stroke='#2f9e44' stroke-width='1.6' stroke-linecap='round' stroke-linejoin='round' fill='none'/><path d='M3 9 A5 5 0 0 0 12.2 10.7' stroke='#2f9e44' stroke-width='1.8' stroke-linecap='round' stroke-linejoin='round' fill='none'/><path d='M13 13.2 L12.4 10.5 L9.7 11' stroke='#2f9e44' stroke-width='1.6' stroke-linecap='round' stroke-linejoin='round' fill='none'/></svg>";

static const ibString s_helpForm_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><circle cx='8' cy='8' r='7' fill='#fab005'/><path d='M5.8 6 Q5.8 3.6 8 3.6 Q10.2 3.6 10.2 5.6 Q10.2 6.9 8.9 7.6 Q8 8.1 8 9.3' fill='none' stroke='#5c3d00' stroke-width='1.8' stroke-linecap='round' stroke-linejoin='round'/><circle cx='8' cy='12' r='1.2' fill='#5c3d00'/></svg>";

static const ibString s_changeForm_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><path d='M4 1.5 V14.5 M8 1.5 V14.5 M12 1.5 V14.5' fill='none' stroke='#5a7b95' stroke-width='1.2' stroke-linecap='butt' stroke-linejoin='round'/><rect x='2.3' y='8.6' width='3.4' height='3' rx='0.8' fill='#2f9e44'/><rect x='6.3' y='3.6' width='3.4' height='3' rx='0.8' fill='#2f9e44'/><rect x='10.3' y='6.6' width='3.4' height='3' rx='0.8' fill='#2f9e44'/></svg>";

static const ibString s_addFolder_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><path d='M1 3.5 Q1 2 2.5 2 H5.8 L7.5 3.7 H13.5 Q15 3.7 15 5.2 V12.5 Q15 14 13.5 14 H2.5 Q1 14 1 12.5 Z' fill='#f08c00'/><path d='M1 6.5 H15 V12.5 Q15 14 13.5 14 H2.5 Q1 14 1 12.5 Z' fill='#fcc419'/><circle cx='12' cy='12' r='4.7' fill='#ffffff'/><circle cx='12' cy='12' r='3.9' fill='#2f9e44'/><path d='M12 9.9 V14.1 M9.9 12 H14.1' fill='none' stroke='#ffffff' stroke-width='1.5' stroke-linecap='butt' stroke-linejoin='round'/></svg>";

static const ibString s_add_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><circle cx='8' cy='8' r='6.5' fill='#2f9e44'/><path d='M8 4.5 V11.5' stroke='#ffffff' stroke-width='2' stroke-linecap='butt' stroke-linejoin='miter' fill='none'/><path d='M4.5 8 H11.5' stroke='#ffffff' stroke-width='2' stroke-linecap='butt' stroke-linejoin='miter' fill='none'/></svg>";

static const ibString s_copy_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><path d='M3 1 H8.5 L12 4.5 V14 Q12 15 11 15 H3 Q2 15 2 14 V2 Q2 1 3 1 Z' fill='#5a7b95'/><path d='M8.5 1 V3.7 Q8.5 4.5 9.3 4.5 H12 Z' fill='#a8bac8'/><path d='M4.5 7.5 H9 M4.5 10 H7.5' fill='none' stroke='#ffffff' stroke-width='1.3' stroke-linecap='round' stroke-linejoin='round'/><circle cx='12' cy='12' r='4.7' fill='#ffffff'/><circle cx='12' cy='12' r='3.9' fill='#2f9e44'/><path d='M12 9.9 V14.1 M9.9 12 H14.1' fill='none' stroke='#ffffff' stroke-width='1.5' stroke-linecap='butt' stroke-linejoin='round'/></svg>";

static const ibString s_edit_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><path d='M2.5 13.5 L3.2 10.3 L10.6 2.9 L13.1 5.4 L5.7 12.8 Z' fill='#e03131'/><path d='M10.6 2.9 L11.8 1.7 Q12.5 1 13.2 1.7 L14.3 2.8 Q15 3.5 14.3 4.2 L13.1 5.4 Z' fill='#ffa8a8'/><path d='M2.5 13.5 L3.2 10.3 L5.7 12.8 Z' fill='#ffc9c9'/><path d='M2.5 13.5 L2.8 12.1 L3.9 13.2 Z' fill='#5a7b95'/></svg>";

static const ibString s_delete_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><circle cx='8' cy='8' r='6.5' fill='#e03131'/><path d='M5.5 5.5 L10.5 10.5' stroke='#ffffff' stroke-width='2' stroke-linecap='round' stroke-linejoin='round' fill='none'/><path d='M10.5 5.5 L5.5 10.5' stroke='#ffffff' stroke-width='2' stroke-linecap='round' stroke-linejoin='round' fill='none'/></svg>";

static const ibString s_filter_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><path d='M1.2 2 H14.8 Q15.4 2 15 2.6 L9.8 8.6 V13.8 Q9.8 14.5 9.1 14.2 L6.6 12.9 Q6.2 12.7 6.2 12.2 V8.6 L1 2.6 Q0.6 2 1.2 2 Z' fill='#5a7b95'/><path d='M3 3.3 H13 L10.8 5.9 H5.2 Z' fill='#a5d8ff'/></svg>";

static const ibString s_filterSet_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><path d='M1.2 2 H14.8 Q15.4 2 15 2.6 L9.8 8.6 V13.8 Q9.8 14.5 9.1 14.2 L6.6 12.9 Q6.2 12.7 6.2 12.2 V8.6 L1 2.6 Q0.6 2 1.2 2 Z' fill='#5a7b95'/><path d='M3 3.3 H13 L10.8 5.9 H5.2 Z' fill='#a5d8ff'/><path d='M9 15.2 L9.4 13.1 L13.9 8.6 L15.6 10.3 L11.1 14.8 Z' fill='#5a7b95' stroke='#ffffff' stroke-width='1'/></svg>";

static const ibString s_filterClear_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><path d='M1.2 2 H14.8 Q15.4 2 15 2.6 L9.8 8.6 V13.8 Q9.8 14.5 9.1 14.2 L6.6 12.9 Q6.2 12.7 6.2 12.2 V8.6 L1 2.6 Q0.6 2 1.2 2 Z' fill='#5a7b95'/><path d='M3 3.3 H13 L10.8 5.9 H5.2 Z' fill='#a5d8ff'/><circle cx='12' cy='12' r='4.7' fill='#ffffff'/><circle cx='12' cy='12' r='3.9' fill='#e03131'/><path d='M10.4 10.4 L13.6 13.6 M13.6 10.4 L10.4 13.6' fill='none' stroke='#ffffff' stroke-width='1.4' stroke-linecap='round' stroke-linejoin='round'/></svg>";

static const ibString s_moveUp_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><path d='M6 14 V8 H3 L8 2.5 L13 8 H10 V14 Z' fill='#1c6fd1'/></svg>";

static const ibString s_moveDown_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><path d='M6 2 V8 H3 L8 13.5 L13 8 H10 V2 Z' fill='#1c6fd1'/></svg>";

static const ibString s_sortAsc_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><rect x='2' y='3' width='3' height='2' fill='#2f9e44'/><rect x='2' y='7' width='5' height='2' fill='#2f9e44'/><rect x='2' y='11' width='7' height='2' fill='#2f9e44'/><path d='M13 14 V4' stroke='#1c6fd1' stroke-width='2' stroke-linecap='butt' stroke-linejoin='miter' fill='none'/><path d='M10.6 5.6 L13 3.2 L15.4 5.6' stroke='#1c6fd1' stroke-width='1.8' stroke-linecap='round' stroke-linejoin='round' fill='none'/></svg>";

static const ibString s_sortDesc_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><rect x='2' y='3' width='7' height='2' fill='#2f9e44'/><rect x='2' y='7' width='5' height='2' fill='#2f9e44'/><rect x='2' y='11' width='3' height='2' fill='#2f9e44'/><path d='M13 2 V12' stroke='#1c6fd1' stroke-width='2' stroke-linecap='butt' stroke-linejoin='miter' fill='none'/><path d='M10.6 10.4 L13 12.8 L15.4 10.4' stroke='#1c6fd1' stroke-width='1.8' stroke-linecap='round' stroke-linejoin='round' fill='none'/></svg>";

static const ibString s_clone_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><path d='M6 1 H12 L15 4 V10.5 Q15 11.5 14 11.5 H6 Q5 11.5 5 10.5 V2 Q5 1 6 1 Z' fill='#a8bac8'/><path d='M12 1 V3.2 Q12 4 12.8 4 H15 Z' fill='#ffffff'/><path d='M2 4.5 H8 L11 7.5 V14 Q11 15 10 15 H2 Q1 15 1 14 V5.5 Q1 4.5 2 4.5 Z' fill='#5a7b95'/><path d='M8 4.5 V6.7 Q8 7.5 8.8 7.5 H11 Z' fill='#a8bac8'/><path d='M3.4 10 H8.6 M3.4 12.4 H7.4' fill='none' stroke='#ffffff' stroke-width='1.2' stroke-linecap='round' stroke-linejoin='round'/></svg>";

static const ibString s_save_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><path d='M1 10.5 H15 V13.5 Q15 15 13.5 15 H2.5 Q1 15 1 13.5 Z' fill='#5a7b95'/><circle cx='12.4' cy='12.8' r='1' fill='#2f9e44'/><path d='M6.4 0.8 H9.6 V4.8 H12.6 L8 9.4 L3.4 4.8 H6.4 Z' fill='#1c6fd1'/></svg>";

static const ibString s_post_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><circle cx='8' cy='8' r='6.5' fill='#2f9e44'/><path d='M4.8 8.2 L7 10.4 L11.4 5.6' stroke='#ffffff' stroke-width='2' stroke-linecap='round' stroke-linejoin='round' fill='none'/></svg>";

static const ibString s_markAsDelete_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><path d='M3.5 3.5 L12.5 12.5' stroke='#e03131' stroke-width='2.6' stroke-linecap='round' stroke-linejoin='round' fill='none'/><path d='M12.5 3.5 L3.5 12.5' stroke='#e03131' stroke-width='2.6' stroke-linecap='round' stroke-linejoin='round' fill='none'/></svg>";

static const ibString s_generate_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><path d='M2 1 H7.5 L11 4.5 V13 Q11 14 10 14 H2 Q1 14 1 13 V2 Q1 1 2 1 Z' fill='#5a7b95'/><path d='M7.5 1 V3.7 Q7.5 4.5 8.3 4.5 H11 Z' fill='#a8bac8'/><path d='M3.5 7 H8.5 M3.5 9.5 H7' fill='none' stroke='#ffffff' stroke-width='1.3' stroke-linecap='round' stroke-linejoin='round'/><circle cx='12' cy='12' r='4.7' fill='#ffffff'/><circle cx='12' cy='12' r='3.9' fill='#2f9e44'/><path d='M10 12 H13.6 M12 10.2 L13.9 12 L12 13.8' fill='none' stroke='#ffffff' stroke-width='1.4' stroke-linecap='round' stroke-linejoin='round'/></svg>";
// An alarm clock — "start this unattended work now". Deliberately not Post's tick: a job run
// records nothing, and a picture that says otherwise is read before the caption is.
static const ibString s_executeJob_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><path d='M2 4.6 L4 2.8' fill='none' stroke='#e03131' stroke-width='1.8' stroke-linecap='round' stroke-linejoin='round'/><path d='M11.6 4.6 L9.6 2.8' fill='none' stroke='#e03131' stroke-width='1.8' stroke-linecap='round' stroke-linejoin='round'/><circle cx='6.8' cy='8.8' r='5' fill='#e03131'/><path d='M6.8 6.2 V8.8 H9' fill='none' stroke='#ffffff' stroke-width='1.5' stroke-linecap='round' stroke-linejoin='round'/><circle cx='12' cy='12' r='4.7' fill='#ffffff'/><circle cx='12' cy='12' r='3.9' fill='#2f9e44'/><path d='M10.8 10 L14.1 12 L10.8 14 Z' fill='#ffffff'/></svg>";

static const ibString s_user_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><circle cx='8' cy='4.4' r='2.6' fill='#1c6fd1'/><path d='M3.8 12 Q3.8 7.6 8 7.6 Q12.2 7.6 12.2 12 Z' fill='#1c6fd1'/></svg>";

static const ibString s_activeUser_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><circle cx='7' cy='4.4' r='2.6' fill='#1c6fd1'/><path d='M2.8 12 Q2.8 7.6 7 7.6 Q11.2 7.6 11.2 12 Z' fill='#1c6fd1'/><circle cx='12.4' cy='12.4' r='3.2' fill='#ffffff'/><circle cx='12.4' cy='12.4' r='2.4' fill='#2f9e44'/></svg>";

static const ibString s_userList_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><circle cx='10.5' cy='3.4' r='2.6' fill='#a8bac8'/><path d='M6.3 11 Q6.3 6.6 10.5 6.6 Q14.7 6.6 14.7 11 Z' fill='#a8bac8'/><circle cx='6' cy='6.4' r='2.6' fill='#1c6fd1'/><path d='M1.8 14 Q1.8 9.6 6 9.6 Q10.2 9.6 10.2 14 Z' fill='#1c6fd1'/></svg>";

static const ibString s_authentication_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><path d='M8 1 L14 3.2 V8 Q14 12.4 8 15 Q2 12.4 2 8 V3.2 Z' fill='#7048e8'/><circle cx='8' cy='6.8' r='1.6' fill='#ffffff'/><path d='M8 8 V11' fill='none' stroke='#ffffff' stroke-width='1.6' stroke-linecap='butt' stroke-linejoin='round'/></svg>";

static const ibString s_print_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><rect x='4' y='1' width='8' height='4.5' rx='0.8' fill='#a8bac8'/><rect x='1' y='5' width='14' height='7' rx='1.5' fill='#5a7b95'/><rect x='4' y='9.5' width='8' height='5.5' rx='0.8' fill='#a8bac8'/><path d='M5.8 12.2 H10.2' fill='none' stroke='#5a7b95' stroke-width='1.1' stroke-linecap='butt' stroke-linejoin='round'/><circle cx='12.4' cy='7.4' r='0.9' fill='#8ce99a'/></svg>";

static const ibString s_hierarchy_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><path d='M4 5 V12.5 H9.5 M4 8 H9.5' fill='none' stroke='#5a7b95' stroke-width='1.3' stroke-linecap='butt' stroke-linejoin='round'/><rect x='1' y='1' width='6' height='4.5' rx='1' fill='#f08c00'/><rect x='9' y='6' width='6' height='4' rx='1' fill='#5a7b95'/><rect x='9' y='10.8' width='6' height='4' rx='1' fill='#5a7b95'/></svg>";

/* A lightning bolt, amber: an event */
static const ibString s_event_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><path d='M9.8 0.8 L3 9.2 H7.4 L5.8 15.2 L13 6.6 H8.6 Z' fill='#f08c00'/></svg>";

/* The start page (home page workspace): a house. Drawn for this platform - no longer icons8's "switch host", so
   that credit no longer applies here. */
static const ibString s_homePage_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><path d='M8 1.2 L15 7.6 Q15.4 8.2 14.7 8.2 H13 V14 Q13 15 12 15 H4 Q3 15 3 14 V8.2 H1.3 Q0.6 8.2 1 7.6 Z' fill='#1c6fd1'/><rect x='6.5' y='9.5' width='3' height='5.5' rx='0.6' fill='#ffffff'/></svg>";

/* The assistant (its tab and its menu item): sparkles. Drawn for this platform - no longer icons8's "ai". */
static const ibString s_assistant_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><path d='M7 1 L8.4 5.6 L13 7 L8.4 8.4 L7 13 L5.6 8.4 L1 7 L5.6 5.6 Z' fill='#1c6fd1'/><path d='M12.5 9.5 L13.2 11.8 L15.5 12.5 L13.2 13.2 L12.5 15.5 L11.8 13.2 L9.5 12.5 L11.8 11.8 Z' fill='#7048e8'/></svg>";

/* THE ROW'S STATE, drawn at the left of a list's first column (ibDataViewModel::GetRowPicture): a catalog's record
   and its group, each also marked for deletion; a document, posted, marked for deletion; a register's record - one
   drawing, greyed below for a record that is not active. */
static const ibString s_rowItem_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><rect x='1' y='3' width='14' height='10' rx='1.8' fill='#f76707'/><path d='M4 6.5 H12 M4 9.5 H9' fill='none' stroke='#ffffff' stroke-width='1.3' stroke-linecap='round' stroke-linejoin='round'/></svg>";
static const ibString s_rowItemDeleted_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><rect x='1' y='3' width='14' height='10' rx='1.8' fill='#f76707'/><path d='M4 6.5 H12 M4 9.5 H7' fill='none' stroke='#ffffff' stroke-width='1.3' stroke-linecap='round' stroke-linejoin='round'/><circle cx='12' cy='12' r='4.7' fill='#ffffff'/><circle cx='12' cy='12' r='3.9' fill='#e03131'/><path d='M10.4 10.4 L13.6 13.6 M13.6 10.4 L10.4 13.6' fill='none' stroke='#ffffff' stroke-width='1.4' stroke-linecap='round' stroke-linejoin='round'/></svg>";
static const ibString s_rowFolder_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><path d='M1 3.5 Q1 2 2.5 2 H5.8 L7.5 3.7 H13.5 Q15 3.7 15 5.2 V12.5 Q15 14 13.5 14 H2.5 Q1 14 1 12.5 Z' fill='#f08c00'/><path d='M1 6.5 H15 V12.5 Q15 14 13.5 14 H2.5 Q1 14 1 12.5 Z' fill='#fcc419'/></svg>";
static const ibString s_rowFolderDeleted_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><path d='M1 3.5 Q1 2 2.5 2 H5.8 L7.5 3.7 H13.5 Q15 3.7 15 5.2 V12.5 Q15 14 13.5 14 H2.5 Q1 14 1 12.5 Z' fill='#f08c00'/><path d='M1 6.5 H15 V12.5 Q15 14 13.5 14 H2.5 Q1 14 1 12.5 Z' fill='#fcc419'/><circle cx='12' cy='12' r='4.7' fill='#ffffff'/><circle cx='12' cy='12' r='3.9' fill='#e03131'/><path d='M10.4 10.4 L13.6 13.6 M13.6 10.4 L10.4 13.6' fill='none' stroke='#ffffff' stroke-width='1.4' stroke-linecap='round' stroke-linejoin='round'/></svg>";
static const ibString s_rowDocument_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><path d='M3 1 H10 L14 5 V14 Q14 15 13 15 H3 Q2 15 2 14 V2 Q2 1 3 1 Z' fill='#1c6fd1'/><path d='M10 1 V4.2 Q10 5 10.8 5 H14 Z' fill='#a5d8ff'/><path d='M4.5 7.5 H11 M4.5 10 H11 M4.5 12.5 H9' fill='none' stroke='#ffffff' stroke-width='1.3' stroke-linecap='round' stroke-linejoin='round'/></svg>";
static const ibString s_rowDocumentPosted_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><path d='M3 1 H10 L14 5 V14 Q14 15 13 15 H3 Q2 15 2 14 V2 Q2 1 3 1 Z' fill='#1c6fd1'/><path d='M10 1 V4.2 Q10 5 10.8 5 H14 Z' fill='#a5d8ff'/><path d='M4.5 7.5 H11 M4.5 10 H9.5' fill='none' stroke='#ffffff' stroke-width='1.3' stroke-linecap='round' stroke-linejoin='round'/><circle cx='12' cy='12' r='4.7' fill='#ffffff'/><circle cx='12' cy='12' r='3.9' fill='#2f9e44'/><path d='M10 12.1 L11.4 13.5 L14 10.7' fill='none' stroke='#ffffff' stroke-width='1.4' stroke-linecap='round' stroke-linejoin='round'/></svg>";
static const ibString s_rowDocumentDeleted_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><path d='M3 1 H10 L14 5 V14 Q14 15 13 15 H3 Q2 15 2 14 V2 Q2 1 3 1 Z' fill='#1c6fd1'/><path d='M10 1 V4.2 Q10 5 10.8 5 H14 Z' fill='#a5d8ff'/><path d='M4.5 7.5 H11 M4.5 10 H9.5' fill='none' stroke='#ffffff' stroke-width='1.3' stroke-linecap='round' stroke-linejoin='round'/><circle cx='12' cy='12' r='4.7' fill='#ffffff'/><circle cx='12' cy='12' r='3.9' fill='#e03131'/><path d='M10.4 10.4 L13.6 13.6 M13.6 10.4 L10.4 13.6' fill='none' stroke='#ffffff' stroke-width='1.4' stroke-linecap='round' stroke-linejoin='round'/></svg>";
static const ibString s_rowRecord_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><rect x='1' y='4' width='14' height='8' rx='1.5' fill='#5a7b95'/><path d='M5.6 4 V12 M10.4 4 V12' fill='none' stroke='#ffffff' stroke-width='1.2' stroke-linecap='butt' stroke-linejoin='round'/></svg>";
static const ibString s_rowRecordInactive_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><rect x='1' y='4' width='14' height='8' rx='1.5' fill='#a8bac8'/><path d='M5.6 4 V12 M10.4 4 V12' fill='none' stroke='#ffffff' stroke-width='1.2' stroke-linecap='butt' stroke-linejoin='round'/></svg>";

// THE COMPOSER'S OWN — the composition, its settings, its variants, the settings restored. The settings' gear is the
// Service group's as well; the settings are saved as anything is (Save).
static const ibString s_compose_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><path d='M1.5 2.5 Q1.5 1.6 2.3 2 L9.3 7.3 Q10 8 9.3 8.7 L2.3 14 Q1.5 14.4 1.5 13.5 Z' fill='#2f9e44'/><rect x='11' y='2.5' width='4' height='2.6' rx='1' fill='#5a7b95'/><rect x='11' y='6.7' width='4' height='2.6' rx='1' fill='#5a7b95'/><rect x='11' y='10.9' width='4' height='2.6' rx='1' fill='#5a7b95'/></svg>";
static const ibString s_composerSettings_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><path d='M12.84 6.10 L15.06 6.60 L15.06 9.40 L12.84 9.90 L12.76 10.08 L13.99 12.00 L12.00 13.99 L10.08 12.76 L9.90 12.84 L9.40 15.06 L6.60 15.06 L6.10 12.84 L5.92 12.76 L4.00 13.99 L2.01 12.00 L3.24 10.08 L3.16 9.90 L0.94 9.40 L0.94 6.60 L3.16 6.10 L3.24 5.92 L2.01 4.00 L4.00 2.01 L5.92 3.24 L6.10 3.16 L6.60 0.94 L9.40 0.94 L9.90 3.16 L10.08 3.24 L12.00 2.01 L13.99 4.00 L12.76 5.92 Z' fill='#5a7b95'/><circle cx='8' cy='8' r='2.2' fill='#ffffff'/></svg>";
static const ibString s_variants_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><rect x='1.5' y='1.5' width='13' height='3.6' rx='1.2' fill='#a8bac8'/><rect x='1.5' y='6.2' width='13' height='3.6' rx='1.2' fill='#1c6fd1'/><rect x='1.5' y='10.9' width='13' height='3.6' rx='1.2' fill='#a8bac8'/></svg>";
static const ibString s_restoreSettings_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><path d='M1 10.5 H15 V13.5 Q15 15 13.5 15 H2.5 Q1 15 1 13.5 Z' fill='#5a7b95'/><circle cx='12.4' cy='12.8' r='1' fill='#2f9e44'/><path d='M8 0.8 L12.6 5.4 H9.6 V9.4 H6.4 V5.4 H3.4 Z' fill='#1c6fd1'/></svg>";

// THE COMMAND BAR AND ITS COMMAND — what the form editor shows the "Command interface" node and its commands with.
// The server has no toolbar class to answer it with: the picture is the engine's, the client's.
static const ibString s_commandBar_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><rect x='1' y='4' width='14' height='8' rx='1.5' fill='#5a7b95'/><rect x='2.6' y='5.8' width='4.4' height='4.4' rx='0.8' fill='#ffffff'/><rect x='8.2' y='5.8' width='5.4' height='4.4' rx='2.2' fill='#74c0fc'/></svg>";
static const ibString s_commandBarItem_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><rect x='1' y='1' width='10.5' height='10.5' rx='1.8' fill='#f76707'/><rect x='3.4' y='3.4' width='5.7' height='5.7' rx='0.8' fill='#ffc078'/><path d='M8.5 7.3 V15 L10.3 13.3 L11.6 15.8 L13.2 15 L12 12.6 H14.5 Z' fill='#5a7b95' stroke='#ffffff' stroke-width='1' stroke-linejoin='round'/></svg>";

// WHAT AN APPLICATION IS MADE OF, AS MORE THAN ONE PLACE SHOWS IT — the application, a report, a table, a translation,
// a value, a resource: a class's icon and what the desktop shows beside it, one drawing.
static const ibString s_application_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><circle cx='8' cy='8' r='7' fill='#74c0fc'/><circle cx='8' cy='8' r='4.6' fill='#fcc419'/></svg>";
static const ibString s_report_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><path d='M3.5 1 H10 L14 5 V13.5 Q14 15 12.5 15 H3.5 Q2 15 2 13.5 V2.5 Q2 1 3.5 1 Z' fill='#2f9e44'/><path d='M10 1 V3.8 Q10 5 11.2 5 H14 Z' fill='#8ce99a'/><rect x='4.5' y='10' width='1.8' height='3' fill='#ffffff'/><rect x='7.1' y='8' width='1.8' height='5' fill='#ffffff'/><rect x='9.7' y='6' width='1.8' height='7' fill='#ffffff'/></svg>";
static const ibString s_table_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><rect x='1' y='2' width='14' height='12' rx='1.5' fill='#1c6fd1'/><rect x='2' y='5' width='12' height='8' rx='0.5' fill='#a5d8ff'/><path d='M2 9 H14 M6 5 V13 M10 5 V13' fill='none' stroke='#1c6fd1' stroke-width='1' stroke-linecap='butt' stroke-linejoin='round'/></svg>";
static const ibString s_translation_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><path d='M2.5 1 H9.5 Q11 1 11 2.5 V7 Q11 8.5 9.5 8.5 H5.5 L3 10.5 V8.5 H2.5 Q1 8.5 1 7 V2.5 Q1 1 2.5 1 Z' fill='#a8bac8'/><path d='M6.5 5.5 H13.5 Q15 5.5 15 7 V11.5 Q15 13 13.5 13 H13 V15.2 L10.5 13 H6.5 Q5 13 5 11.5 V7 Q5 5.5 6.5 5.5 Z' fill='#1c6fd1'/><path d='M8 11.6 L10 6.9 L12 11.6' fill='none' stroke='#ffffff' stroke-width='1.3' stroke-linecap='round' stroke-linejoin='round'/><path d='M8.8 10 H11.2' fill='none' stroke='#ffffff' stroke-width='1.1' stroke-linecap='round' stroke-linejoin='round'/></svg>";
static const ibString s_value_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><rect x='1' y='5' width='6.5' height='6' rx='1.5' fill='#1c6fd1'/><rect x='8.5' y='5' width='6.5' height='6' rx='1.5' fill='#74c0fc'/></svg>";
static const ibString s_resource_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><rect x='1' y='4' width='14' height='8' rx='2' fill='#2f9e44'/><path d='M7.3 5.6 H3.6 L5.6 8 L3.6 10.4 H7.3' fill='none' stroke='#ffffff' stroke-width='1.3' stroke-linecap='round' stroke-linejoin='round'/><path d='M10 8 H12.5' fill='none' stroke='#ffffff' stroke-width='1.4' stroke-linecap='round' stroke-linejoin='round'/></svg>";

// THE PLATFORM'S OWN COMMAND GROUPS — each one picture: the Reports group is the report's own drawing (above), the
// Service group the composer's settings' — one act, one picture.
static const ibString s_commandGroupImportant_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><rect x='1' y='2' width='14' height='12' rx='1.5' fill='#5a7b95'/><rect x='3' y='4.5' width='10' height='3.4' rx='0.8' fill='#ffffff'/><path d='M3.5 10.8 H10' fill='none' stroke='#ffffff' stroke-width='1.3' stroke-linecap='round' stroke-linejoin='round'/></svg>";
static const ibString s_commandGroupNormal_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><rect x='1' y='2' width='14' height='12' rx='1.5' fill='#5a7b95'/><path d='M3.5 5.5 H12.5 M3.5 8 H12.5 M3.5 10.5 H9.5' fill='none' stroke='#ffffff' stroke-width='1.3' stroke-linecap='round' stroke-linejoin='round'/></svg>";
static const ibString s_commandGroupCreate_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><path d='M3.5 1 H10 L14 5 V13.5 Q14 15 12.5 15 H3.5 Q2 15 2 13.5 V2.5 Q2 1 3.5 1 Z' fill='#5a7b95'/><path d='M10 1 V3.8 Q10 5 11.2 5 H14 Z' fill='#a8bac8'/><path d='M4.5 7.5 H9 M4.5 10 H7' fill='none' stroke='#ffffff' stroke-width='1.3' stroke-linecap='round' stroke-linejoin='round'/><circle cx='12' cy='12' r='4.7' fill='#ffffff'/><circle cx='12' cy='12' r='3.9' fill='#2f9e44'/><path d='M12 9.9 V14.1 M9.9 12 H14.1' fill='none' stroke='#ffffff' stroke-width='1.5' stroke-linecap='butt' stroke-linejoin='round'/></svg>";
static const ibString s_new_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><path d='M3 1 H8.5 L12 4.5 V14 Q12 15 11 15 H3 Q2 15 2 14 V2 Q2 1 3 1 Z' fill='#5a7b95'/><path d='M8.5 1 V3.7 Q8.5 4.5 9.3 4.5 H12 Z' fill='#a8bac8'/><path d='M4.5 8 H9.5 M4.5 10.5 H8' fill='none' stroke='#ffffff' stroke-width='1.3' stroke-linecap='round' stroke-linejoin='round'/><path d='M12.8 0.4 L13.8 3 L16 3.9 L13.8 4.8 L12.8 7.4 L11.8 4.8 L9.6 3.9 L11.8 3 Z' fill='#f08c00' stroke='#ffffff' stroke-width='0.7'/></svg>";
static const ibString s_open_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><path d='M1 3.5 Q1 2 2.5 2 H5.8 L7.5 3.7 H12 Q13.5 3.7 13.5 5.2 V6.5 H1 Z' fill='#1c6fd1'/><path d='M1 6.5 V12.8 Q1 14 2.2 14 H12.4 L15.3 7.6 Q15.6 6.5 14.5 6.5 Z' fill='#1c6fd1'/><path d='M2.2 14 L4.7 8.2 Q5 7.6 5.7 7.6 H15 L12.4 14 Z' fill='#74c0fc'/></svg>";
static const ibString s_saveAs_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><path d='M1 10.5 H15 V13.5 Q15 15 13.5 15 H2.5 Q1 15 1 13.5 Z' fill='#5a7b95'/><circle cx='12.4' cy='12.8' r='1' fill='#2f9e44'/><path d='M4.9 0.8 H8.1 V4.8 H11.1 L6.5 9.4 L1.9 4.8 H4.9 Z' fill='#1c6fd1'/><path d='M10.2 8.6 L10.5 7 L14 3.5 L15.3 4.8 L11.8 8.3 Z' fill='#e03131'/></svg>";
static const ibString s_quit_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><rect x='1' y='1' width='8.5' height='14' rx='1.2' fill='#5a7b95'/><circle cx='7.3' cy='8.6' r='0.9' fill='#ffffff'/><path d='M8 8 H14.6' fill='none' stroke='#e03131' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'/><path d='M12 5.4 L14.6 8 L12 10.6' fill='none' stroke='#e03131' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'/></svg>";
static const ibString s_undo_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><path d='M13 13 C13 7 9.5 5 4.6 5.4' stroke='#1c6fd1' stroke-width='2' stroke-linecap='round' stroke-linejoin='round' fill='none'/><path d='M7 2.6 L4.2 5.4 L7 8.2' stroke='#1c6fd1' stroke-width='2' stroke-linecap='round' stroke-linejoin='round' fill='none'/></svg>";
static const ibString s_redo_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><path d='M3 13 C3 7 6.5 5 11.4 5.4' stroke='#1c6fd1' stroke-width='2' stroke-linecap='round' stroke-linejoin='round' fill='none'/><path d='M9 2.6 L11.8 5.4 L9 8.2' stroke='#1c6fd1' stroke-width='2' stroke-linecap='round' stroke-linejoin='round' fill='none'/></svg>";
static const ibString s_cut_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><path d='M5.6 9.6 L11.2 1.6 M10.4 9.6 L4.8 1.6' fill='none' stroke='#5a7b95' stroke-width='1.7' stroke-linecap='round' stroke-linejoin='round'/><circle cx='4.4' cy='12' r='2.3' fill='none' stroke='#e03131' stroke-width='1.7'/><circle cx='11.6' cy='12' r='2.3' fill='none' stroke='#e03131' stroke-width='1.7'/></svg>";
static const ibString s_paste_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><rect x='2' y='2.2' width='12' height='12.8' rx='1.5' fill='#f08c00'/><rect x='4.2' y='5' width='7.6' height='8.4' rx='0.6' fill='#ffffff'/><path d='M5.8 7.6 H10.2 M5.8 9.6 H10.2 M5.8 11.6 H8.8' fill='none' stroke='#a8bac8' stroke-width='1' stroke-linecap='round' stroke-linejoin='round'/><rect x='5.2' y='0.8' width='5.6' height='3.4' rx='1' fill='#5a7b95'/></svg>";
static const ibString s_find_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><circle cx='6.5' cy='6.5' r='4.6' fill='#a5d8ff' stroke='#5a7b95' stroke-width='2'/><path d='M10 10 L14 14' fill='none' stroke='#5a7b95' stroke-width='2.8' stroke-linecap='round' stroke-linejoin='round'/></svg>";
static const ibString s_findCancel_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><circle cx='6.2' cy='6.2' r='4.3' fill='none' stroke='#5a7b95' stroke-width='2'/><path d='M9.4 9.4 L11 11' fill='none' stroke='#5a7b95' stroke-width='2.4' stroke-linecap='round' stroke-linejoin='round'/><circle cx='12' cy='12' r='4.4' fill='#ffffff'/><circle cx='12' cy='12' r='3.6' fill='#e03131'/><path d='M10.4 10.4 L13.6 13.6 M13.6 10.4 L10.4 13.6' fill='none' stroke='#ffffff' stroke-width='1.4' stroke-linecap='round' stroke-linejoin='round'/></svg>";
static const ibString s_goBack_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><path d='M14 6 H8 V3 L2.5 8 L8 13 V10 H14 Z' fill='#1c6fd1'/></svg>";
static const ibString s_goForward_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><path d='M2 6 H8 V3 L13.5 8 L8 13 V10 H2 Z' fill='#1c6fd1'/></svg>";
static const ibString s_goDirUp_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><path d='M1 3.5 Q1 2 2.5 2 H5.8 L7.5 3.7 H13.5 Q15 3.7 15 5.2 V12.5 Q15 14 13.5 14 H2.5 Q1 14 1 12.5 Z' fill='#1c6fd1'/><path d='M1 6.5 H15 V12.5 Q15 14 13.5 14 H2.5 Q1 14 1 12.5 Z' fill='#74c0fc'/><circle cx='12' cy='12' r='4.7' fill='#ffffff'/><circle cx='12' cy='12' r='3.9' fill='#2f9e44'/><path d='M12 9.6 L14.3 12 H12.9 V14.3 H11.1 V12 H9.7 Z' fill='#ffffff'/></svg>";
static const ibString s_plus_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><path d='M8 2.5 V13.5' stroke='#1c6fd1' stroke-width='2' stroke-linecap='butt' stroke-linejoin='miter' fill='none'/><path d='M2.5 8 H13.5' stroke='#1c6fd1' stroke-width='2' stroke-linecap='butt' stroke-linejoin='miter' fill='none'/></svg>";
static const ibString s_minus_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><path d='M2.5 8 H13.5' stroke='#1c6fd1' stroke-width='2' stroke-linecap='butt' stroke-linejoin='miter' fill='none'/></svg>";
static const ibString s_file_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><path d='M3 1 H10 L14 5 V14 Q14 15 13 15 H3 Q2 15 2 14 V2 Q2 1 3 1 Z' fill='#5a7b95'/><path d='M10 1 V4.2 Q10 5 10.8 5 H14 Z' fill='#a8bac8'/><path d='M4.5 7.5 H11 M4.5 10 H11 M4.5 12.5 H9' fill='none' stroke='#ffffff' stroke-width='1.3' stroke-linecap='round' stroke-linejoin='round'/></svg>";
static const ibString s_folder_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><path d='M1 3.5 Q1 2 2.5 2 H5.8 L7.5 3.7 H13.5 Q15 3.7 15 5.2 V12.5 Q15 14 13.5 14 H2.5 Q1 14 1 12.5 Z' fill='#1c6fd1'/><path d='M1 6.5 H15 V12.5 Q15 14 13.5 14 H2.5 Q1 14 1 12.5 Z' fill='#74c0fc'/></svg>";
static const ibString s_listView_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><rect x='1.5' y='2.8' width='2.6' height='2.6' rx='0.6' fill='#1c6fd1'/><rect x='1.5' y='6.7' width='2.6' height='2.6' rx='0.6' fill='#1c6fd1'/><rect x='1.5' y='10.6' width='2.6' height='2.6' rx='0.6' fill='#1c6fd1'/><path d='M6.5 4.1 H14 M6.5 8 H14 M6.5 11.9 H14' fill='none' stroke='#5a7b95' stroke-width='1.8' stroke-linecap='round' stroke-linejoin='round'/></svg>";
static const ibString s_reportView_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><rect x='1' y='2' width='14' height='12' rx='1.5' fill='#5a7b95'/><rect x='2' y='5' width='12' height='8' rx='0.5' fill='#a8bac8'/><path d='M2 9 H14 M6.5 5 V13' fill='none' stroke='#ffffff' stroke-width='1' stroke-linecap='butt' stroke-linejoin='round'/></svg>";
static const ibString s_fullScreen_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><path d='M2 6 V2 H6 M10 2 H14 V6 M14 10 V14 H10 M6 14 H2 V10' fill='none' stroke='#1c6fd1' stroke-width='2' stroke-linecap='butt' stroke-linejoin='round'/><rect x='5' y='5' width='6' height='6' rx='1' fill='#74c0fc'/></svg>";
static const ibString s_helpBook_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><rect x='2' y='1' width='12' height='14' rx='1.5' fill='#1c6fd1'/><path d='M3.5 1 H5 V15 H3.5 Q2 15 2 13.5 V2.5 Q2 1 3.5 1 Z' fill='#1864ab'/><path d='M7.4 5.3 Q7.4 3.6 9.4 3.6 Q11.4 3.6 11.4 5.2 Q11.4 6.4 10.2 6.9 Q9.4 7.3 9.4 8.3' fill='none' stroke='#ffffff' stroke-width='1.5' stroke-linecap='round'/><circle cx='9.4' cy='10.8' r='1' fill='#ffffff'/></svg>";
static const ibString s_helpSettings_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><path d='M3 14 L8 2.5 L13 14' fill='none' stroke='#5a7b95' stroke-width='2.2' stroke-linecap='round' stroke-linejoin='round'/><path d='M5 10 H11' fill='none' stroke='#5a7b95' stroke-width='2' stroke-linecap='butt' stroke-linejoin='round'/></svg>";
static const ibString s_tickMark_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><path d='M3 8.5 L6.5 12 L13 4' stroke='#2f9e44' stroke-width='2.4' stroke-linecap='round' stroke-linejoin='round' fill='none'/></svg>";
static const ibString s_stop_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><path d='M5.3 1 H10.7 L15 5.3 V10.7 L10.7 15 H5.3 L1 10.7 V5.3 Z' fill='#e03131'/><path d='M5.6 5.6 L10.4 10.4 M10.4 5.6 L5.6 10.4' fill='none' stroke='#ffffff' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'/></svg>";
static const ibString s_missingImage_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><rect x='1' y='1' width='14' height='14' rx='1.5' fill='#a8bac8'/><path d='M5 5 L11 11 M11 5 L5 11' fill='none' stroke='#e03131' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'/></svg>";
static const ibString s_warning_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><path d='M8 1 Q8.8 1 9.3 1.9 L15.3 13 Q15.8 14.5 14.2 14.5 H1.8 Q0.2 14.5 0.7 13 L6.7 1.9 Q7.2 1 8 1 Z' fill='#fcc419'/><path d='M8 5.4 V9.4' fill='none' stroke='#5c3d00' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'/><circle cx='8' cy='12' r='1.2' fill='#5c3d00'/></svg>";
static const ibString s_information_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><circle cx='8' cy='8' r='7' fill='#1c6fd1'/><circle cx='8' cy='4.6' r='1.3' fill='#ffffff'/><path d='M8 7.2 V12' fill='none' stroke='#ffffff' stroke-width='2.2' stroke-linecap='butt' stroke-linejoin='round'/></svg>";
static const ibString s_question_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><circle cx='8' cy='8' r='7' fill='#1c6fd1'/><path d='M5.7 5.9 Q5.7 3.4 8 3.4 Q10.3 3.4 10.3 5.5 Q10.3 6.9 8.9 7.6 Q8 8.1 8 9.3' fill='none' stroke='#ffffff' stroke-width='1.8' stroke-linecap='round' stroke-linejoin='round'/><circle cx='8' cy='12' r='1.2' fill='#ffffff'/></svg>";
static const ibString s_tip_svg = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><path d='M8 0.8 Q12.9 0.8 12.9 5.6 Q12.9 7.6 11.2 9.3 Q10.3 10.2 10.3 11.3 H5.7 Q5.7 10.2 4.8 9.3 Q3.1 7.6 3.1 5.6 Q3.1 0.8 8 0.8 Z' fill='#fcc419'/><path d='M5.6 5.4 Q5.9 3.4 7.8 3' fill='none' stroke='#ffffff' stroke-width='1.3' stroke-linecap='round' stroke-linejoin='round'/><rect x='5.7' y='12' width='4.6' height='1.4' rx='0.5' fill='#5a7b95'/><rect x='6.4' y='14' width='3.2' height='1.4' rx='0.6' fill='#5a7b95'/></svg>";

#pragma endregion

void RegisterAllPicture()
{
	::RegisterPicture(ibT("Structure"), g_picStructureCLSID, s_structure_svg);
	::RegisterPicture(ibT("Error"), g_picErrorCLSID, s_error_svg);

	::RegisterPicture(ibT("CloseForm"), g_picCloseFormCLSID, s_closeForm_svg);
	::RegisterPicture(ibT("UpdateForm"), g_picUpdateFormCLSID, s_updateForm_svg);
	::RegisterPicture(ibT("HelpForm"), g_picHelpFormCLSID, s_helpForm_svg);

	::RegisterPicture(ibT("ChangeForm"), g_picChangeFormCLSID, s_changeForm_svg);

	::RegisterPicture(ibT("Add"), g_picAddCLSID, s_add_svg);
	::RegisterPicture(ibT("Edit"), g_picEditCLSID, s_edit_svg);
	::RegisterPicture(ibT("Copy"), g_picCopyCLSID, s_copy_svg);
	::RegisterPicture(ibT("Delete"), g_picDeleteCLSID, s_delete_svg);

	::RegisterPicture(ibT("AddFolder"), g_picAddFolderCLSID, s_addFolder_svg);
	::RegisterPicture(ibT("Select"), g_picSelectCLSID, s_tickMark_svg);

	::RegisterPicture(ibT("Compose"), g_picComposeCLSID, s_compose_svg);
	::RegisterPicture(ibT("ComposerSettings"), g_picComposerSettingsCLSID, s_composerSettings_svg);
	::RegisterPicture(ibT("Variants"), g_picVariantsCLSID, s_variants_svg);
	::RegisterPicture(ibT("RestoreSettings"), g_picRestoreSettingsCLSID, s_restoreSettings_svg);
	::RegisterPicture(ibT("SaveSettings"), g_picSaveSettingsCLSID, s_save_svg);

	::RegisterPicture(ibT("Filter"), g_picFilterCLSID, s_filter_svg);
	::RegisterPicture(ibT("FilterSet"), g_picFilterSetCLSID, s_filterSet_svg);
	::RegisterPicture(ibT("FilterClear"), g_picFilterClearCLSID, s_filterClear_svg);

	::RegisterPicture(ibT("MoveUp"), g_picMoveUpCLSID, s_moveUp_svg);
	::RegisterPicture(ibT("MoveDown"), g_picMoveDownCLSID, s_moveDown_svg);
	::RegisterPicture(ibT("SortAsc"), g_picSortAscCLSID, s_sortAsc_svg);
	::RegisterPicture(ibT("SortDesc"), g_picSortDescCLSID, s_sortDesc_svg);

	::RegisterPicture(ibT("Clone"), g_picCloneCLSID, s_clone_svg);
	::RegisterPicture(ibT("Save"), g_picSaveCLSID, s_save_svg);
	::RegisterPicture(ibT("Post"), g_picPostCLSID, s_post_svg);
	::RegisterPicture(ibT("MarkAsDelete"), g_picMarkAsDeleteCLSID, s_markAsDelete_svg);
	::RegisterPicture(ibT("Generate"), g_picGenerateCLSID, s_generate_svg);
	::RegisterPicture(ibT("ExecuteJob"), g_picExecuteJobCLSID, s_executeJob_svg);
	::RegisterPicture(ibT("Print"), g_picPrintCLSID, s_print_svg);
	::RegisterPicture(ibT("Hierarchy"), g_picHierarchyCLSID, s_hierarchy_svg);
	::RegisterPicture(ibT("Event"), g_picEventCLSID, s_event_svg);

	::RegisterPicture(ibT("HomePage"), g_picHomePageCLSID, s_homePage_svg);
	::RegisterPicture(ibT("Assistant"), g_picAssistantCLSID, s_assistant_svg);

	::RegisterPicture(ibT("User"), g_picUserCLSID, s_user_svg);
	::RegisterPicture(ibT("UserActive"), g_picUserActiveCLSID, s_activeUser_svg);
	::RegisterPicture(ibT("UserList"), g_picUserListCLSID, s_userList_svg);

	::RegisterPicture(ibT("Authentication"), g_picAuthenticationCLSID, s_authentication_svg);

	::RegisterPicture(ibT("RowItem"), g_picRowItemCLSID, s_rowItem_svg);
	::RegisterPicture(ibT("RowItemDeleted"), g_picRowItemDeletedCLSID, s_rowItemDeleted_svg);
	::RegisterPicture(ibT("RowFolder"), g_picRowFolderCLSID, s_rowFolder_svg);
	::RegisterPicture(ibT("RowFolderDeleted"), g_picRowFolderDeletedCLSID, s_rowFolderDeleted_svg);
	::RegisterPicture(ibT("RowDocument"), g_picRowDocumentCLSID, s_rowDocument_svg);
	::RegisterPicture(ibT("RowDocumentPosted"), g_picRowDocumentPostedCLSID, s_rowDocumentPosted_svg);
	::RegisterPicture(ibT("RowDocumentDeleted"), g_picRowDocumentDeletedCLSID, s_rowDocumentDeleted_svg);
	::RegisterPicture(ibT("RowRecord"), g_picRowRecordCLSID, s_rowRecord_svg);
	::RegisterPicture(ibT("RowRecordInactive"), g_picRowRecordInactiveCLSID, s_rowRecordInactive_svg);

	::RegisterPicture(ibT("CommandBar"), g_picCommandBarCLSID, s_commandBar_svg);
	::RegisterPicture(ibT("CommandBarItem"), g_picCommandBarItemCLSID, s_commandBarItem_svg);

	::RegisterPicture(ibT("CommandGroupImportant"), g_picCommandGroupImportantCLSID, s_commandGroupImportant_svg);
	::RegisterPicture(ibT("CommandGroupNormal"), g_picCommandGroupNormalCLSID, s_commandGroupNormal_svg);
	::RegisterPicture(ibT("CommandGroupCreate"), g_picCommandGroupCreateCLSID, s_commandGroupCreate_svg);
	::RegisterPicture(ibT("CommandGroupReports"), g_picCommandGroupReportsCLSID, s_report_svg);
	::RegisterPicture(ibT("CommandGroupService"), g_picCommandGroupServiceCLSID, s_composerSettings_svg);

	::RegisterPicture(ibT("Application"), g_picApplicationCLSID, s_application_svg);
	::RegisterPicture(ibT("Report"), g_picReportCLSID, s_report_svg);
	::RegisterPicture(ibT("Table"), g_picTableCLSID, s_table_svg);
	::RegisterPicture(ibT("Translation"), g_picTranslationCLSID, s_translation_svg);
	::RegisterPicture(ibT("Value"), g_picValueCLSID, s_value_svg);
	::RegisterPicture(ibT("Resource"), g_picResourceCLSID, s_resource_svg);

	::RegisterPicture(ibT("New"), g_picNewCLSID, s_new_svg);
	::RegisterPicture(ibT("Open"), g_picOpenCLSID, s_open_svg);
	::RegisterPicture(ibT("SaveAs"), g_picSaveAsCLSID, s_saveAs_svg);
	::RegisterPicture(ibT("Quit"), g_picQuitCLSID, s_quit_svg);
	::RegisterPicture(ibT("Undo"), g_picUndoCLSID, s_undo_svg);
	::RegisterPicture(ibT("Redo"), g_picRedoCLSID, s_redo_svg);
	::RegisterPicture(ibT("Cut"), g_picCutCLSID, s_cut_svg);
	::RegisterPicture(ibT("ClipboardCopy"), g_picClipboardCopyCLSID, s_clone_svg);
	::RegisterPicture(ibT("Paste"), g_picPasteCLSID, s_paste_svg);
	::RegisterPicture(ibT("Find"), g_picFindCLSID, s_find_svg);
	::RegisterPicture(ibT("FindCancel"), g_picFindCancelCLSID, s_findCancel_svg);
	::RegisterPicture(ibT("GoBack"), g_picGoBackCLSID, s_goBack_svg);
	::RegisterPicture(ibT("GoForward"), g_picGoForwardCLSID, s_goForward_svg);
	::RegisterPicture(ibT("GoDirUp"), g_picGoDirUpCLSID, s_goDirUp_svg);
	::RegisterPicture(ibT("Plus"), g_picPlusCLSID, s_plus_svg);
	::RegisterPicture(ibT("Minus"), g_picMinusCLSID, s_minus_svg);
	::RegisterPicture(ibT("File"), g_picFileCLSID, s_file_svg);
	::RegisterPicture(ibT("Folder"), g_picFolderCLSID, s_folder_svg);
	::RegisterPicture(ibT("ListView"), g_picListViewCLSID, s_listView_svg);
	::RegisterPicture(ibT("ReportView"), g_picReportViewCLSID, s_reportView_svg);
	::RegisterPicture(ibT("FullScreen"), g_picFullScreenCLSID, s_fullScreen_svg);
	::RegisterPicture(ibT("HelpBook"), g_picHelpBookCLSID, s_helpBook_svg);
	::RegisterPicture(ibT("HelpSettings"), g_picHelpSettingsCLSID, s_helpSettings_svg);
	::RegisterPicture(ibT("TickMark"), g_picTickMarkCLSID, s_tickMark_svg);
	::RegisterPicture(ibT("Stop"), g_picStopCLSID, s_stop_svg);
	::RegisterPicture(ibT("MissingImage"), g_picMissingImageCLSID, s_missingImage_svg);
	::RegisterPicture(ibT("Warning"), g_picWarningCLSID, s_warning_svg);
	::RegisterPicture(ibT("Information"), g_picInformationCLSID, s_information_svg);
	::RegisterPicture(ibT("Question"), g_picQuestionCLSID, s_question_svg);
	::RegisterPicture(ibT("Tip"), g_picTipCLSID, s_tip_svg);
}

class ibPictureAutoLoader {
public:

	ibPictureAutoLoader() {

		// Register all picture
		RegisterAllPicture();
	}
};

ibPictureAutoLoader m_autoLoader;
