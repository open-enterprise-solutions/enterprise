/***************************************************************************
    copyright            : (C) 2002-2008 by Stefano Barbato
    email                : stefano@codesink.org

    $Id: contenttransferencoding.cxx,v 1.3 2008-10-07 11:06:25 tat Exp $
 ***************************************************************************/

/***************************************************************************

 Licence:     wxWidgets licence

 This file has been copied from the project Mimetic
 (http://codesink.org/mimetic_mime_library.html) and relicenced from the MIT
 licence to the wxWidgets one with authorisation received from Stefano Barbato

 ***************************************************************************/

#include <email/mimetic/contenttransferencoding.h>

namespace mimetic
{
using namespace std;

const char ContentTransferEncoding::label[] = "Content-Transfer-Encoding";

ContentTransferEncoding::ContentTransferEncoding()
{
}

ContentTransferEncoding::ContentTransferEncoding(const char* cstr)
: m_mechanism(cstr)
{
}


ContentTransferEncoding::ContentTransferEncoding(const string& mechanism)
: m_mechanism(mechanism)
{
}

const istring& ContentTransferEncoding::mechanism() const
{
    return m_mechanism;
}

void ContentTransferEncoding::mechanism(const string& mechanism)
{
    m_mechanism = mechanism;
}

void ContentTransferEncoding::set(const string& val)
{
    mechanism(val);
}

string ContentTransferEncoding::str() const
{
    return mechanism();
}

FieldValue* ContentTransferEncoding::clone() const
{
    return new ContentTransferEncoding(*this);
}

}

