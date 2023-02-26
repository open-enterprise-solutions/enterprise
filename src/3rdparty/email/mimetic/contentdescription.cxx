/***************************************************************************
    copyright            : (C) 2002-2008 by Stefano Barbato
    email                : stefano@codesink.org

    $Id: contentdescription.cxx,v 1.3 2008-10-07 11:06:25 tat Exp $
 ***************************************************************************/

/***************************************************************************

 Licence:     wxWidgets licence

 This file has been copied from the project Mimetic
 (http://codesink.org/mimetic_mime_library.html) and relicenced from the MIT
 licence to the wxWidgets one with authorisation received from Stefano Barbato

 ***************************************************************************/

#include <3rdparty/email/mimetic/contentdescription.h>

namespace mimetic
{
using namespace std;

const char ContentDescription::label[] = "Content-Description";

ContentDescription::ContentDescription()
{
}

ContentDescription::ContentDescription(const char* cstr)
{
    set(cstr);
}

ContentDescription::ContentDescription(const string& val)
{
    set(val);
}


void ContentDescription::set(const string& val)
{
    m_value = val;
}

string ContentDescription::str() const
{
    return m_value;
}


FieldValue* ContentDescription::clone() const
{
    return new ContentDescription(*this);
}

}
