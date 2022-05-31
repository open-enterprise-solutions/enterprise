/***************************************************************************
    copyright            : (C) 2002-2008 by Stefano Barbato
    email                : stefano@codesink.org

    $Id: contentid.cxx,v 1.3 2008-10-07 11:06:25 tat Exp $
 ***************************************************************************/

/***************************************************************************

 Licence:     wxWidgets licence

 This file has been copied from the project Mimetic
 (http://codesink.org/mimetic_mime_library.html) and relicenced from the MIT
 licence to the wxWidgets one with authorisation received from Stefano Barbato

 ***************************************************************************/

#include <email/mimetic/contentid.h>

#include <wx/wxprec.h>

#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

namespace mimetic
{
unsigned int ContentId::ms_sequence_number = 0;

const char ContentId::label[] = "Content-ID";

ContentId::ContentId()
{
    std::string host = (const char*)wxGetHostName().mb_str(wxConvLocal);
    if(!host.length())
        host = "unknown";
  m_cid = "c" + utils::int2str(wxDateTime::Now().GetTicks()) + "." + utils::int2str(wxGetProcessId()) +
        "." + utils::int2str(++ms_sequence_number) + "@" + host;
}

ContentId::ContentId(const char* cstr)
:m_cid(cstr)
{
}


ContentId::ContentId(const std::string& value)
:m_cid(value)
{
}

void ContentId::set(const std::string& value)
{
    m_cid = value;
}

std::string ContentId::str() const
{
    return m_cid;
}

FieldValue* ContentId::clone() const
{
    return new ContentId(*this);
}

}
