/////////////////////////////////////////////////////////////////////////////
// Name:        forstrnu.h
// Purpose:     wxFormatStringAsNumber header
// Author:      Manuel Martin
// Modified by: MM Apr 2012
// Created:     Mar 2003
// License:     wxWindows license
/////////////////////////////////////////////////////////////////////////////

#ifndef _FORSTRNU_H
#define _FORSTRNU_H

enum
{
	wxUF_BESTRICT = 1,       //be strict when unformatting
	wxUF_NOSTRICT = 2,       //be tolerant
	wxTZ_LEFT = 4,                       //trim zeros at left
	wxTZ_RIGHT = 8,                       //trim zeros at right
	wxTZ_BOTH = wxTZ_LEFT | wxTZ_RIGHT,   //trim zeros both sides
};

struct stringParts
{
	wxString sDigits;
	bool sSign;
	long sExp;
};

/////////////////////////////////////////////////////////////////////////////
class wxFormatStringAsNumber
{
public:
	wxFormatStringAsNumber();
	wxFormatStringAsNumber(const wxString& formatStyle);
	~wxFormatStringAsNumber();

	//Analyze the style, formed with at most three substyles.
	//If the style/s are OK, current stored style is replaced.
	//Returns '-1' if no errors
	// or the position (0-based) where first error
	int SetStyle(const wxString& formatStyle);

	//Returns the current stored style
	wxString GetStyle() { return m_formatStyle; }

	//Returns the string used as decimal separator in first substyle
	wxString GetDecSep() { return m_Sstypos.decsep; }

	//Returns the last error message, wxEmptyString if none
	wxString GetLastError() { return m_lasterror; }

	//Analyze strNum to be a computer-style number.
	//Because there´s no format, it is quick.
	//Before using it, be sure strNum has no tabs, blanks, etc.
	//Returns '-1' if no errors.
	// or the position (0-based) where first error.
	int QCheck(const wxString& strNum);

	//Formats strNumIn into strNumOut
	//beStrict == wxUF_BESTRICT calls QCheck() before formatting.
	//beStrict == wxUF_NOSTRICT doesn't. So, strNumOut may become incomplete
	// (not a number) but formatted as much as possible.
	//Returns '-1' if no errors
	// or the position (0-based) where first error
	int FormatStr(const wxString& strNumIn,
		wxString& strNumOut,
		int beStrict = wxUF_BESTRICT,
		bool doRound = false);

	//Unformats strIn into strOut
	//strIn is supposed to be formatted with current style
	//beStrict == wxUF_BESTRICT will return error if strIn is not
	// exactly well style-formatted
	//beStrict == wxUF_NOSTRICT is tolerant, stripping anything but
	// digits, signs, decimal char and exponential char. Because
	// of this, num's sign may be not clear. beStrict == wxUF_NOSTRICT
	// only uses the first (or better, the only) substyle. So be sure
	// this substyle includes the sign.
	//Produces a string to use in a 'string-to-number' function.
	//Returns '-1' if no errors
	// or the position (0-based) where first error
	int UnFormatStr(const wxString& strIn,
		wxString& strOut,
		int beStrict = wxUF_BESTRICT);

	//Trim zeros, by default at left
	void TrimZeros(wxString& strNum, int atside = wxTZ_LEFT);

	//Digits, sign and exponent
	void StringToParts(stringParts* sP);

private:

	struct stFSNData
	{
		wxString   sty;     //sub-style
		size_t     ig_sty;  //position of group's beginning
		size_t     fg_sty;  //position of group's finish
		wxString   decsep;  //string for decimal separator
		size_t     idecse;  //decimal separator position
		size_t     iexpse;  //exponential position
		int        typExp;  //type of exponential (normal, scientific, engineer)
		wxArrayInt Asty;    //types of char in sub-style
		wxArrayInt AminD;   //min number or required digits
		size_t     isuffix; //position of suffix, length of sub-style if no suffix
		int        iprefix; //position of right-most in prefix. -1 if there's no prefix
		bool       reqDec;  //required (0 b) in decimal part?
		bool       reqExp;  //required (0 b) in exponential part?
		int        iround;  //number of decimals for rounding off
	};
	stFSNData m_Sstypos, m_Sstyneg, m_Sstyzer;

	enum {
		cIsDigit = 1, cIsForced, cIsBL, cIsAllDig, cIsSignp, cIsSignn,
		cIsSignpE, cIsSignnE, cIsChar, cIsDecSep, cIsExpU, cIsExpL
	};

	enum { eExpNorm = 1, eExpSci, eExpEng };

	//Used everywhere
	wxUniChar m_cZero;  // wxT('0')
	wxUniChar m_cPlus;  // wxT('+')
	wxUniChar m_cMinus; // wxT('-')
	wxUniChar m_cExpU;  // wxT('E')
	wxUniChar m_cExpL;  // wxT('e')
	wxUniChar m_cDot;   // wxT('.')

	//Initialize the wxUniChar above
	void Init();

	wxString m_formatStyle;
	wxString m_lasterror;

	//The format routine
	int StrFor(int usesty, const wxString& srIn, wxString& srOut);

	//The unformat routine
	int StrUnf(int usesty, const wxString& srIn, wxString& srOut, int beStc);

	//A function used to know 'minimum digits' (see implementation)
	void FillArrayMinDig(wxArrayInt *arrMinDig, wxArrayInt *pArray,
		wxString *psty, size_t ig_sty, size_t fg_sty,
		int *prefixpos, size_t *suffixpos);

	//A function used several times in StrUnf()
	int WhatToDoWithThisChar(
		int numDigSty, bool inRequiredZone, int* numDigIn,
		const wxUniChar& cI, const wxUniChar& cS, int stychartype, int beStc,
		bool *signIn, bool *signExIn, bool *gotN, bool *gotS);

	//Makes rounding off and prepare for Sci/Eng style
	void RoundAndForm(int usesty, wxString& srNum, bool doRound);
};

#endif //_FORSTRNU_H

