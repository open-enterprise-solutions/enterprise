/////////////////////////////////////////////////////////////////////////////
// Name:        wxrfc2047.h
// Purpose:     This implements the rfc2047 norm. This allows encoding and decoding
//              MIME headers parts
// Author:      Brice André
// Created:     2010/12/12
// RCS-ID:      $Id: mycomp.h 505 2007-03-31 10:31:46Z frm $
// Copyright:   (c) 2010 Brice André
// Licence:     wxWidgets licence
/////////////////////////////////////////////////////////////////////////////


#ifndef _WX_RFC2047_H_
#define _WX_RFC2047_H_

#include <email/mimetic/mimeentity.h>

class wxRfc2047
{
   public:

      /**
       * This function decodes a string encoded in conformance with RFC2047. The decoded string
       * is encoded in the default system charset. Note that it is safe to provide a non-encoded
       * string to this function.
       *
       * @param encoded_str The encoded string
       * @return The decoded string, encoded in system encoding charset
       */
      static wxString Decode(const wxString& encoded_str);

      /*!
       * This function encodes a string encoded in the default system charset in conformance
       * to the RFC2047 norm.
       *
       * Note that it is safe to provide a string that does not request encoding.
       *
       * @param value the string that shall be encoded, in default system charset.
       * @return The encoded string
       */
      //TODO : we shall change the mimetic library so that it uses this encoding mechanism
      static wxString Encode(const wxString& value);

};

#endif /* _WX_RFC2047_H_ */
