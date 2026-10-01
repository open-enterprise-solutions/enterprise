// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to frontend.vcxproj
// (set ObjectFileName on a name clash with visualView), add to the demo form.

///////////////////////////////////////////////////////////////////////////////
// Name:        wx/univ/app.h
// Purpose:     ibUniversalApp class extends wxApp for wxUniv port
// Author:      Vadim Zeitlin
// Created:     06.08.00
// Copyright:   (c) 2000 SciTech Software, Inc. (www.scitechsoft.com)
// Licence:     ibWindows licence
///////////////////////////////////////////////////////////////////////////////

#ifndef _WX_UNIX_APP_H_
#define _WX_UNIX_APP_H_

class FRONTEND_API ibUniversalApp : public wxApp
{
public:
};

#endif // _WX_UNIX_APP_H_

