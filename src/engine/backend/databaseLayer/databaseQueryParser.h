#ifndef _DATABASEQUERYPARSER_H_
#define _DATABASEQUERYPARSER_H_

#include "core/fstring.h"   // ibString, ibT

#include <vector>

std::vector<ibString> ParseQueries(const ibString& strQuery);

#endif // _DATABASEQUERYPARSER_H_
