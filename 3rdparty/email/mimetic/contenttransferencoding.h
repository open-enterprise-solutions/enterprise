/***************************************************************************
    copyright            : (C) 2002-2008 by Stefano Barbato
    email                : stefano@codesink.org

    $Id: contenttransferencoding.h,v 1.12 2008-10-07 11:06:25 tat Exp $
 ***************************************************************************/

/***************************************************************************

 Licence:     wxWidgets licence

 This file has been copied from the project Mimetic
 (http://codesink.org/mimetic_mime_library.html) and relicenced from the MIT
 licence to the wxWidgets one with authorisation received from Stefano Barbato

 ***************************************************************************/

#ifndef _MIMETIC_CONTENT_TRANSFER_ENCODING_H_
#define _MIMETIC_CONTENT_TRANSFER_ENCODING_H_

#include <string>
#include <email/mimetic/strutils.h>
#include <email/mimetic/rfc822/fieldvalue.h>

namespace mimetic
{


/// Content-Transfer-Encoding field value
struct ContentTransferEncoding: public FieldValue
{
    static const char label[];
    ContentTransferEncoding();
    ContentTransferEncoding(const char*);
    ContentTransferEncoding(const std::string&);
    const istring& mechanism() const;
    void mechanism(const std::string&);

    void set(const std::string&);
    std::string str() const;
protected:
    FieldValue* clone() const;
private:
    istring m_mechanism;
};

}

#endif

