/////////////////////////////////////////////////////////////////////////////
// Name:        wxmd5.h
// Purpose:     This implements the md5 hash algorithm
// Author:      Brice André
// Created:     2010/12/05
// RCS-ID:      $Id: mycomp.h 505 2007-03-31 10:31:46Z frm $
// Copyright:   (c) 2010 Brice André
// Licence:     wxWidgets licence
/////////////////////////////////////////////////////////////////////////////


#ifndef _WX_MD5_H_
#define _WX_MD5_H_

// wxWidgets headers
#include <wx/wx.h>

class wxMD5
{
   public:
      static wxString ComputeMd5(const wxString& content);

      static wxString ComputeKeyedMd5(const wxString& content, const wxString& key);
};

#endif /* _WX_MD5_H_ */
