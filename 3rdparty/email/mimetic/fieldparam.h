/***************************************************************************
    copyright            : (C) 2002-2008 by Stefano Barbato
    email                : stefano@codesink.org

    $Id: fieldparam.h,v 1.7 2008-10-07 11:06:25 tat Exp $
 ***************************************************************************/

/***************************************************************************

 Licence:     wxWidgets licence

 This file has been copied from the project Mimetic
 (http://codesink.org/mimetic_mime_library.html) and relicenced from the MIT
 licence to the wxWidgets one with authorisation received from Stefano Barbato

 ***************************************************************************/

#ifndef _MIMETIC_FIELD_PARAM_H_
#define _MIMETIC_FIELD_PARAM_H_

#include <string>
#include <iostream>
#include <list>
#include <email/mimetic/strutils.h>

namespace mimetic
{

/// Field param
struct FieldParam
{
    FieldParam();
    FieldParam(const std::string&);
    FieldParam(const std::string&, const std::string&);
    const istring& name() const;
    const std::string& value() const;
    void name(const std::string&);
    void value(const std::string&);
    friend std::ostream& operator<<(std::ostream&, const FieldParam&);
private:
    istring m_name;
    std::string m_value;
};

typedef std::list<FieldParam> FieldParamList;
}

#endif
