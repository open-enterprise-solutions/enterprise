#ifndef _MIMETIC_ITPARSER_DECL_H_
#define _MIMETIC_ITPARSER_DECL_H_

/***************************************************************************

 Licence:     wxWidgets licence

 This file has been copied from the project Mimetic
 (http://codesink.org/mimetic_mime_library.html) and relicenced from the MIT
 licence to the wxWidgets one with authorisation received from Stefano Barbato

 ***************************************************************************/

namespace mimetic
{

// gcc gives a warning if I move this into  IteratorParser<Iterator, std::input_iterator_tag>
typedef unsigned int ParsingElem;

/**
 * Ignore Mask
 * constants to use with load(...) functions if you don't want to load
 * in memory the whole message but just some parts of it
 * to save execution memory and time
 */
enum {
    imNone          = 0,
    imHeader        = 1 << 6,
    imBody          = 1 << 7,
    imChildParts    = 1 << 8,
    imPreamble      = 1 << 9,
    imEpilogue      = 1 << 10
};

// forward declaration
template<typename Iterator, typename ItCategory>
struct IteratorParser;

}

#endif

