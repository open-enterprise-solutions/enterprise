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
#include <wx/wx.h> 
#endif

#include <3rdparty/email/codec/rfc2231.h>
#include <3rdparty/email/codec/charsetconv.h>

bool wxRfc2231::ExtractParameter(const std::list<mimetic::FieldParam>& parameters_list, const wxString& name, wxString& value)
{
   /* Parse complete list and check if we can find parameter */
   std::list<mimetic::FieldParam>::const_iterator it;
   for (it = parameters_list.begin(); it != parameters_list.end(); it++)
   {
      if (name.compare(wxString(it->name().c_str(), wxConvLocal)) == 0)
      {
         value = wxString(it->value().c_str(), wxConvLocal);
         return true;
      }
   }

   /* We did not found the parameter */
   return false;
}

wxString wxRfc2231::Decode(const std::list<mimetic::FieldParam>& parameters_list, const wxString& parameter_name)
{
   /* Check which encoding was used and extract raw content */
   wxString extracted_val;
   wxString raw_content;
   bool shall_decode_charset;
   if (ExtractParameter(parameters_list, parameter_name, extracted_val))
   {
      /* No Encoding */
      raw_content = extracted_val;
      shall_decode_charset = false;
   }
   else if (ExtractParameter(parameters_list, parameter_name + _T("*0"), extracted_val))
   {
      /* Lines Split Encoding */
      raw_content = extracted_val;
      unsigned long current_id = 1;
      while (ExtractParameter(parameters_list,
                              parameter_name + wxString::Format(_T("*%lu"), current_id),
                              extracted_val))
      {
         raw_content += extracted_val;
         current_id++;
      }
      shall_decode_charset = false;
   }
   else if (ExtractParameter(parameters_list, parameter_name + _T("*"), extracted_val))
   {
      /* Charset Encoding */
      raw_content = extracted_val;
      shall_decode_charset = true;
   }
   else if (ExtractParameter(parameters_list, parameter_name + _T("*0*"), extracted_val))
   {
      /* Line Split And Charset Encoding */
      raw_content = extracted_val;
      unsigned long current_id = 1;
      while (ExtractParameter(parameters_list,
                              parameter_name + wxString::Format(_T("*%lu*"), current_id),
                              extracted_val))
      {
         raw_content += extracted_val;
         current_id++;
      }
      shall_decode_charset = true;
   }
   else
   {
      /* Not Found -> return empty string... */
      raw_content = _T("");
      shall_decode_charset = false;
   }

   /* decode the string, if necessary */
   wxString decoded_string;
   if (shall_decode_charset)
   {
      /* Extract charset, language and content */
      wxString charset = raw_content.BeforeFirst('\'');
     // wxString language = raw_content.AfterFirst('\'').BeforeFirst('\'');
      wxString remaining_content = raw_content.AfterFirst('\'').AfterFirst('\'');

      /* decode complete string */
      while (remaining_content.Contains(_T("%")))
      {
         decoded_string += remaining_content.BeforeFirst('%');
         unsigned long hex_val;
         remaining_content.AfterFirst('%').Mid(0, 2).ToULong(&hex_val, 16);
         decoded_string += wxString::Format(_T("%c"), int(hex_val));
         remaining_content = remaining_content.AfterFirst('%').Mid(2);
      }
      decoded_string += remaining_content;

      /* Decode charset */
      decoded_string = wxCharsetConverter::ConvertCharset(decoded_string, charset);
   }
   else
   {
      decoded_string = raw_content;
   }

   return decoded_string;
}
