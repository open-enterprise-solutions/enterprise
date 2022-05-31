/***************************************************************************
    copyright            : (C) 2002-2008 by Stefano Barbato
    email                : stefano@codesink.org

    $Id: contentid.h,v 1.11 2008-10-07 11:06:25 tat Exp $
 ***************************************************************************/

/***************************************************************************

 Licence:     wxWidgets licence

 This file has been copied from the project Mimetic
 (http://codesink.org/mimetic_mime_library.html) and relicenced from the MIT
 licence to the wxWidgets one with authorisation received from Stefano Barbato

 ***************************************************************************/

#ifndef _MIMETIC_CONTENTID_H_
#define _MIMETIC_CONTENTID_H_

#include <string>
#include <email/mimetic/utils.h>
#include <email/mimetic/rfc822/fieldvalue.h>

namespace mimetic
{

/// Content-ID field value
struct ContentId: public FieldValue
{
    // format: yyyymmgg.pid.seq@hostname
    static const char label[];
    ContentId();
    ContentId(const char*);
    ContentId(const std::string&);
    void set(const std::string&);
    std::string str() const;
protected:
    FieldValue* clone() const;
private:
    static unsigned int ms_sequence_number;
    std::string m_cid;
};

}

#endif
