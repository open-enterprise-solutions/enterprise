/***************************************************************************
    copyright            : (C) 2002-2008 by Stefano Barbato
    email                : stefano@codesink.org

    $Id: header.h,v 1.12 2008-10-07 11:06:25 tat Exp $
 ***************************************************************************/

/***************************************************************************

 Licence:     wxWidgets licence

 This file has been copied from the project Mimetic
 (http://codesink.org/mimetic_mime_library.html) and relicenced from the MIT
 licence to the wxWidgets one with authorisation received from Stefano Barbato

 ***************************************************************************/

#ifndef _MIMETIC_HEADER_H_
#define _MIMETIC_HEADER_H_

#include <string>
#include <algorithm>
#include <3rdparty/email/mimetic/rfc822/header.h>
#include <3rdparty/email/mimetic/mimeversion.h>
#include <3rdparty/email/mimetic/contenttype.h>
#include <3rdparty/email/mimetic/contentid.h>
#include <3rdparty/email/mimetic/contenttransferencoding.h>
#include <3rdparty/email/mimetic/contentdisposition.h>
#include <3rdparty/email/mimetic/contentdescription.h>

namespace mimetic
{

/// MIME message header class
struct Header: public Rfc822Header
{
    const MimeVersion& mimeVersion() const;
    MimeVersion& mimeVersion();
    void mimeVersion(const MimeVersion&);

    const ContentType& contentType() const;
    ContentType& contentType();
    void contentType(const ContentType&);

    const ContentTransferEncoding& contentTransferEncoding() const;
    ContentTransferEncoding& contentTransferEncoding();
    void contentTransferEncoding(const ContentTransferEncoding&);

    const ContentDisposition& contentDisposition() const;
    ContentDisposition& contentDisposition();
    void contentDisposition(const ContentDisposition&);

    const ContentDescription& contentDescription() const;
    ContentDescription& contentDescription();
    void contentDescription(const ContentDescription&);

    const ContentId& contentId() const;
    ContentId& contentId();
    void contentId(const ContentId&);
};

}

#endif
