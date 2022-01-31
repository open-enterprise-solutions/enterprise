/***************************************************************************
    copyright            : (C) 2002-2008 by Stefano Barbato
    email                : stefano@codesink.org

    $Id: mimeentitylist.h,v 1.8 2008-10-07 11:06:25 tat Exp $
 ***************************************************************************/

/***************************************************************************

 Licence:     wxWidgets licence

 This file has been copied from the project Mimetic
 (http://codesink.org/mimetic_mime_library.html) and relicenced from the MIT
 licence to the wxWidgets one with authorisation received from Stefano Barbato

 ***************************************************************************/

#ifndef _MIMETIC_MIME_ENTITY_LIST_
#define _MIMETIC_MIME_ENTITY_LIST_

#include <list>
#include <string>

namespace mimetic
{

class MimeEntity;

/// List of MimeEntity classes
typedef std::list<MimeEntity*> MimeEntityList;


}

#endif
