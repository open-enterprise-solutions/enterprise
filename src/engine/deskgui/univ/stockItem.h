// FORKED from wxWidgets: wxGetStockLabel and wxGetStockHelpString (include/wx/stockitem.h,
// src/common/stockitem.cpp), their tables whole (wx -> ib prefixes applied).

///////////////////////////////////////////////////////////////////////////////
// Name:        wx/stockitem.h
// Purpose:     stock items helpers (privately for use by wxWidgets)
// Author:      Vaclav Slavik
// Created:     2004-08-15
// Copyright:   (c) Vaclav Slavik, 2004
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

#ifndef _DESKGUI_UNIV_STOCKITEM_H_
#define _DESKGUI_UNIV_STOCKITEM_H_

#include "deskgui/deskgui.h"

#include <wx/stockitem.h>   // the flags and the clients, wxIsStockID, wxGetStockAccelerator - no words in them

// ⭐ ONE LOCALIZATION. wx's own labels come through wx's own translations object, which nobody makes: the engine's
// words are its dictionary's (core/localization/dictionary.h), one catalog, open_es. So the words wx keeps for its
// stock ids are said here, through _() - the dictionary's - and their translations sit in open_es.po with the rest,
// taken from wx's locale/<code>.po; New, Open..., Undo read in the base's language beside the engine's own
// (2026-11-13, Max: "one localization, not two, wx's and ours"). Every port alike: the table and the dictionary are
// the same everywhere.

// The label of a stock id, as wxGetStockLabel gives it.
DESKGUI_API wxString ibGetStockLabel(wxWindowID id, long flags = wxSTOCK_WITH_MNEMONIC);

// The help string of a stock id, as wxGetStockHelpString gives it; empty where there is none.
DESKGUI_API wxString ibGetStockHelpString(wxWindowID id, wxStockHelpStringClient client = wxSTOCK_MENU);

#endif // _DESKGUI_UNIV_STOCKITEM_H_
