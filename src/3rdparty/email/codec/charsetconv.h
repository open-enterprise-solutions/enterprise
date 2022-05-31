/////////////////////////////////////////////////////////////////////////////
// Name:        wxrfc2231.h
// Purpose:     This implements the rfc2231 norm. This allows encoding and decoding
//              parameters of MIME headers
// Author:      Brice André
// Created:     2010/12/12
// RCS-ID:      $Id: mycomp.h 505 2007-03-31 10:31:46Z frm $
// Copyright:   (c) 2010 Brice André
// Licence:     wxWidgets licence
/////////////////////////////////////////////////////////////////////////////


#ifndef _WX_CHARSET_CONV_H_
#define _WX_CHARSET_CONV_H_

class wxCharsetConverter
{
   public:
      static wxString ConvertCharset(const wxString& str, const wxString& char_set);
};

#endif /* _WX_CHARSET_CONV_H_ */
