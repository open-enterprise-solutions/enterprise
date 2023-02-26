/***************************************************************************
	copyright            : (C) 2002-2008 by Stefano Barbato
	email                : stefano@codesink.org

	$Id: strutils.cxx,v 1.3 2008-10-07 11:06:26 tat Exp $
 ***************************************************************************/

 /***************************************************************************

  Licence:     wxWidgets licence

  This file has been copied from the project Mimetic
  (http://codesink.org/mimetic_mime_library.html) and relicenced from the MIT
  licence to the wxWidgets one with authorisation received from Stefano Barbato

  ***************************************************************************/

#include <3rdparty/email/mimetic/strutils.h>

namespace mimetic
{
	using namespace std;

	const string nullstring;

	string canonical(const string& s, bool no_ws)
	{
		if (s.empty())
			return s;
		string input = s;
		// removes leading spaces
#if defined(_LP64) || defined(__LP64__) || defined(__arch64__) || defined(_WIN64)
		long long idx = 0;
#else 
		int idx = 0;
#endif 
		while (input[idx] == ' ')
			idx++;
		if (idx)
			input.erase(0, idx);
		// removes trailing spaces
		idx = input.length() - 1;
		while (input[idx] == ' ')
			idx--;
		input.erase(idx, input.length() - (idx + 1));
		idx++;
		// removes rfc822 comments and non-required spaces
		bool in_dquote = false, has_brack = false;
#if defined(_LP64) || defined(__LP64__) || defined(__arch64__) || defined(_WIN64)
		long long in_par = 0, in_brack = 0, par_last = 0;
#else 
		int in_par = 0, in_brack = 0, par_last = 0;
#endif 

#if defined(_LP64) || defined(__LP64__) || defined(__arch64__) || defined(_WIN64)
		for (long long t = input.length() - 1; t >= 0; --t)
#else 
		for (int t = input.length() - 1; t >= 0; --t)
#endif 
		{
			if (input[t] == '"') {
				in_dquote = !in_dquote;
			}
			else if (in_dquote) {
				continue;
			}
			else if (input[t] == '<') {
				in_brack--;
			}
			else if (input[t] == '>') {
				has_brack = true;
				in_brack++;
			}
			else if (input[t] == ')') {
				in_par++;
				if (in_par == 1)
					par_last = t;
			}
			else if (input[t] == '(') {
				in_par--;
				if (in_par == 0)
				{
					input.erase(t, 1 + par_last - t);
					// comments will be replaced with a space in
					// !no_ws
					if (!no_ws)
						input.insert(t, " ");
				}
			}
			else if (no_ws && input[t] == ' ' && !in_par && !has_brack) {
				input.erase(t, 1);
			}
		}
		return input;
	}

}
