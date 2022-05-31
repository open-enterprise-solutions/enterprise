/***************************************************************************
    copyright            : (C) 2002-2008 by Stefano Barbato
    email                : stefano@codesink.org

    $Id: messageid.h,v 1.15 2008-10-07 11:06:27 tat Exp $
 ***************************************************************************/

/***************************************************************************

 Licence:     wxWidgets licence

 This file has been copied from the project Mimetic
 (http://codesink.org/mimetic_mime_library.html) and relicenced from the MIT
 licence to the wxWidgets one with authorisation received from Stefano Barbato

 ***************************************************************************/

#ifndef _MIMETIC_MESSAGEID_H_
#define _MIMETIC_MESSAGEID_H_

#include <string>
#include <email/mimetic/utils.h>
#include <email/mimetic/rfc822/fieldvalue.h>

namespace mimetic
{


/// Message-ID field value
/// On Win32 Winsock library must be initialized before using this class.
struct MessageId: public FieldValue
{
    MessageId(unsigned long thread_id = 0 );
    MessageId(const std::string&);
    std::string str() const;
    void set(const std::string&);
protected:
    FieldValue* clone() const;
private:
    static unsigned int ms_sequence_number;
    std::string m_msgid;
};


}

#endif
