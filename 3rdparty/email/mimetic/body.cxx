/***************************************************************************
    copyright            : (C) 2002-2008 by Stefano Barbato
    email                : stefano@codesink.org

    $Id: body.cxx,v 1.3 2008-10-07 11:06:25 tat Exp $
 ***************************************************************************/

/***************************************************************************

 Licence:     wxWidgets licence

 This file has been copied from the project Mimetic
 (http://codesink.org/mimetic_mime_library.html) and relicenced from the MIT
 licence to the wxWidgets one with authorisation received from Stefano Barbato

 ***************************************************************************/
// For compilers that support precompilation, includes "wx.h".
#include <wx/wxprec.h>

#ifdef __BORLANDC__
#pragma hdrstop
#endif

// includes
#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include <email/mimetic/mimeentity.h>
#include <email/mimetic/body.h>
#include <email/mimetic/wxFileIterator.h>

namespace mimetic
{
using std::string;

Body::Body()
: m_owner(0)
{
}

void Body::set(const std::string& text)
{
    this->assign(text);
}

void Body::owner(MimeEntity* owner)
{
    m_owner = owner;
}

MimeEntity* Body::owner()
{
    return m_owner;
}

const MimeEntity* Body::owner() const
{
    return m_owner;
}

bool Body::load(const string& fqn)
{
   if (!wxFile::Exists(wxString(fqn.c_str(), wxConvUTF8)))
      return false;

   wxFile file(wxString(fqn.c_str(), wxConvUTF8));
   wxFileIterator beg(file, true);
   wxFileIterator end(file, false);
   std::copy(beg, end, back_inserter(*this) );
   return true;
}

MimeEntityList& Body::parts()
{
    return m_parts;
}

const MimeEntityList& Body::parts() const
{
    return m_parts;
}

void Body::preamble(const string& v)
{
    m_preamble = v;
}

const string& Body::preamble() const
{
    return m_preamble;
}

string& Body::preamble()
{
    return m_preamble;
}

void Body::epilogue(const string& v)
{
    m_epilogue = v;
}

const string& Body::epilogue() const
{
    return m_epilogue;
}

string& Body::epilogue()
{
    return m_epilogue;
}

}

