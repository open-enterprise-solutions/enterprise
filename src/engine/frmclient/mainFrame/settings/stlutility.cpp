#include "stlutility.h"

void ReplaceAll(std::string& string, const std::string& find, const std::string& sub)
{
#if defined(_LP64) || defined(__LP64__) || defined(__arch64__) || defined(_WIN64)
	long long pos = 0;
#else 
	int pos = 0;
#endif 
	while (pos != -1)
	{
		pos = string.find(find, pos);

		if (pos != -1)
		{
			string.replace(string.begin() + pos, string.begin() + pos + find.length(), sub);
			pos += sub.length();
		}
	}
}

std::string TrimSpaces(const std::string& string)
{
	const char* whitespace = " \t\n";

	size_t start = string.find_first_not_of(whitespace);
	size_t end = string.find_last_not_of(whitespace);

	if (start == std::string::npos) return std::string();

	return string.substr(start, end - start + 1);
}

std::string GetDirectory(const std::string& strFileName)
{
	size_t slash = strFileName.find_last_of("\\/");

	if (slash == std::string::npos) slash = 0;
	return strFileName.substr(0, slash);

}

bool GetIsSlash(const char &c)
{
	return c == '\\' || c == '/';
}
