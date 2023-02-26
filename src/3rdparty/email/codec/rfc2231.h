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


#ifndef _WX_RFC2231_H_
#define _WX_RFC2231_H_

#include <3rdparty/email/mimetic/mimeentity.h>

class wxRfc2231
{
   public:

      /**
       * This function retrieves value of parameter whose name is 'parameter_name'.
       * It fetches the different elements constituting the value of the parameter
       * in parameters_list.
       *
       * The value is decoded in conformance to RFC2231 and is returned in the default
       * system encoding charset.
       *
       * @param parameters_list The list of parameters-values from which parameter value shall be extracted
       * @param parameter_name The name of the parameter that shall be extracted
       * @return The decoded parameter value, encoded in system encoding charset
       */
      static wxString Decode(const std::list<mimetic::FieldParam>& parameters_list, const wxString& parameter_name);

      /*!
       * The function encodes the parameter_value provided and inserts it as parameter
       * of name parameter_name in provided parameters_list. This encoding is performed
       * in conformance to RFC2231.
       *
       * The provided value is supposed to be encoded in default system encoding charset.
       *
       * @param parameters_list the parameters list in which the coded parameter shall be inserted
       * @param parameter_name The name of the parameter that shall be encoded
       * @param parameter_value The value of parameter that shall be encoded
       */
      //TODO : we shall change the mimetic library so that it uses this encoding mechanism
      static void Encode(std::list<mimetic::FieldParam>& parameters_list, const wxString& parameter_name, const wxString& parameter_value);

   private:

      static bool ExtractParameter(const std::list<mimetic::FieldParam>& parameters_list, const wxString& name, wxString& value);
};

#endif /* _WX_RFC2231_H_ */
