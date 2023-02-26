/***************************************************************************
    copyright            : (C) 2002-2008 by Stefano Barbato
    email                : stefano@codesink.org

    $Id: message.cxx,v 1.4 2008-10-07 11:06:27 tat Exp $
 ***************************************************************************/

/***************************************************************************

 Licence:     wxWidgets licence

 This file has been copied from the project Mimetic
 (http://codesink.org/mimetic_mime_library.html) and relicenced from the MIT
 licence to the wxWidgets one with authorisation received from Stefano Barbato

 ***************************************************************************/

#include <3rdparty/email/mimetic/rfc822/message.h>
#include <3rdparty/email/mimetic/strutils.h>
#include <3rdparty/email/mimetic/utils.h>


namespace mimetic
{

using namespace std;

ostream& operator<<(ostream& os, const Message& m)
{
   os << "MIME-Version: 1.0" << crlf;
    // header field
    Rfc822Header::const_iterator hbit, heit;
    hbit = m.header().begin(), heit = m.header().end();
    for(; hbit != heit; ++hbit)
        os << *hbit << crlf;
    // empty line, header/body separator
//    os << crlf;
    // body
    os << m.body();
    os.flush();
    return os;
}

Rfc822Header& Message::header()
{
    return m_header;
}

const Rfc822Header& Message::header() const
{
    return m_header;
}

Rfc822Body& Message::body()
{
    return m_body;
}

const Rfc822Body& Message::body() const
{
    return m_body;
}

}
