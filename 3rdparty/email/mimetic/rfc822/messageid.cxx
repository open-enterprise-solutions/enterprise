/***************************************************************************
    copyright            : (C) 2002-2008 by Stefano Barbato
    email                : stefano@codesink.org

    $Id: messageid.cxx,v 1.4 2008-10-07 11:06:27 tat Exp $
 ***************************************************************************/

/***************************************************************************

 Licence:     wxWidgets licence

 This file has been copied from the project Mimetic
 (http://codesink.org/mimetic_mime_library.html) and relicenced from the MIT
 licence to the wxWidgets one with authorisation received from Stefano Barbato

 ***************************************************************************/

#include <wx/wxprec.h>

#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include <wx/utils.h>

#include <email/mimetic/rfc822/messageid.h>

namespace mimetic
{

unsigned int MessageId::ms_sequence_number = 0;

/// pass the thread_id argument if you're using mimetic with threads
MessageId::MessageId(unsigned long thread_id)
{
    std::string host = (const char*)wxGetHostName().mb_str(wxConvLocal);
    if(!host.length())
        host = "unknown";

    m_msgid = "m" + utils::int2str(wxDateTime::Now().GetTicks()) + "." + utils::int2str(wxGetProcessId()) +
        "." + utils::int2str(thread_id) +
        utils::int2str(++ms_sequence_number) + "@" + host;
}

MessageId::MessageId(const std::string& value)
: m_msgid(value)
{
}

std::string MessageId::str() const
{
    return m_msgid;
}

void MessageId::set(const std::string& value)
{
    m_msgid = value;
}

FieldValue* MessageId::clone() const
{
    return new MessageId(*this);
}

}
