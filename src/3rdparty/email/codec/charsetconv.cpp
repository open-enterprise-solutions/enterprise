////////////////////////////////////////////////////////////////////////////
// Name:        rfc2231.cpp
// Purpose:     This class implements the rfc2231 norm
// Author:      Brice André
// Created:     2010/12/12
// RCS-ID:      $Id: mycomp.cpp 505 2007-03-31 10:31:46Z frm $
// Copyright:   (c) 2010 Brice André
// Licence:     wxWidgets licence
/////////////////////////////////////////////////////////////////////////////


// For compilers that support precompilation, includes "wx.h".
#include <wx/wxprec.h>

#ifdef __BORLANDC__
#pragma hdrstop
#endif

// includes
#ifndef WX_PRECOMP
   // here goes the #include <wx/abc.h> directives for those
   // files which are not included by wxprec.h
#include <wx/wx.h>
#endif

#include "wx/encconv.h"
#include "wx/fontmap.h"

#include <3rdparty/email/codec/charsetconv.h>

/*!
 * This function computes the beginning of an UTF-8 stream by pointing after the BOM, if any
 * It seems that some configs of wxWidgets do not like the presence of BOM in an UTF-8 stream...
 */
static const char* AfterBOM(const char* stream)
{
   const unsigned char* ptr = (const unsigned char*)stream;
   if ((ptr[0] == 0xEF) &&
       (ptr[1] == 0xBB) &&
       (ptr[2] == 0xBF))
   {
      /* BOM coded on 3 bytes */
      return stream + 3;
   }
   else if ((ptr[0] == 0xF0) &&
            (ptr[1] == 0x8F) &&
            (ptr[2] == 0xBB) &&
            (ptr[3] == 0xBF))
   {
      /* BOM coded on 4 bytes */
      return stream + 4;
   }
   else
   {
      /* No BOM... */
      return stream;
   }
}

wxString wxCharsetConverter::ConvertCharset(const wxString& str, const wxString& char_set)
{
   wxString str_content;
   wxFontEncoding text_encoding = wxFontMapper::Get()->CharsetToEncoding(char_set);
   wxFontEncoding system_encoding = wxLocale::GetSystemEncoding();
   if (text_encoding == wxFONTENCODING_UTF8)
   {
      str_content = wxString::FromUTF8(AfterBOM(str.mb_str(wxConvLocal)));
   }
   else if (system_encoding != text_encoding)
   {
      wxEncodingConverter converter;
      bool can_convert = converter.Init(text_encoding, system_encoding);
      if (can_convert)
      {
         str_content = converter.Convert(str);
      }
      else
      {
         /* What can we do ?? */
      }
   }

   return str_content;
}
