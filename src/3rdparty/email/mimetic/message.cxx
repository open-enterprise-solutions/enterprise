/***************************************************************************
    copyright            : (C) 2002-2008 by Stefano Barbato
    email                : stefano@codesink.org

    $Id: message.cxx,v 1.4 2008-10-07 11:06:25 tat Exp $
 ***************************************************************************/

/***************************************************************************

 Licence:     wxWidgets licence

 This file has been copied from the project Mimetic
 (http://codesink.org/mimetic_mime_library.html) and relicenced from the MIT
 licence to the wxWidgets one with authorisation received from Stefano Barbato

 ***************************************************************************/
#include <wx/wxprec.h>

#ifdef __BORLANDC__
    #pragma hdrstop
#endif

#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include <fstream>
#include <cstdlib>
#include <3rdparty/email/mimetic/message.h>
#include <3rdparty/email/mimetic/contenttype.h>
#include <3rdparty/email/mimetic/utils.h>

namespace mimetic
{

using namespace std;
Attachment::Attachment(const std::string& fqn)
{
    set(fqn, ContentType("application","octet-stream"),Base64::Encoder());
}

Attachment::Attachment(const std::string& fqn, const std::vector<unsigned char>& content)
{
   ContentType ctype("application","octet-stream");

   Header& h = header();
   m_fqn = fqn;
   m_status = false;
   std::string filename = utils::extractFilename(m_fqn);

   Base64::Encoder codec;

   // Content-Type
   h.contentType(ctype);
   h.contentType().paramList().push_back(ContentType::Param("name", filename));

   // Content-Transfer-Encoding
   h.contentTransferEncoding().mechanism(codec.name());

   // Content-Disposition
   h.contentDisposition().type("attachment");
   h.contentDisposition().paramList().push_back(ContentDisposition::Param("filename", filename));

    if(codec.codeSizeMultiplier() > 1.0)
    {
       body().reserve(::ceil(content.size() * codec.codeSizeMultiplier()));
    }
    mimetic::code(content.begin(), content.end(), codec, back_inserter(body()) );

    return ;
}

Attachment::Attachment(const std::string& fqn, const ContentType& ct)
{
    set(fqn, ct, Base64::Encoder());
}


TextEntity::TextEntity()
{
    header().contentType("text/unknown");
}

TextEntity::TextEntity(const string& text)
{
    m_header.contentType("text/unknown");
    m_body.assign(text);
}

TextEntity::TextEntity(const string& text, const string& charset)
{
    ContentType ct("text", "unknown");
    ct.paramList().push_back(ContentType::Param("charset", charset));
    m_header.contentType(ct);
    m_body.assign(text);
}

TextPlain::TextPlain(const string& text)
: TextEntity(text)
{
    m_header.contentType("text/plain");
}

TextPlain::TextPlain(const string& text, const string& charset)
: TextEntity(text,charset)
{
    m_header.contentType("text/plain");
}


TextEnriched::TextEnriched(const string& text)
: TextEntity(text)
{
    m_header.contentType("text/enriched");
}
TextEnriched::TextEnriched(const string& text, const string& charset)
: TextEntity(text,charset)
{
    m_header.contentType("text/enriched");
}


MultipartEntity::MultipartEntity()
{
    ContentType::Boundary boundary;
    ContentType ct("multipart", "unknown");
    ct.paramList().push_back(ContentType::Param("boundary", boundary));
    m_header.contentType(ct);
}

MultipartMixed::MultipartMixed()
{
    ContentType::Boundary boundary;
    ContentType ct("multipart", "mixed");
    ct.paramList().push_back(ContentType::Param("boundary", boundary));
    m_header.contentType(ct);
}

MultipartParallel::MultipartParallel()
{
    ContentType::Boundary boundary;
    ContentType ct("multipart", "parallel");
    ct.paramList().push_back(ContentType::Param("boundary", boundary));
    m_header.contentType(ct);
}

MultipartAlternative::MultipartAlternative()
{
    ContentType::Boundary boundary;
    ContentType ct("multipart", "alternative");
    ct.paramList().push_back(ContentType::Param("boundary", boundary));
    m_header.contentType(ct);
}

MultipartDigest::MultipartDigest()
{
    ContentType::Boundary boundary;
    ContentType ct("multipart", "digest");
    ct.paramList().push_back(ContentType::Param("boundary", boundary));
    m_header.contentType(ct);
}

ApplicationOctStream::ApplicationOctStream()
{
    m_header.contentType("application/octet-stream");
}

MessageRfc822::MessageRfc822(const MimeEntity& me)
: m_me(me)
{
    m_header.contentType("message/rfc822");
}

ostream& MessageRfc822::write(ostream& os, const char* WXUNUSED(eol)) const
{
    MimeEntity::write(os);
    return os << m_me;
}

string ApplicationOctStream::type() const
{
    return m_header.contentType().param("type");
}

void ApplicationOctStream::type(const string& type)
{
    ContentType ct = m_header.contentType();
    ct.param("type",type);
    m_header.contentType(ct);
}

unsigned int ApplicationOctStream::padding() const
{
    return utils::str2int(m_header.contentType().param("padding"));
}

void ApplicationOctStream::padding(unsigned int n)
{
    ContentType ct = m_header.contentType();
    ct.param("padding", utils::int2str(n));
    m_header.contentType(ct);
}


}
