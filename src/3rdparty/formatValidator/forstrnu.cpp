/////////////////////////////////////////////////////////////////////////////
// Name:        forstrnu.cpp
// Purpose:     wxFormatStringAsNumber
// Author:      Manuel Martin
// Modified by: MM Apr 2012
// Created:     Mar 2003
// License:     wxWindows license
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#ifdef __BORLANDC__
#pragma hdrstop
#endif

#ifndef WX_PRECOMP
#include  "wx/string.h"
#include  "wx/dynarray.h"
#include  "wx/intl.h"
#endif

#include "forstrnu.h"

/////////////////////////////////////////////////////////////////////////////
//For what I see, this function will work with any character encoding
static bool fs_IsDigit(const wxUniChar& c)
{
	static wxUniChar uc0 = wxT('0');
	static wxUniChar uc9 = wxT('9');
	return c >= uc0 && c <= uc9;
}

wxFormatStringAsNumber::wxFormatStringAsNumber()
{
	Init();
}

wxFormatStringAsNumber::wxFormatStringAsNumber(
	const wxString& formatStyle)
{
	Init();
	SetStyle(formatStyle);
}

void wxFormatStringAsNumber::Init()
{
	//Initialize these wxUniChar
	m_cZero = wxT('0');
	m_cPlus = wxT('+');
	m_cMinus = wxT('-');
	m_cExpU = wxT('E');
	m_cExpL = wxT('e');
	m_cDot = wxT('.');
}

wxFormatStringAsNumber::~wxFormatStringAsNumber()
{
}


/////////////////////////////////////////////////////////////////////////////
/*Style chars:
   0   : Sets '0' (zero) if no digit, or the digit if any.
   #   : Sets the digit if any.
   *   : For decimal and exponential parts, show all existing digits.
   b   : Sets next char in style (typically blank ' ') if no digit
   .   : Next char marks the beginning of the decimal
		  separator. The same char marks end of this separator
		  Typically this char would be "'".
   []  : Repeat this group. Only one group is allowed
		  No digits (#0b) are allowed before group.
   +   : Set always the sign.
   -   : Set the sign if the number is negative.
   @   : Next char is literal; useful for showing
		  a command-character, such as '#'.
   E   : Exponential.
   e   : Same. Use lower case letter 'e' instead of 'E'
		  One digit needed at least before exponential.
		  Groups not allowed after exponential.
		  Exponential is only used if both number and style have
		  it, or if 'forced' (0 b) are used in exponential.
   S   : Scientific notation i.e. "1.2345E+10"
   s   : same, lower letter 'e'.
   N   : Engineer notation i.e. "12.345E+09"
   n   : same, lower letter 'e'.
   ;   : Styles separator. May be styles for positive,
		  negative and zero values (in this order).
		  If only one style is provided, it is used for
		  all numbers. If two styles, the first is used
		  for positive and zero, the second for negative.

 If style has two signs, after 'E' or 'e', first sign is
   intended for exponential and second for number

 Nasty things as thousandSeparator == decimalSeparator  or
  decimalSeparator == "33" surely leads to bad unformatting
*/

/////////////////////////////////////////////////////////////////////////////
//Analyze the style and store it.
//Return -1 if everything is OK, or the error's position
int wxFormatStringAsNumber::SetStyle(const wxString& formatStyle)
{
	size_t stylen = formatStyle.Len();
	if (stylen == 0)
	{
		m_lasterror = _("\nNo style");
		wxFAIL_MSG(m_lasterror);
		return 0;
	}

	if (m_formatStyle.Cmp(formatStyle) == 0)
		return -1; //we already have it

	stFSNData Stypos, Styneg, Styzer;
	// For formatting/unformatting the wxArrayInt from stFSNData is what is
	// used, not the 'formatStyle'.
	int pindex = -1; //used for positions in this wxArrayInt

	wxString *pTsty = &(Stypos.sty);    //the sub-style
	size_t *pigroup = &(Stypos.ig_sty); //position of group's beginning
	size_t *pfgroup = &(Stypos.fg_sty); //position +1 of group's finish
	wxString *pdecsep = &(Stypos.decsep); //decimal separator
	size_t *pidecsep = &(Stypos.idecse); //decimal separator position
	size_t *pexp = &(Stypos.iexpse); //exponential position
	int *ptypexp = &(Stypos.typExp); //type of exponential
	wxArrayInt *pasty = &(Stypos.Asty);   //types of char in style
	bool *pReqDec = &(Stypos.reqDec); //0 or b in decimal part
	bool *pReqExp = &(Stypos.reqExp); //0 or b in exponential part
	int  *piround = &(Stypos.iround); //number of decimals for rounding off
	*pReqDec = false;
	*pReqExp = false;
	*pigroup = 0;
	*pfgroup = 0;
	*pidecsep = 0;
	*pexp = 0;
	*ptypexp = 0;
	*piround = 0;

	size_t i;
	bool binsep = false; //are we inside a separator?
	bool bdecAlready = false; //is decimal separator defined already?
	bool bExpAlready = false;
	bool bgroAlready = false; //group has begun
	bool bfgrAlready = false; //finished group
	bool bdigit = false; //any digit
	bool bnumInGroup = false; //is there any number inside group or exp?
	bool bsgnAlready = false; //number's sign
	bool bsgEAlready = false; //exponential's sign
	bool bdigAlready = false; // '#' found
	bool ballAlready = false; // '*' found
	wxUniChar c;
	wxUniChar s; //separator delimiter
	wxUniChar cAt = wxT('@');
	wxUniChar cLb = wxT('[');
	wxUniChar cRb = wxT(']');
	wxUniChar cSU = wxT('S');
	wxUniChar cSL = wxT('s');
	wxUniChar cNU = wxT('N');
	wxUniChar cNL = wxT('n');
	wxUniChar cBe = wxT('b');
	wxUniChar cHa = wxT('#');
	wxUniChar cAk = wxT('*');
	wxUniChar cSc = wxT(';');

	m_lasterror = wxEmptyString;

	for (i = 0; i < stylen; i++)
	{
		c = formatStyle.GetChar(i);
		if (binsep) //inside decimal separator, all chars are literal
		{
			if (c == s) //separator finishes here
			{
				if (pdecsep->Len() == 0)
				{
					m_lasterror = _("\nBad style: decimal separator");
					wxFAIL_MSG(m_lasterror);
					return i;
				}
				binsep = false;
				bdigAlready = false;
				ballAlready = false;
			}
			else
			{
				*pTsty << c; //store the style's char and then its type
				pindex++;
				pasty->Add(cIsDecSep);
				*pdecsep << c;
			}
			continue;
		}
		if (c == cAt) //'@'  next char is literal
		{
			i++;
			if (i < stylen)
			{
				c = formatStyle.GetChar(i);
				*pTsty << c; //store the style's char and then its type
				pindex++;
				pasty->Add(cIsChar);
				continue;
			}
			else
			{
				m_lasterror = _("\nBad style: unexpected '@' at end");
				wxFAIL_MSG(m_lasterror);
				return i;
			}
		}
		if (c == cLb) //'['  group's beginning
		{
			if (bdigit || bgroAlready || bdecAlready || bExpAlready)
			{
				m_lasterror = _("\nBad style: 'group'");
				wxFAIL_MSG(m_lasterror);
				return i;
			}
			else
			{
				*pigroup = (size_t)(pindex + 1);
				bgroAlready = true;
				bnumInGroup = false;
				continue;
			}
		}
		if (c == cRb) //']'  group's finish
		{
			if (!bgroAlready || bdecAlready || bExpAlready || bfgrAlready
				|| !bnumInGroup)
			{
				m_lasterror = _("\nBad style: 'group'");
				wxFAIL_MSG(m_lasterror);
				return i;
			}
			else
			{
				*pfgroup = (size_t)(pindex + 1); //position + 1
				bfgrAlready = true;
				bnumInGroup = false;
				continue;
			}
		}
		if (c == m_cDot) //'.'
		{
			if (bdecAlready || bExpAlready || (bgroAlready && !bfgrAlready))
			{
				m_lasterror = _("\nBad style: 'decimal separator'");
				wxFAIL_MSG(m_lasterror);
				return i;
			}
			i++;
			if (i < stylen)
			{
				s = formatStyle.GetChar(i);
				binsep = true;
				bdecAlready = true;
				bnumInGroup = false;
				*pidecsep = (size_t)(pindex + 1);
				continue;
			}
			else
			{
				m_lasterror = _("\nBad style: unexpected '.' at end");
				wxFAIL_MSG(m_lasterror);
				return i;
			}
		}
		if (c == m_cPlus || c == m_cMinus) //'+' '-'
		{
			if (bExpAlready)
			{
				if (bsgEAlready)
					if (bsgnAlready)
					{
						m_lasterror = _("\nBad style: 'many signs'");
						wxFAIL_MSG(m_lasterror);
						return i;
					}
					else
					{
						*pTsty << c;
						bsgnAlready = true;
						pindex++;
						if (c == m_cPlus)
							pasty->Add(cIsSignp);
						else
							pasty->Add(cIsSignn);
						continue;
					}
				else
				{
					*pTsty << c;
					bsgEAlready = true;
					pindex++;
					if (c == m_cPlus)
						pasty->Add(cIsSignpE);
					else
						pasty->Add(cIsSignnE);
					continue;
				}
			}
			if (bsgnAlready)
			{
				m_lasterror = _("\nBad style: 'sign'");
				wxFAIL_MSG(m_lasterror);
				return i;
			}
			else if (bgroAlready && !bfgrAlready)
			{
				m_lasterror = _("\nBad style: 'sign inside group'");
				wxFAIL_MSG(m_lasterror);
				return i;
			}
			else
			{
				*pTsty << c;
				bsgnAlready = true;
				pindex++;
				if (c == m_cPlus)
					pasty->Add(cIsSignp);
				else
					pasty->Add(cIsSignn);
				continue;
			}
		}
		if (c == m_cExpU || c == m_cExpL //'E' 'e'
			|| c == cSU || c == cSL      //'S' 's'
			|| c == cNU || c == cNL)    //'N' 'n'
		{
			if (!bdigit || bExpAlready || (bgroAlready && !bfgrAlready))
			{
				m_lasterror = _("\nBad style: 'exponential'");
				wxFAIL_MSG(m_lasterror);
				return i;
			}
			else
			{
				if (c == m_cExpU || c == cSU || c == cNU)
				{
					*pTsty << m_cExpU;
					pasty->Add(cIsExpU);
				}
				else
				{
					*pTsty << m_cExpL;
					pasty->Add(cIsExpL);
				}
				bExpAlready = true;
				bnumInGroup = false;
				bdigAlready = false;
				ballAlready = false;
				pindex++;
				*pexp = (size_t)pindex;
				if (c == cSU || c == cSL)
					*ptypexp = eExpSci;
				else if (c == cNU || c == cNL)
					*ptypexp = eExpEng;
				else
					*ptypexp = eExpNorm;
				continue;
			}
		}
		if (c == cBe) //'b'  next char is literal to use when no digit
		{
			if (bdecAlready && !bExpAlready && (bdigAlready || ballAlready))
			{
				m_lasterror = _("\nBad style: 'b' after '#' or '*' in decimal part");
				wxFAIL_MSG(m_lasterror);
				return i;
			}
			i++;
			if (i < stylen)
			{
				c = formatStyle.GetChar(i);
				*pTsty << c; //store the style's char and then its type
				pindex++;
				bdigit = true;
				pasty->Add(cIsBL);
				if (bdecAlready && !bExpAlready)
				{
					*pReqDec = true; //'forced' in decimal part
					(*piround)++; //take into account for rounding
				}
				if (bExpAlready)
					*pReqExp = true; //'forced in exponential part

				bnumInGroup = true;
				continue;
			}
			else
			{
				m_lasterror = _("\nBad style: unexpected 'b' at end");
				wxFAIL_MSG(m_lasterror);
				return i;
			}
		}
		if (c == m_cZero) //'0'
		{
			if (bdecAlready && !bExpAlready && (bdigAlready || ballAlready))
			{
				m_lasterror = _("\nBad style: '0' after '#' or '*' in decimal part'");
				wxFAIL_MSG(m_lasterror);
				return i;
			}
			*pTsty << c;
			pindex++;
			bdigit = true;
			pasty->Add(cIsForced);
			if (bdecAlready && !bExpAlready)
			{
				*pReqDec = true; //'forced' in decimal part
				(*piround)++; //take into account for rounding
			}
			if (bExpAlready)
				*pReqExp = true; //'forced in exponential part
			bnumInGroup = true;
			continue;
		}
		if (c == cHa) //'#'
		{
			if (bdecAlready && !bExpAlready && ballAlready)
			{
				m_lasterror = _("\nBad style: '#' after '*' in decimal part'");
				wxFAIL_MSG(m_lasterror);
				return i;
			}
			*pTsty << c;
			pindex++;
			bdigit = true;
			pasty->Add(cIsDigit);
			if (bdecAlready && !bExpAlready)
				(*piround)++; //take into account for rounding
			bdigAlready = true;
			bnumInGroup = true;
			continue;
		}
		if (c == cAk) //'*'
		{
			if (!bdecAlready && !bExpAlready)
			{
				m_lasterror = _("\nBad style: '*' not allowed in integer part");
				wxFAIL_MSG(m_lasterror);
				return i;
			}
			if (bdecAlready && !bExpAlready && (bdigAlready || ballAlready))
			{
				m_lasterror = _("\nBad style: '*' after '#' or '*' in decimal part");
				wxFAIL_MSG(m_lasterror);
				return i;
			}
			if (bExpAlready && bnumInGroup)
			{
				m_lasterror = _("\nBad style: '*' after digit in exponential part");
				wxFAIL_MSG(m_lasterror);
				return i;
			}
			*pTsty << c;
			pindex++;
			bdigit = true;
			pasty->Add(cIsAllDig);
			if (bdecAlready && !bExpAlready)
				*piround = -1; //no round off
			ballAlready = true;
			bnumInGroup = true;
			continue;
		}

		if (c == cSc) //';' styles separator
		{
			if (pTsty->Len() == 0 || binsep || (bgroAlready && !bfgrAlready)
				|| (bExpAlready && !bnumInGroup))
			{
				m_lasterror = _("\nBad style: 'styles separator'");
				wxFAIL_MSG(m_lasterror);
				return i;
			}
			if (!bdigit && pTsty != &(Styzer.sty))
			{
				m_lasterror = _("\nBad style: 'no digit before styles separator'");
				wxFAIL_MSG(m_lasterror);
				return i;
			}
			binsep = false;
			bdecAlready = false;
			bExpAlready = false;
			bgroAlready = false;
			bfgrAlready = false;
			bdigit = false;
			bnumInGroup = false;
			bsgnAlready = false;
			bsgEAlready = false;
			bdigAlready = false;
			ballAlready = false;
			//let's point to next style
			pindex = -1;
			if (pTsty == &(Stypos.sty))
			{
				pTsty = &(Styneg.sty);
				pigroup = &(Styneg.ig_sty);
				pfgroup = &(Styneg.fg_sty);
				pdecsep = &(Styneg.decsep);
				pidecsep = &(Styneg.idecse);
				pexp = &(Styneg.iexpse);
				ptypexp = &(Styneg.typExp);
				pasty = &(Styneg.Asty);
				pReqDec = &(Styneg.reqDec);
				pReqExp = &(Styneg.reqExp);
				piround = &(Styneg.iround);
			}
			else if (pTsty == &(Styneg.sty))
			{
				pTsty = &(Styzer.sty);
				pigroup = &(Styzer.ig_sty);
				pfgroup = &(Styzer.fg_sty);
				pdecsep = &(Styzer.decsep);
				pidecsep = &(Styzer.idecse);
				pexp = &(Styzer.iexpse);
				ptypexp = &(Styzer.typExp);
				pasty = &(Styzer.Asty);
				pReqDec = &(Styzer.reqDec);
				pReqExp = &(Styzer.reqExp);
				piround = &(Styzer.iround);
			}
			else
			{
				m_lasterror = _("\nBad style: 'too many styles separators'");
				wxFAIL_MSG(m_lasterror);
				return i;
			}
			*pReqDec = false;
			*pReqExp = false;
			*pigroup = 0;
			*pfgroup = 0;
			*pidecsep = 0;
			*pexp = 0;
			*ptypexp = 0;
			*piround = 0;

			continue;
		}
		*pTsty << c;
		pindex++;
		pasty->Add(cIsChar);
	} //loop's end

	//perhaps the style doesn't finish properly
	if (binsep || (bgroAlready && !bfgrAlready) || (bExpAlready && !bnumInGroup))
	{
		m_lasterror = _("\nBad style: 'unexpected style's end'");
		wxFAIL_MSG(m_lasterror);
		return i;
	}
	if (!bdigit && pTsty != &(Styzer.sty))
	{
		m_lasterror = _("\nBad style: 'no digit in [sub]style'");
		wxFAIL_MSG(m_lasterror);
		return i;
	}

	//Style is OK, let's store it
	m_formatStyle = formatStyle;

	m_Sstypos.sty = Stypos.sty;
	m_Sstyneg.sty = Styneg.sty;
	m_Sstyzer.sty = Styzer.sty;
	m_Sstypos.ig_sty = Stypos.ig_sty;
	m_Sstyneg.ig_sty = Styneg.ig_sty;
	m_Sstyzer.ig_sty = Styzer.ig_sty;
	m_Sstypos.fg_sty = Stypos.fg_sty;
	m_Sstyneg.fg_sty = Styneg.fg_sty;
	m_Sstyzer.fg_sty = Styzer.fg_sty;
	m_Sstypos.decsep = Stypos.decsep;
	m_Sstyneg.decsep = Styneg.decsep;
	m_Sstyzer.decsep = Styzer.decsep;
	m_Sstypos.idecse = Stypos.idecse;
	m_Sstyneg.idecse = Styneg.idecse;
	m_Sstyzer.idecse = Styzer.idecse;
	m_Sstypos.iexpse = Stypos.iexpse;
	m_Sstyneg.iexpse = Styneg.iexpse;
	m_Sstyzer.iexpse = Styzer.iexpse;
	m_Sstypos.typExp = Stypos.typExp;
	m_Sstyneg.typExp = Styneg.typExp;
	m_Sstyzer.typExp = Styzer.typExp;
	m_Sstypos.Asty = Stypos.Asty;
	m_Sstyneg.Asty = Styneg.Asty;
	m_Sstyzer.Asty = Styzer.Asty;
	m_Sstypos.reqDec = Stypos.reqDec;
	m_Sstyneg.reqDec = Styneg.reqDec;
	m_Sstyzer.reqDec = Styzer.reqDec;
	m_Sstypos.reqExp = Stypos.reqExp;
	m_Sstyneg.reqExp = Styneg.reqExp;
	m_Sstyzer.reqExp = Styzer.reqExp;
	m_Sstypos.iround = Stypos.iround;
	m_Sstyneg.iround = Styneg.iround;
	m_Sstyzer.iround = Styzer.iround;

	//Fill arrays with data about minimum digits in style.
	// Also set prefix and suffix positions
	FillArrayMinDig(&(m_Sstypos.AminD), &(m_Sstypos.Asty), &(m_Sstypos.sty),
		m_Sstypos.ig_sty, m_Sstypos.fg_sty,
		&(m_Sstypos.iprefix), &(m_Sstypos.isuffix));
	if (m_Sstyneg.sty.Len() > 0)
		FillArrayMinDig(&(m_Sstyneg.AminD), &(m_Sstyneg.Asty), &(m_Sstyneg.sty),
			m_Sstyneg.ig_sty, m_Sstyneg.fg_sty,
			&(m_Sstyneg.iprefix), &(m_Sstyneg.isuffix));
	if (m_Sstyzer.sty.Len() > 0)
		FillArrayMinDig(&(m_Sstyzer.AminD), &(m_Sstyzer.Asty), &(m_Sstyzer.sty),
			m_Sstyzer.ig_sty, m_Sstyzer.fg_sty,
			&(m_Sstyzer.iprefix), &(m_Sstyzer.isuffix));

	//Clear error message
	m_lasterror = wxEmptyString;

	return -1; //everything was OK
}

/////////////////////////////////////////////////////////////////////////////
//Do some checks and then format the string
/////////////////////////////////////////////////////////////////////////////
int wxFormatStringAsNumber::FormatStr(const wxString& strNumIn,
	wxString& strNumOut,
	int beStrict,
	bool doRound)
{
	strNumOut = wxEmptyString;
	m_lasterror = wxEmptyString;

	//Let's test strNumIn to be a 'number'
	if (strNumIn.IsEmpty()) //nothing to format
		return -1;

	if (m_formatStyle.Len() == 0)
	{
		m_lasterror = _("No style. No format done");
		return (int)(strNumIn.Len());
	}

	wxString strNup(strNumIn);
	int firstNonSpc;
	int posIn = 0;

	strNup.Trim(false); //remove spaces ('\t' '\v' '\f' '\r' '\n' ' ') from the left
	strNup.Trim(); //remove spaces from the right
	int lenNup = (int)strNup.Len();
	if (lenNup == 0)
		return -1; //everything skipped
	firstNonSpc = strNumIn.Find(strNup.GetChar(0));

	if (beStrict == wxUF_BESTRICT)
	{   //check strNup to represent a valid 'C-locale' number
		posIn = QCheck(strNup);
		if (posIn >= 0)
		{
			m_lasterror = _("Error: not a number");
			return (posIn + firstNonSpc);
		}
	}

	size_t nusty = 1, seemsty = 3, usesty;
	//how many substyles we have?. If beStrict != wxUF_BESTRICT, use first one.
	if (m_Sstyneg.sty.Len() > 0 && beStrict == wxUF_BESTRICT)
		nusty = 2;
	if (m_Sstyzer.sty.Len() > 0 && beStrict == wxUF_BESTRICT)
	{
		nusty = 3;
		wxUniChar c;
		//analyze what sub-style to use
		for (posIn = 0; posIn < lenNup; posIn++) //check to use m_styzer style
		{
			c = strNup.GetChar((size_t)posIn);
			if (fs_IsDigit(c) && c != m_cZero)
			{   //we found a (1-9) digit, so the number is really non zero
				seemsty = 1; //seems we will use first style
				break;
			}
		}
	}
	else
		seemsty = 1;

	if (nusty == 3 && seemsty == 3)
		usesty = 3; //for zero
	else if (nusty > 1 && seemsty < 3 && strNup.GetChar(0) == m_cMinus)
		usesty = 2; //for negative number
	else
		usesty = 1;

	//Do rounding off and prepare strNup for Sci/Eng sub-styles
	RoundAndForm(usesty, strNup, doRound);

	//Now we can format the string
	posIn = StrFor(usesty, strNup, strNumOut);
	if (posIn != -1)
		return (posIn + firstNonSpc);

	m_lasterror = wxEmptyString;
	return -1;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
int wxFormatStringAsNumber::UnFormatStr(const wxString& strIn,
	wxString& strOut,
	int beStrict)
{
	strOut = wxEmptyString;
	m_lasterror = wxEmptyString;
	if (strIn.IsEmpty()) //nothing to unformat
		return -1;

	if (m_formatStyle.Len() == 0)
	{
		m_lasterror = _("No style. No unformat done");
		return (int)(strIn.Len());
	}

	//how many substyles we have?
	size_t nusty = 1;
	if (m_Sstyzer.sty.Len() > 0)
		nusty = 3;
	else if (m_Sstyneg.sty.Len() > 0)
		nusty = 2;

	//beStrict == wxUF_NOSTRICT only uses first substyle
	//beStrict == wxUF_BESTRICT uses all substyles. The first one that
	// has no errors is the right one to use.
	if (beStrict != wxUF_BESTRICT)
		beStrict = wxUF_NOSTRICT;

	int result1 = 0, result2 = 0, result3 = 0;
	wxString out1;

	result1 = StrUnf(1, strIn, out1, beStrict);
	if ((beStrict == wxUF_NOSTRICT) || (nusty == 1))
	{
		strOut = out1;
		return result1;
	}
	//Other possible styles are tested
	wxString out2, out3;
	wxString merror1, merror2;
	merror1 = m_lasterror;

	result2 = StrUnf(2, strIn, out2, beStrict);
	merror2 = m_lasterror;

	if (nusty == 3)
		result3 = StrUnf(3, strIn, out3, beStrict);

	if (nusty == 3 && result3 == -1) //first test to be 0
	{
		strOut = out3;
		return result3;
	}
	if (result1 == -1)
	{
		strOut = out1;
		m_lasterror = merror1;
		return result1;
	}
	if (nusty > 1 && result2 == -1)
	{
		strOut = out2;
		m_lasterror = merror2;
		return result2;
	}
	//none was good, return the first error
	if (result1 <= result2 &&
		((nusty == 3 && result1 <= result3) || nusty == 2))
	{
		strOut = out1;
		m_lasterror = merror1;
		return result1;
	}
	if (result2 <= result1 &&
		((nusty == 3 && result2 <= result3) || nusty == 2))
	{
		strOut = out2;
		m_lasterror = merror2;
		return result2;
	}
	strOut = out3;
	return result3;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
//A quick check to know if strNum is computer-style 'C-locale'
//Notes:
// "E5" is not allowed, because there are no digits before exponential
// "1E" is not allowed, because there are no digits after exponential
// "."  or "-" are not allowed because there are no digits
int wxFormatStringAsNumber::QCheck(const wxString& strNum)
{
	bool signAlready = false;
	bool decAlready = false;
	bool expAlready = false;
	bool sigEAlready = false;
	bool digAlready = false; //a digit
	wxUniChar c;
	size_t lenNum = strNum.Len();
	size_t posIn = 0;

	if (lenNum == 0)
		return -1; //don't return an error for an empty string

	while (posIn < lenNum)
	{
		c = strNum.GetChar(posIn);
		if (c == m_cMinus || c == m_cPlus)
		{
			if (expAlready)
			{
				if (sigEAlready || digAlready)
					return (int)posIn; //exp. sign duplicated or after exp. digits
				else
					sigEAlready = true;
			}
			else //before exponential
			{
				if (signAlready || digAlready || decAlready)
					return (int)posIn; //main sign is not first char
				else
					signAlready = true;
			}
		}
		else if (c == m_cExpU || c == m_cExpL)
		{
			if (!digAlready || expAlready)
				return (int)posIn; //no digits before exp. or duplicated
			else
			{
				digAlready = false;
				expAlready = true;
			}
		}
		else if (c == m_cDot)
		{
			if (decAlready || expAlready)
				return (int)posIn; //dec. separator duplicated or after exp.
			else
				decAlready = true;
		}
		else if (fs_IsDigit(c))
			digAlready = true;
		else
			return (int)posIn; //not valid char

		posIn++;
	}

	if (!digAlready)
		return (int)lenNum - 1; //no digits
	else
		return -1; //Good number
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
//Here is where really formatting is done, given 'usesty'.
//'srIn' must be a no-formatted string (i.e. -1234567.89e90)
// with no blanks, tabs, etc.
//We catch here two errors: exponential overflow and if number has exponential
// but style doesn't
int wxFormatStringAsNumber::StrFor(int usesty,
	const wxString& srIn,
	wxString& srOut)
{
	wxString sto = wxEmptyString;
	wxString stex = wxEmptyString;
	wxString *psty = (wxString*)NULL;
	wxArrayInt *pArray = (wxArrayInt*)NULL;
	size_t ig_sty = 0;
	size_t fg_sty = 0;
	wxUniChar pdecsep = m_cDot; //the decimal separator character

	//NOTE: we use negative values, so better 'int' instead of 'size_t'
	int posdec, posStydec = 0, posexp, posStyexp = 0;
	int stychartype;
	bool signIn, signExIn, signEIs;
	int posSty, posrIn, pos1sDigitIn, poser, posStyPart; //positions
	int pos1sReq, pos1sDig;
	int lenStrIn = (int)srIn.Len();
	int lenSty;
	wxUniChar c;

	stFSNData *stData;
	//data and decimal and exponential positions in sub-style
	if (usesty == 1)
		stData = &m_Sstypos;
	else if (usesty == 2)
		stData = &m_Sstyneg;
	else
		stData = &m_Sstyzer;

	psty = &(stData->sty);
	pArray = &(stData->Asty);
	ig_sty = stData->ig_sty;
	fg_sty = stData->fg_sty;
	posStydec = (int)stData->idecse;
	posStyexp = (int)stData->iexpse;

	lenSty = (int)pArray->GetCount();

	if (posStydec == 0 && pArray->Item(0) != cIsDecSep)
		posStydec = -1; //there is not decimal separator
	if (stData->typExp == 0)
		posStyexp = -1; //there is not exponential

	//get sign
	c = srIn.GetChar(0); // 1st char is sign or no-sign is sign = +
	if (c == m_cMinus)
		signIn = false;
	else
		signIn = true;

	//Some basic positions - - - - - - - - - - - - - - - - - - - - - -
	posexp = posdec = wxNOT_FOUND;
	for (int i = (int)srIn.Len() - 1; i >= 0; i--)
	{
		c = srIn.GetChar((size_t)i);
		if (posexp == wxNOT_FOUND && (c == m_cExpU || c == m_cExpL))
			posexp = i;
		if (c == pdecsep)
		{
			posdec = i;
			break;
		}
	}

	//search first digit before decimal separator
	pos1sDigitIn = 0;
	while (pos1sDigitIn < lenStrIn)
	{
		c = srIn.GetChar((size_t)pos1sDigitIn);
		if (c == pdecsep)
		{
			pos1sDigitIn = -1; //no digits. i.e. "-.12"
			break;
		}
		if (fs_IsDigit(c))
			break;
		pos1sDigitIn++;
	}


	//CallAsFunc: get from style the next char
	//to match (if digit) or insert (rest of chars)

	//1. All given digits before decimal separator, exponential or
	//   srIn's end, the first we get

	if (posdec == wxNOT_FOUND)
	{
		if (posexp != wxNOT_FOUND)
			posrIn = posexp - 1;
		else
			posrIn = lenStrIn - 1;
	}
	else
		posrIn = posdec - 1;

	poser = posrIn;

	//check control: posStyPart == 0 means decimal and exponential parts not done
	posStyPart = 0;
	//where do we start at style to format?
	if (posStydec == -1)
		if (posStyexp == -1)
		{
			posSty = lenSty - 1;
			posStyPart = lenSty;
		}
		else
			posSty = posStyexp - 1;
	else
		posSty = posStydec - 1;

	//backwards filling the digits
	while (posSty >= 0 && posrIn >= 0 && pos1sDigitIn >= 0 && posrIn >= pos1sDigitIn)
	{
		c = srIn.GetChar((size_t)posrIn);

		while (posSty >= 0)
		{
			stychartype = pArray->Item((size_t)posSty);
			if (stychartype == cIsDigit || stychartype == cIsForced
				|| stychartype == cIsBL)
			{
				sto = c + sto; //get the digit
			 //remember fg_sty is position + 1 of right-most char inside group
				if (fg_sty > 0 && ig_sty == (size_t)posSty //reached beginning of group
					&& posrIn > pos1sDigitIn) //and more digits may be formatted
					posSty = (int)fg_sty - 1; //go to group's finish (at right)
				else
					posSty--;
				break;
			}
			else //insert all [no-digit] chars till next digit in style
			{
				if (stychartype == cIsSignp)
					if (signIn)
						sto = m_cPlus + sto;
					else
						sto = m_cMinus + sto;
				else if (stychartype == cIsSignn)
				{
					if (!signIn)
						sto = m_cMinus + sto;
				}
				else if (stychartype == cIsChar)
					sto = psty->GetChar((size_t)posSty) + sto;
			}
			if (fg_sty > 0 && ig_sty == (size_t)posSty //reached beginning of group
				&& posrIn >= pos1sDigitIn) //and this or more digits may be formatted
				posSty = (int)fg_sty - 1; //go to group's finish
			else
				posSty--;
		}

		posrIn--;
	}

	//2. Rest of required digits ('b' or '0') before decimal separator - - -
	for (pos1sDigitIn = 0; pos1sDigitIn <= posSty; pos1sDigitIn++)
	{
		stychartype = pArray->Item((size_t)pos1sDigitIn);
		if (stychartype == cIsForced || stychartype == cIsBL)
			break;
	}
	while (posSty >= pos1sDigitIn)
	{
		stychartype = pArray->Item((size_t)posSty);
		if (stychartype == cIsForced)
			sto = m_cZero + sto;
		else if (stychartype == cIsBL || stychartype == cIsChar)
			sto = psty->GetChar((size_t)posSty) + sto;
		else if (!signIn && (stychartype == cIsSignp || stychartype == cIsSignn))
			sto = m_cMinus + sto;
		else if (signIn && stychartype == cIsSignp)
			sto = m_cPlus + sto;
		posSty--;
	}

	//3. Rest before group - - - - -
	if (fg_sty > 0 && ig_sty <= (size_t)posSty) //still inside of group
		posSty = (int)ig_sty - 1; //go to group's beginning -1
	//we must skip all chars, except those at left (prefix)
	//  i.e. ab#+#cd### will skip all '#' and 'cd' but not 'ab' neither '+'
	//locate first from left no-char no-sign
	pos1sDigitIn = 0;
	while (pos1sDigitIn < posSty)
	{
		stychartype = pArray->Item((size_t)pos1sDigitIn);
		if (stychartype != cIsSignp && stychartype != cIsSignn && stychartype != cIsChar)
			break;
		pos1sDigitIn++;
	}
	//get the sign if it is between the skipped chars
	while (posSty > pos1sDigitIn)
	{
		stychartype = pArray->Item((size_t)posSty);
		if (!signIn && (stychartype == cIsSignp || stychartype == cIsSignn))
			sto = m_cMinus + sto;
		else if (signIn && stychartype == cIsSignp)
			sto = m_cPlus + sto;
		posSty--;
	}
	if (posSty > pos1sDigitIn && pos1sDigitIn >= 0)
		posSty = pos1sDigitIn;
	//get the chars from pos1sDigitIn to 0
	while (posSty >= 0)
	{
		stychartype = pArray->Item((size_t)posSty);
		if (!signIn && (stychartype == cIsSignp || stychartype == cIsSignn))
			sto = m_cMinus + sto;
		else if (signIn && stychartype == cIsSignp)
			sto = m_cPlus + sto;
		else if (stychartype == cIsChar)
			sto = psty->GetChar((size_t)posSty) + sto;
		posSty--;
	}


	//4. The decimal part - - - - - - - - - - - - - - - -

	//If no decimal separator, TRUNCATE the number and jump to 6.
	//If both srIn/style have decimal separator or style requires 'forced'
	// digits, use the decimal part
	if ((posStydec >= 0 && posdec != wxNOT_FOUND) || stData->reqDec)
	{   //let's put the decimal separator
		posSty = posStydec;
		while (posSty >= 0 && pArray->Item((size_t)posSty) == cIsDecSep)
			posSty--; //make sure we are at decsep's beginning
		posSty++;
		while (posSty < lenSty && pArray->Item((size_t)posSty) == cIsDecSep)
		{
			sto = sto + psty->GetChar((size_t)posSty);
			posSty++;
		}

		//pos1sDigitIn-1 will be the last to format
		pos1sDigitIn = (posexp != wxNOT_FOUND) ? posexp : lenStrIn;
		//is there any decimal digit to get formatted?
		posrIn = (posdec != wxNOT_FOUND) ? posdec + 1 : pos1sDigitIn;

		if (posSty < lenSty)
			stychartype = pArray->Item((size_t)posSty);
		else
			stychartype = 0;

		//5. After decimal separator and before exponential- - - - - - - -
		while (posSty < lenSty && stychartype != cIsExpU && stychartype != cIsExpL)
		{
			if (stychartype == cIsAllDig)// '*' means 'all decimal digits'
			{   //For big difference pos1sDigitIn-posrIn, this could be
				// optimized using wxString::Mid() instead of the while-loop
				while (posrIn < pos1sDigitIn)
				{
					sto = sto + srIn.GetChar((size_t)posrIn);
					posrIn++;
				}
			}
			else if (stychartype == cIsDigit) //'#' means 'get the digit, if there's one'
			{
				if (posrIn < pos1sDigitIn)
				{
					sto = sto + srIn.GetChar((size_t)posrIn);
					posrIn++;
				}
			}
			else if (stychartype == cIsForced || stychartype == cIsBL)
			{
				if (posrIn < pos1sDigitIn)
				{
					sto = sto + srIn.GetChar((size_t)posrIn);
					posrIn++;
				}
				else //no more digits in number before exponential
				{
					if (stychartype == cIsForced)
						sto = sto + m_cZero;
					else //is BL
						sto = sto + psty->GetChar((size_t)posSty);
				}
			}
			else if (!signIn && (stychartype == cIsSignp || stychartype == cIsSignn))
				sto = sto + m_cMinus;
			else if (signIn && stychartype == cIsSignp)
				sto = sto + m_cPlus;
			else if (stychartype == cIsChar)
			{   //Can this char be skipped? Only if the first we get is '#'
				bool skipIt = false;
				for (int i = posSty; i < lenSty; i++)
				{
					stychartype = pArray->Item((size_t)i);
					if (stychartype == cIsDigit)
					{
						skipIt = true; //yes, this char may be skipped
						break;
					}
					else if (stychartype == cIsForced || stychartype == cIsBL
						|| stychartype == cIsExpU || stychartype == cIsExpL)
						break;
				}
				if (!skipIt || posrIn < pos1sDigitIn)
					sto = sto + psty->GetChar((size_t)posSty);
			}

			posSty++;
			if (posSty < lenSty)
				stychartype = pArray->Item((size_t)posSty);
		}
		posStyPart = posSty;
	}

	//6. Exponential- - - - - - - - - - - - - - - - -
	//Use the exponential part if both number and style have an 'e'
	// or if exponential part at style has 'forced' digits
	if (posexp != wxNOT_FOUND && posStyexp == -1)
	{
		m_lasterror = _("Error: style has not exponential");
		return posexp;
	}

	if ((posexp != wxNOT_FOUND && posStyexp != -1) || stData->reqExp)
	{   //Write the proper 'e'
		stychartype = pArray->Item((size_t)posStyexp);
		if (stychartype == cIsExpU)
			sto = sto + m_cExpU;
		else
			sto = sto + m_cExpL;

		//Is there a sign for exponential in srIn?
		signEIs = false; //perhaps sign is not typed yet (i.e. '1.23E')
		//The exponential sign
		signExIn = true; //presumed to be '+' even if signEIs==false

		if (posexp != wxNOT_FOUND)
		{
			pos1sDigitIn = posexp + 1;
			if (posexp < lenStrIn - 1) //posexp is not the last char, so next may be sign
			{
				c = srIn.GetChar((size_t)pos1sDigitIn);
				if (c == m_cMinus || c == m_cPlus)
				{
					signEIs = true;
					pos1sDigitIn++; //advance to first exponential digit, if any
					if (c == m_cMinus) signExIn = false;
				}
			}
		}
		else
			pos1sDigitIn = lenStrIn; //beyond last char

		//Let's use 'pos1sReq' to store the first required digit in style
		for (pos1sReq = posStyexp + 1; pos1sReq < lenSty; pos1sReq++)
		{
			stychartype = pArray->Item((size_t)pos1sReq);
			if (stychartype == cIsForced || stychartype == cIsBL)
				break;
		}
		//Let's use 'pos1sDig' to store the first digit in style, required or not
		for (pos1sDig = posStyexp + 1; pos1sDig < lenSty; pos1sDig++)
		{
			stychartype = pArray->Item((size_t)pos1sDig);
			if (stychartype == cIsDigit || stychartype == cIsForced
				|| stychartype == cIsBL)
				break;
		}

		posrIn = lenStrIn - 1; //we'll read backwards until pos1sDigitIn
		posSty = lenSty - 1;
		posStyPart = lenSty;
		poser = posrIn;

		//backwards filling the digits
		while (posrIn >= pos1sDigitIn || posSty > posStyexp)
		{
			if (posrIn >= pos1sDigitIn)
				c = srIn.GetChar((size_t)posrIn);
			if (posSty <= posStyexp) //not enough style-size for the number
			{
				m_lasterror = _("Error: too many digits in exponential");
				return poser;
			}
			stychartype = pArray->Item((size_t)posSty);
			if (stychartype == cIsAllDig) // '*'
			{   //Get the remaining digits
				while (posrIn >= pos1sDigitIn)
				{
					c = srIn.GetChar((size_t)posrIn);
					stex = c + stex; //get the digit
					posrIn--;
				}
			}
			else if (stychartype == cIsDigit || stychartype == cIsForced
				|| stychartype == cIsBL)
			{
				if (posrIn >= pos1sDigitIn) //srIn has a new digit to write
					stex = c + stex; //get the digit
				else if (stychartype == cIsForced)
					stex = m_cZero + stex;
				else if (stychartype == cIsBL)
					stex = psty->GetChar((size_t)posSty) + stex;
				posrIn--;
			}
			//Only put the exponential sign if required or srIn has it
			else if (stychartype == cIsSignpE || (stychartype == cIsSignnE && signEIs))
			{
				if (signExIn)
					stex = m_cPlus + stex;
				else
					stex = m_cMinus + stex;
			}
			//the number's sign
			else if (!signIn && (stychartype == cIsSignp || stychartype == cIsSignn))
				stex = m_cMinus + stex;
			else if (signIn && stychartype == cIsSignp)
				stex = m_cPlus + stex;
			//style requires a char
			else if (stychartype == cIsChar && (posSty >= pos1sReq //pos1sReq is first required
				|| posrIn >= pos1sDigitIn || posSty < pos1sDig))
				stex = psty->GetChar((size_t)posSty) + stex;

			posSty--;
		}

		sto = sto + stex;
	}

	//7. Last part: style may require chars and numb's sign.
	//We know this because posStyPart < lenSty
	if (posStyPart < lenSty)
	{
		posSty = lenSty - 1;
		stychartype = pArray->Item((size_t)posSty);
		while (stychartype == cIsChar
			|| stychartype == cIsSignp || stychartype == cIsSignn)
			stychartype = pArray->Item((size_t) --posSty);
		posSty++;
		//now the rest of characters
		while (posSty < lenSty)
		{
			stychartype = pArray->Item((size_t)posSty);
			if (!signIn && (stychartype == cIsSignp || stychartype == cIsSignn))
				sto += m_cMinus;
			else if (signIn && stychartype == cIsSignp)
				sto += m_cPlus;
			else if (stychartype == cIsChar)
				sto += psty->GetChar((size_t)posSty);

			posSty++;
		}
	}

	srOut = sto;
	m_lasterror = wxEmptyString;
	return -1; //input is OK, format done
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/* UNFORMATTING
 This is the most difficult part, because we allow complex formats.
 A formatted number can have several different parts: prefix, integer part,
 decimal part, exponential part and suffix. We allow also characters between
 the digits, and, even worst, these characters can be digits.
 So, we can have something like "item 1: 1,234,567 dot 89 Exp+05 m3" meaning
 the unformatted number is 1234567.89E05

 To cope with all of it, we must know not only basic positions (exponential,
 decimal separator, suffix, etc.), but also if some of those digits are really
 chars (like in 'item 1' or 'm3'). To help us, we stored information about
 digits used when the style was set.

 For each part, we walk position by position simultaneously in number and style,
 checking if a character matches the type defined in style.

 We must also check if there are less/just/more digits as style requires.
*/
/////////////////////////////////////////////////////////////////////////////
//The unformat function. If beStc == wxUF_BESTRICT returns when first error
//is found, this is, srIn is not formatted with the sub-style
int wxFormatStringAsNumber::StrUnf(int usesty,
	const wxString& srIn,
	wxString& srOut,
	int beStc)
{
	wxString sto = wxEmptyString;
	wxString stex = wxEmptyString;
	wxString *psty = (wxString*)NULL;
	wxArrayInt *pArray = (wxArrayInt*)NULL;
	size_t ig_sty = 0;
	size_t fg_sty = 0;
	wxString *p_sdec = (wxString*)NULL;
	int prefixpos, suffixpos;

	//NOTE: we use negative values, so better 'int' instead of 'size_t'
	int posdec = 0, posStydec = 0, posexp, posStyexp = 0;
	int stychartype;
	bool signIn, signExIn;
	bool gotN, gotS, hasWholeDec, gotDigits, inReqZone;
	int posSty, posrIn, posLastDec, posgroup, poser, retPosErr; //positions
	int numDigSty = 0, numDigPfx = 0, numDigIn = 0; //number of digits
	int wtdwtc; //response
	int lenStrIn = (int)srIn.Len();
	int lenSty;
	wxUniChar cI, cS;
	wxArrayInt* arrMinDig = (wxArrayInt*)NULL;

	stFSNData *stData;
	//data and decimal/exponential/suffix positions in sub-style
	if (usesty == 1)
		stData = &m_Sstypos;
	else if (usesty == 2)
		stData = &m_Sstyneg;
	else
		stData = &m_Sstyzer;
	//Let's point to the used sub-style
	psty = &(stData->sty);
	pArray = &(stData->Asty);
	ig_sty = stData->ig_sty;
	fg_sty = stData->fg_sty;
	posStydec = (int)stData->idecse;
	posStyexp = (int)stData->iexpse;
	prefixpos = stData->iprefix;
	suffixpos = (int)stData->isuffix;
	p_sdec = &(stData->decsep);
	arrMinDig = &(stData->AminD); //See FillArrayMinDig

	lenSty = (int)pArray->GetCount();

	if (posStydec == 0 && pArray->Item(0) != cIsDecSep)
		posStydec = -1; //there is not decimal separator
	if (stData->typExp == 0)
		posStyexp = -1; //there is not exponential


	//0. Some basic positions - - - - - - - - - - - - - - - - -*-*-*-*-*-*-*-*-
	// If we have not decimal separator and/or exponential in sub-style
	//  we don't search for them. If they exist in srIn, we'll push an error.
	// Other case, we must know where decimal separator and exponential are.

	// To find decimal separator (in strict & !strict modes) we just
	//  get the first occurrence (from left to right) in srIn.
	// If !strict mode we'll search the first occurrence of any of the
	//  decimal separator chars.

	m_lasterror = _("Error in input string"); //prepare message (generic)

	//0.1 posdec: decimal-separator's position in srIn
	hasWholeDec = false;
	if (p_sdec->Len() > 0) //have we decimal separator to search for?
	{   //first try to find the whole decimal separator
		posdec = srIn.Find(*p_sdec);
		hasWholeDec = posdec != wxNOT_FOUND ? true : false;
		if (!hasWholeDec && beStc == wxUF_NOSTRICT)
		{   //tolerant: search two consecutive chars of *p_sdec's char
			for (posSty = 0; posSty < (int)p_sdec->Len() - 1; posSty++)
			{
				posdec = srIn.Find(p_sdec->Mid((size_t)posSty, 2));
				if (posdec != wxNOT_FOUND)
					break;
			}
		}
	}
	else
		posdec = wxNOT_FOUND;

	//Go to p_sdec's end for searching exponential (decimal-separator may be
	// several chars). If p_sdec contains a 'e' this may trick to no strict mode.
	if (posdec != wxNOT_FOUND && hasWholeDec)
		posdec += (int)p_sdec->Len() - 1;

	//0.2 posexp: exponential position in srIn
	// Because srIn may have several 'e', which one is exponential?
	// I have not a better idea than getting the first 'e' at left of the
	//  first digit, searching finish to beginning (right to left)
	// If in not strict mode, just get the first 'e' from the right
	// I know this can't cope with all cases, but 90% (100% of 'normal')
	//TODO: an infallible way
	posexp = wxNOT_FOUND;
	if (posStyexp != -1)
	{   //Search only if style has exponential. Otherwise an error will arise.
		posrIn = lenStrIn - 1;
		gotN = false;
		while (posrIn > posdec) //right to left
		{
			cI = srIn.GetChar((size_t)posrIn);
			if ((cI == m_cExpU || cI == m_cExpL)
				&& (beStc == wxUF_NOSTRICT || (beStc == wxUF_BESTRICT && gotN)))
			{
				posexp = posrIn;
				break;
			}
			if (!gotN && fs_IsDigit(cI))
				gotN = true;
			posrIn--;
		}
	}

	//If the style requires exponential part (because it has 'forced' 0b
	// digits, or uses scientific/engineering notation) srIn must also have
	// exponential part, otherwise is an error.
	if (posexp == wxNOT_FOUND && (stData->reqExp
		|| stData->typExp == eExpSci || stData->typExp == eExpEng))
	{
		m_lasterror = _("Exponential part required");
		return (lenStrIn >= 0 ? lenStrIn : 0); //error
	}

	signIn = true; //assume number to be positive
	inReqZone = true; //a char in style must be used


	//0.3 Before unformatting we'll strip out suffix. - - - - - -X-X-X-X-X-X-X-X
	/*With the style "[#].'.'#e##xx" the number "12" becomes formatted as "12xx"
	  and "12.3" becomes "12.3xx" and "12e3" into "12e3xx". When unformatting
	  "12.3xx" we may wrongly believe "xx" is part of the decimal part.
	  So, we need to analyze the suffix before any part.
	  This is easy in "strict" mode. In "tolerant" mode we can dismiss the
	  chars, but not the sign.
	*/
	//suffixpos == lenSty means there is no suffix
	//wxUF_BESTRICT analyse suffix exactly char by char.
	//wxUF_NOSTRICT will stop at first digit in srIn
	//Lengths (lenStrIn, lenSty) will be adjusted to rest-to-test lengths.

	if (suffixpos < lenSty)
	{
		posSty = lenSty - 1;
		posrIn = lenStrIn - 1;
		//At left of this pos, we are not in srIn suffix
		if (posexp == wxNOT_FOUND)
		{
			if (posdec == wxNOT_FOUND)
				poser = -1;
			else
				poser = posdec;
		}
		else
			poser = posexp;
		//Analyze
		while (posSty >= suffixpos)
		{
			gotN = gotS = false;
			numDigSty = numDigIn = 0;
			cS = psty->GetChar((size_t)posSty);
			stychartype = pArray->Item((size_t)posSty);
			if (posrIn > poser)
				cI = srIn.GetChar((size_t)posrIn);
			else
				cI = 0; //no more from srIn

			if (cI == 0 || fs_IsDigit(cI))
			{
				if (beStc == wxUF_BESTRICT)
				{
					if (cI != 0 && cI == cS)
						gotN = gotS = true; //this digit is a char in suffix
					else
					{
						m_lasterror = _("Suffix required");
						return (posrIn >= 0 ? posrIn + 1 : 0); //error
					}
				}
				else //test if a sign is still required
				{
					for (poser = posSty; poser >= suffixpos; poser--)
					{
						if (pArray->Item((size_t)poser) == cIsSignp)
						{
							m_lasterror = _("Sign required");
							return (posrIn >= 0 ? posrIn + 1 : 0); //error
						}
					}
					break; //end suffix check
				}
			}
			else //sign can be treated here
			{
				wtdwtc = WhatToDoWithThisChar(numDigSty, inReqZone, &numDigIn,
					cI, cS, stychartype, beStc,
					&signIn, &signExIn, &gotN, &gotS);
				if (wtdwtc == 0)
					return posrIn; //error
			}
			if (gotN) posrIn--;
			if (gotS) posSty--;
		}
		lenSty = suffixpos;
		lenStrIn = posrIn + 1;
		if (lenStrIn <= 0 && lenSty <= 0) //everything is suffix, number is zero
		{
			srOut = m_cZero;
			m_lasterror = wxEmptyString;
			return -1;
		}
	}

	//Position in style to begin unformatting (integer part)
	if (posStydec == -1)
		posSty = (posStyexp == -1) ? (lenSty - 1) : (posStyexp - 1);
	else
		posSty = posStydec - 1;

	//We need to know how many digits-chars has the prefix
	if (prefixpos >= 0)
		numDigPfx = abs(arrMinDig->Item((size_t)prefixpos));
	else
		numDigPfx = 0;

	//back to decimal beginning
	if (posdec != wxNOT_FOUND && hasWholeDec)
		posdec -= (int)p_sdec->Len() - 1;

	//1. All given digits before decimal separator - - - - - - -X-X-X-X-X-X-X-X-
	//Position in srIn to begin unformatting
	if (posdec == wxNOT_FOUND)
	{
		if (posexp != wxNOT_FOUND)
			posrIn = posexp - 1;
		else
			posrIn = lenStrIn - 1;
	}
	else
		posrIn = posdec - 1;

	//Number of [possible] digits in srIn before decimal separator
	for (poser = 0; poser <= posrIn; poser++)
	{
		cI = srIn.GetChar((size_t)poser);
		if (fs_IsDigit(cI))
			numDigIn++;
	}

	poser = posrIn; //position of possible error
	if (poser < 0)
		poser = (int)srIn.Len();

	//posrIn < 0 may be (i.e. '.12'), let's check if it is allowed
	if (posSty < (int)arrMinDig->GetCount()) //no minimum digits, may be third style
		numDigSty = arrMinDig->Item((size_t)posSty);
	else
		numDigSty = 0;
	if (posrIn < 0 && abs(numDigSty) > 0)
	{
		m_lasterror = _("Digits needed");
		return poser; //error (i.e user typed ".1" and style is "0.'.'#"
	}
	//Another check: has srIn enough digits?
	if (numDigIn < abs(numDigSty)
		&& (beStc == wxUF_BESTRICT || (numDigIn < abs(numDigSty) - numDigPfx)))
	{
		m_lasterror = _("More digits required");
		return poser; //error
	}

	//The method I use is to walk position by position in srIn and compare the
	// char (cI) at that position to what is supposed to be according with style
	// position (cS).
	//The comparison is made at WhatToDoWithThisChar(). This function also
	// indicates (gotN, gotS) if positions should be moved.

	retPosErr = -1;
	gotDigits = false;

	while (posrIn >= 0 || posSty >= 0)
	{   //The chars to compare
		if (posrIn >= 0)
			cI = srIn.GetChar((size_t)posrIn);
		if (posSty >= 0)
		{
			cS = psty->GetChar((size_t)posSty);
			stychartype = pArray->Item((size_t)posSty);
		}
		else
			stychartype = 0;

		if (posSty >= 0 && posSty < (int)arrMinDig->GetCount())
			numDigSty = arrMinDig->Item((size_t)posSty);
		else
			numDigSty = 0;

		//Special cases first
		if (posSty < 0 && posrIn >= 0) //not enough style-size for the number
		{
			if (beStc == wxUF_NOSTRICT && numDigIn == 0) //rest in srIn are chars
				break; //tolerant, so skip remaining chars, unused sign included
			m_lasterror = _("Error: too many digits or chars");
			return posrIn;
		}

		if (posSty >= 0 && posrIn < 0) //no more to unformat in this part
		{   //Test the remaining left at style to tell if we can leave this part
			while (posSty >= 0)
			{
				stychartype = pArray->Item((size_t)posSty);
				if (stychartype == cIsForced || stychartype == cIsBL)
				{
					m_lasterror = _("More digits or chars required");
					return 0;
				}
				else if (stychartype == cIsSignp)
				{
					m_lasterror = _("Sign required");
					return 0;
				}
				else if (beStc == wxUF_BESTRICT && stychartype == cIsChar
					&& (posSty <= prefixpos || numDigSty != 0))
				{
					m_lasterror = _("More chars required");
					return 0;
				}
				posSty--;
			}
			//no more digits/sign required and "no strict", we can leave this part
			break;
		}

		//The function analyse cases for cI/cS combinations:
		gotN = gotS = false;
		inReqZone = posSty > prefixpos ? false : true;
		if (abs(numDigSty) > numDigPfx)
			inReqZone = true; //because style has '0' 'b' left of this pos.

		wtdwtc = WhatToDoWithThisChar(numDigSty, inReqZone, &numDigIn,
			cI, cS, stychartype, beStc,
			&signIn, &signExIn, &gotN, &gotS);

		if (wtdwtc == 0)
			return posrIn; //error
		if (wtdwtc == 1)
		{
			sto = cI + sto; //get the digit
			numDigIn--;
			gotDigits = true;
		}

		//now, move positions
		if (gotN) //move number position
			posrIn--;

		if (gotS && posSty >= 0) //move style position
		{   // Are we inside a group?
			//remember fg_sty is position + 1 of right-most char inside group
			if (fg_sty > 0) //there's group
			{
				if (ig_sty == (size_t)posSty) //reached beginning (at left) of group
					posgroup = 0;
				else if (ig_sty < (size_t)posSty && fg_sty >(size_t) posSty)
					posgroup = 1; //still inside of group
				else
					posgroup = -1; //outside of group
			}
			else
				posgroup = -1; //means: no group

			if (posgroup == 0 //reached beginning of group
				&& numDigIn > 0) //and more digits may be unformatted
			{   //Perhaps these digits are needed for prefix
				if (numDigIn > numDigPfx)
					posSty = (int)fg_sty - 1; //go to group's finish (at right) for a new loop
				else
					posSty--;
			}
			else if (posgroup == 1 && numDigIn <= numDigPfx)
			{   //must we leave the group?
				for (posgroup = posSty - 1; posgroup >= (int)ig_sty; posgroup--)
				{
					stychartype = pArray->Item((size_t)posgroup);
					if (stychartype == cIsForced || stychartype == cIsBL)
						break;
				}
				if (posgroup < (int)ig_sty) //we can leave the group
					posSty = (int)ig_sty - 1;
				else
					posSty--; //maybe an error will raise soon
			}
			else
				posSty--;
		}
	}
	//Store the number of integer digits. We will use it for checking if
	// scientific/engineering notation
	size_t nIntDig = sto.Len();

	//2. Decimal digits before exponential - - - - - - - - - - -X-X-X-X-X-X-X-X-
	//This part is analysed when style has decimal separator. If it also has
	// 'forced' (0 b) digits, srIn must have decimal part, otherwise is an error.
	if (posdec == wxNOT_FOUND && stData->reqDec)
	{
		m_lasterror = _("Decimal part required");
		return poser; //error
	}
	if (posdec != wxNOT_FOUND && posStydec != -1)
	{   //Yes, we have it, write it then
		sto += m_cDot;

		if (hasWholeDec)
			posrIn = posdec + (int)p_sdec->Len();
		else
			posrIn = posdec + 1; //the rest of *p_sdec's chars will be skipped

		//posLastDec-1 will be the last position to unformat in this decimal part
		if (posexp != wxNOT_FOUND)
			posLastDec = posexp;
		else
			posLastDec = lenStrIn; //no exponential

		//number of [possible] digits in srIn after decimal separator before exponential
		numDigIn = 0;
		for (poser = posrIn; poser < posLastDec; poser++)
		{
			cI = srIn.GetChar((size_t)poser);
			if (fs_IsDigit(cI))
				numDigIn++;
		}

		//store in 'posgroup' the min number of digits required by style's dec part
		posSty = (posStyexp != -1) ? (posStyexp - 1) : (lenSty - 1);
		if (posSty < (int)arrMinDig->GetCount())
			posgroup = abs(arrMinDig->Item((size_t)posSty));
		else
			posgroup = 0;

		posSty = posStydec + (int)p_sdec->Len();

		//Find the zone where a char can't be skipped
		poser = lenSty - 1; //we did lenSty = suffixpos
		if (posStyexp > 0 && poser > posStyexp)
			poser = posStyexp - 1;

		for (prefixpos = poser; prefixpos > posSty; prefixpos--)
		{
			stychartype = pArray->Item((size_t)prefixpos);
			if (stychartype == cIsForced || stychartype == cIsBL
				|| stychartype == cIsDigit || stychartype == cIsAllDig)
			{
				prefixpos++;
				break;
			}
		}
		//Number of digits-chars in this zone
		//arrMinDig was stored for backwards-mode use, not left to right
		if (prefixpos <= poser && prefixpos < (int)arrMinDig->GetCount())
		{   //make numDigPfx >= 0
			numDigPfx = arrMinDig->Item((size_t)prefixpos);
			if (numDigPfx < 0)
				numDigPfx = posgroup + numDigPfx + 1;
			else
				numDigPfx = posgroup - numDigPfx;
		}
		else
			numDigPfx = 0;

		//Extract the digits from left to right
		if (posrIn < posLastDec)
			cI = srIn.GetChar((size_t)posrIn);
		if (posSty < lenSty)
		{
			cS = psty->GetChar((size_t)posSty);
			stychartype = pArray->Item((size_t)posSty);
		}
		else
			stychartype = 0;

		while ((posrIn < posLastDec) || (posSty < lenSty &&
			stychartype != cIsExpU && stychartype != cIsExpL))
		{
			gotN = gotS = false;
			//arrMinDig was stored for backwards-mode use, not left to right
			if (posSty < (int)arrMinDig->GetCount())
				numDigSty = arrMinDig->Item((size_t)posSty);
			else
				numDigSty = 0;
			if (numDigSty < 0)
				numDigSty = -posgroup - numDigSty - 1;
			else
				numDigSty = posgroup - numDigSty;

			//Special case: test if we can leave or we have an error
			if (posrIn >= posLastDec) //no more to unformat
			{   //Forced digits not used is always an error
				if (abs(numDigSty) > numDigPfx)
					return posrIn;
				//Decimal part suffix is required in 'strict' mode
				if (beStc == wxUF_BESTRICT && posSty >= prefixpos)
					return posrIn;
				if (stychartype == cIsDigit || stychartype == cIsAllDig
					|| stychartype == cIsSignn)
				{
					posSty++;
					if (posSty < lenSty)
					{
						cS = psty->GetChar((size_t)posSty);
						stychartype = pArray->Item((size_t)posSty);
					}
					continue;
				}
			}
			//Another special case
			if (posSty >= lenSty
				|| stychartype == cIsExpU || stychartype == cIsExpL)
			{   //If we have more digits to unformat, this is an error
				if (numDigIn > 0)
					return posrIn;
				//Rest of chars (sign included) may be skipped
				if (beStc == wxUF_BESTRICT)
					return posrIn;
				else
					break;
			}

			//cases for cI/cS:
			inReqZone = posSty < prefixpos ? false : true;
			if (abs(numDigSty) > numDigPfx)
				inReqZone = true; //because style has a '0' 'b' after this pos.
			wtdwtc = WhatToDoWithThisChar(numDigSty, inReqZone, &numDigIn,
				cI, cS, stychartype, beStc,
				&signIn, &signExIn, &gotN, &gotS);
			if (wtdwtc == 0)
				return posrIn; //error
			if (wtdwtc == 1)
			{
				sto = sto + cI; //get the digit
				numDigIn--;
				gotDigits = true;
			}
			//move positions
			if (gotN)
			{
				posrIn++;
				if (posrIn < posLastDec)
					cI = srIn.GetChar((size_t)posrIn);
			}
			if (gotS)
			{
				posSty++;
				if (posSty < lenSty)
				{
					cS = psty->GetChar((size_t)posSty);
					stychartype = pArray->Item((size_t)posSty);
				}
			}
		} //while-end
	}

	//Has srIn digits, apart from exponential or suffix zone?
	if (!gotDigits)
	{
		retPosErr = posexp < 0 ? lenStrIn - 1 : posexp;
		//retPosErr > =0 means that there's an error, but perhaps we can continue
		if (beStc == wxUF_BESTRICT)
		{
			m_lasterror = _("Error: lack of digits");
			return retPosErr;
		}
	}

	/*For 'tolerant' mode (useful for validating at OnChar) we continue
	* retrieving the significant parts. So, even returning poserror >= 0 (which
	* means there's an error), we will also fill srOut as much as possible.
	* SrOut can become something incomplete like "-1.2e" or ".e+" or "-." etc.
	*/

	//3. exponential - same method as for integer part - - - - -X-X-X-X-X-X-X-X-
	//This part is analysed when style has exponential.
	gotDigits = false;

	if (posexp != wxNOT_FOUND && posStyexp != -1)
	{   //First check the integer part in case of sci/eng notations
		if (beStc == wxUF_BESTRICT &&
			(stData->typExp == eExpSci || stData->typExp == eExpEng))
		{   //check number of integer digits
			if (nIntDig > 0 && nIntDig < 4)
			{
				long valI = 0;
				sto.ToCLong(&valI);
				if (valI == 0 || (valI > 9 && stData->typExp == eExpSci))
				{
					m_lasterror = _("Error: wrong integer value for this notation");
					return 0;
				}
			}
			else
			{
				m_lasterror = _("Error: wrong integer digits for this notation");
				return 0;
			}
		}

		stex = wxEmptyString;
		posrIn = lenStrIn - 1;
		numDigIn = 0;
		signExIn = true; //assume exponential to be positive

		//number of [possible] digits in srIn's exponential part
		for (poser = posexp + 1; poser <= posrIn; poser++)
		{
			cI = srIn.GetChar((size_t)poser);
			if (fs_IsDigit(cI))
				numDigIn++;
		}

		//The "prefix" zone (required chars close to 'e')
		for (prefixpos = posStyexp + 1; prefixpos < lenSty; prefixpos++)
		{
			stychartype = pArray->Item((size_t)prefixpos);
			if (stychartype == cIsDigit || stychartype == cIsAllDig
				|| stychartype == cIsForced || stychartype == cIsBL)
			{
				prefixpos--;
				break;
			}
		}
		//We need to know how many digits-chars has the "prefix" (required chars)
		if (prefixpos >= posStyexp)
			numDigPfx = abs(arrMinDig->Item((size_t)prefixpos));
		else
			numDigPfx = 0;

		posSty = lenSty - 1;

		//backwards extracting the digits
		while (posrIn > posexp || posSty > posStyexp)
		{   //The chars to compare
			if (posrIn >= 0)
				cI = srIn.GetChar((size_t)posrIn);
			if (posSty >= 0)
			{
				cS = psty->GetChar((size_t)posSty);
				stychartype = pArray->Item((size_t)posSty);
			}
			else
				stychartype = 0;

			if (posSty >= 0 && posSty < (int)arrMinDig->GetCount())
				numDigSty = arrMinDig->Item((size_t)posSty);
			else
				numDigSty = 0;

			//special cases first
			if (posSty <= posStyexp && posrIn > posexp) //not enough style-size for the number
			{
				if (beStc == wxUF_NOSTRICT && numDigIn == 0) //rest in srIn are chars
					break; //skip even an unused exponential sign
				m_lasterror = _("Error: too many digits or chars");
				return posrIn;
			}

			if (posSty > posStyexp && posrIn <= posexp) //no more to unformat in this part
			{   //Test the remaining left at style to tell if we can leave this part
				while (posSty > posStyexp)
				{
					stychartype = pArray->Item((size_t)posSty);
					if (stychartype == cIsForced || stychartype == cIsBL)
					{
						m_lasterror = _("More digits or chars required in exponential");
						return posrIn;
					}
					else if (stychartype == cIsSignpE || stychartype == cIsSignp)
					{
						m_lasterror = _("Sign required");
						return posrIn;
					}
					else if (beStc == wxUF_BESTRICT && stychartype == cIsChar
						&& (posSty <= prefixpos || numDigSty != 0))
					{
						m_lasterror = _("More chars required");
						return posrIn;
					}
					posSty--;
				}
				//no more digits/sign required and "no strict", we can leave this part
				break;
			}

			//The function analyse cases for cI/cS combinations:
			gotN = gotS = false;
			inReqZone = posSty > prefixpos ? false : true;
			if (abs(numDigSty) > numDigPfx)
				inReqZone = true; //because style has '0' 'b' left of this pos.

			wtdwtc = WhatToDoWithThisChar(numDigSty, inReqZone, &numDigIn,
				cI, cS, stychartype, beStc,
				&signIn, &signExIn, &gotN, &gotS);

			if (wtdwtc == 0)
				return posrIn; //error
			if (wtdwtc == 1)
			{
				stex = cI + stex; //get the digit
				numDigIn--;
				gotDigits = true;
			}

			//now move positions
			if (gotN) //move number position
				posrIn--;

			if (gotS) //move style position
				posSty--;
		}

		if (!gotDigits)
		{
			if (retPosErr < 0 || retPosErr > posexp + 1)
				retPosErr = posexp + 1; //the left-most error's position
			if (beStc == wxUF_BESTRICT)
			{
				m_lasterror = _("Error: lack of exponential digits");
				return posexp + 1;
			}
		}
		else if (beStc == wxUF_BESTRICT && stData->typExp == eExpEng)
		{   //For engineer notation check if exponent is multiple of 3
			long valExp = 0;
			stex.ToCLong(&valExp);
			if (valExp % 3 != 0)
			{
				m_lasterror = _("Error: exponent for engineering notation");
				return posexp + 1;
			}
		}

		//Add the exponential part of the string
		sto += m_cExpU;
		if (!signExIn)
			sto += m_cMinus;
		sto += stex;
	}

	if (!signIn || usesty == 2)
		sto = m_cMinus + sto;

	srOut = sto;
	//For 'tolerant' mode we return an error and  also fill srOut
	if (beStc == wxUF_NOSTRICT && retPosErr >= 0)
		return retPosErr;

	//No errors
	m_lasterror = wxEmptyString;
	return -1;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/*
  This function fills an array, used when Unformatting, whose elements means:
	"minimum number of digits to get to the left if this position is used'.
  Explanation: suppose style is 'xx1x[203##]4##' where 1234 are chars.
   Formatting '5' becomes into 'xx1x0345'.
   Unformatting position by position, right to left, '5' corresponds to the
   right-most '#' but '4' doesn't correspond to next '#', because it was needed
   to form the formatted string. We need to skip this '#' and avoid the '4'
  We deal with this knowing how many _minimum_ digits were written when formatting.
  E.g. the '#' (7th position) has at least 3 digits (1 0 3) needed at its left.
  So, after parsing the '5',  we know that the remaining xx1x034 has four digits
	and arrMinDig[10] is also four, which shows that all those four digits were
	formatted without that '#' at ninth position.

  A second information we store in this array is if digit-char is treated as a
  'minimum to get'. If it is true, we store a negative value, else a positive one.

  So:   char  position    arrMinDig->Item(position)
		  x       0              0
		  x       1              0
		  1       2             -1      It is prefix, so required.
		  x       3              1
		  2       4              1      Positive because perhaps it is not used.
		  0       5             -2      A '0' is always used.
		  3       6             -3      Must be used to reach to '0' at left.
		  #       7              3
		  #       8              3
		  4       9             -4      Must be used to reach to '0' at left.
		  #      10              4
		  #      11              4

  pArray will have suffixpos (<= lenSty) elements

  The function also sets prefix and suffix positions
*/

void wxFormatStringAsNumber::FillArrayMinDig(wxArrayInt *arrMinDig,
	wxArrayInt *pArray, wxString *psty, size_t ig_sty, size_t fg_sty,
	int *prefixpos, size_t *suffixpos)
{
	size_t posSty = 0;
	int stychartype;
	size_t lenSty;
	wxUniChar cS;
	bool forcedYet = false, digYet = false;
	bool insidegroup = false, decpart = false;
	int numToAdd = 0;

	arrMinDig->Clear();
	lenSty = pArray->GetCount();

	//Is there a prefix? (It can't finish inside a group).
	for (posSty = 0; posSty < lenSty; posSty++)
	{
		stychartype = pArray->Item(posSty);
		if (stychartype == cIsDigit || stychartype == cIsBL
			|| stychartype == cIsForced || stychartype == cIsDecSep
			|| stychartype == cIsExpL || stychartype == cIsExpU
			|| (fg_sty > 0 && posSty >= ig_sty))
			break; //previous position was last one of prefix
	}
	*prefixpos = posSty < lenSty ? (int)posSty - 1 : -1;

	//Is there a suffix?
	*suffixpos = lenSty;
	for (posSty = lenSty; posSty > 0; posSty--)
	{
		stychartype = pArray->Item(posSty - 1);
		if (stychartype == cIsSignp || stychartype == cIsSignn || stychartype == cIsChar)
			*suffixpos = posSty - 1;
		else
			break;
	}

	//analyse
	for (posSty = 0; posSty < *suffixpos; posSty++)
	{
		cS = psty->GetChar(posSty);
		stychartype = pArray->Item(posSty);
		//remember fg_sty is position + 1 of right-most char inside group
		if (fg_sty > 0 && posSty >= ig_sty && posSty < fg_sty) //inside group
			insidegroup = true;
		else
			insidegroup = false;

		if (stychartype == cIsExpU || stychartype == cIsExpL || stychartype == cIsDecSep)
		{
			numToAdd = 0;
			digYet = forcedYet = false;
			decpart = (stychartype == cIsDecSep) ? true : false;
		}
		else if (stychartype == cIsForced
			|| (fs_IsDigit(cS)
				&& (stychartype == cIsBL || !digYet || forcedYet || decpart
					|| posSty >= *suffixpos)))
		{
			if (posSty > 0)
				numToAdd = -(abs(arrMinDig->Item(posSty - 1)) + 1);
			else
				numToAdd = -1;
			if (stychartype == cIsChar && !forcedYet && insidegroup)
				numToAdd = -(numToAdd + 1);
		}
		else
		{
			if (posSty > 0)
				numToAdd = abs(arrMinDig->Item(posSty - 1));
			else
				numToAdd = 0;
		}
		if (stychartype == cIsForced || stychartype == cIsBL
			|| stychartype == cIsDigit || stychartype == cIsAllDig)
		{
			digYet = true;
			if (stychartype == cIsForced || stychartype == cIsBL)
				forcedYet = true;
		}

		arrMinDig->Add(numToAdd);
	}
	arrMinDig->Shrink();
	return;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
//A helper for Unformatting.
//Returns:
// 0  = Error. More digits are required, or for some errors and wxUF_BESTRICT
// 1  = Get the digit and [possibly] move pointers
// -1 = Just move pointers
//We also indicate here (gotS, gotN) if pointers to style & data should be updated
int wxFormatStringAsNumber::WhatToDoWithThisChar(
	int numDigSty, bool inRequiredZone, int* numDigIn,
	const wxUniChar& cI, const wxUniChar& cS, int stychartype, int beStc,
	bool *signIn, bool *signExIn, bool *gotN, bool *gotS)
{
	int res = 0;

	//analyse cases for cS (expected) vs cI (at input) combination:
	switch (stychartype)
	{
	case cIsForced:
	case cIsBL:
		if (fs_IsDigit(cI))
		{
			*gotN = true; //means: 'do posrIn++ or posrIn--'
			*gotS = true; //means: 'do posSty++ or posSty--'
			res = 1; //get this digit
		}
		else if (cS == cI) //for cIsBL when there were no more digits to format
		{
			*gotS = *gotN = true;
			res = -1; //just move pointers and try again
		}
		else if (beStc == wxUF_NOSTRICT && cI != m_cMinus && cI != m_cPlus)
		{
			*gotN = true; //presume cI to be a typo
			res = -1;
		}
		else
			res = 0; //Error
		break;

	case cIsDigit: //cS = '#'
	case cIsAllDig: //cS = '*'
		if (*numDigIn <= abs(numDigSty))
		{   //Remaining digits were formatted without this '#/*'. So, skip it.
			*gotS = true;
			res = -1;
		}
		else if (fs_IsDigit(cI))
		{
			if (stychartype == cIsDigit)
			{
				*gotN = *gotS = true;
				res = 1; //get this digit
			}
			else //cS='*'
			{
				if (*numDigIn - abs(numDigSty) == 1)
				{   //rest of digits are required, or this the last digit
					*gotN = *gotS = true; //so advance both style and input
					res = 1; //get this digit
				}
				else //Typical case: move pointer in srIn, but not in style
				{
					*gotN = true;
					res = 1; //get this digit
				}
			}
		}
		//If we consider '-' or '+' as a typo, we may lose the sign
		else if (beStc == wxUF_NOSTRICT && cI != m_cMinus && cI != m_cPlus)
		{
			*gotN = true; //presume cI to be a typo
			res = -1;
		}
		else
			res = 0; //Error
		break;

	case cIsChar:
		res = -1; //just move pointers
		if (*numDigIn <= abs(numDigSty) && !inRequiredZone)
		{   //Remaining digits were formatted skipping this style's char.
			*gotS = true;
		}
		else if (cI == cS)
		{   //a well-positioned char
			*gotN = *gotS = true;
			if (fs_IsDigit(cI))
				(*numDigIn)--; //this digit is really a char
		}
		else if (beStc == wxUF_BESTRICT)
			res = 0; //error
		else
		{
			*gotS = true;
			if (!fs_IsDigit(cI) && cI != m_cMinus && cI != m_cPlus)
				*gotN = true; //presume cI to be a typo
		}
		break;

	case cIsSignp:  //'+' forced sign
	case cIsSignpE: //'+' for exponential part
		res = -1;
		if (cI == m_cMinus)
		{
			if (stychartype == cIsSignp)
				*signIn = false;
			else
				*signExIn = false;
			*gotS = *gotN = true;
		}
		else if (cI == m_cPlus)
			*gotS = *gotN = true; //we've assumed yet positive values. Nothing to do
		else if (beStc == wxUF_NOSTRICT && !fs_IsDigit(cI))
			*gotN = true; //presume cI to be a typo
		else
			res = 0; //error
		break;

	case cIsSignn:  //'-' only used for negative numbers
	case cIsSignnE: //'-' for exponential part
		res = -1;
		if (cI == m_cMinus)
		{
			if (stychartype == cIsSignn)
				*signIn = false;
			else
				*signExIn = false;
			*gotS = *gotN = true;
		}
		else if (fs_IsDigit(cI))
			*gotS = true; //this '-' is not used
		else if (beStc == wxUF_NOSTRICT)
			*gotN = true; //presume cI to be a typo
		else
			*gotS = true; //is strict mode we suppose this '-' is not used
		break;

	case cIsDecSep:
		//For 'tolerant' mode we may be using this case-block
		res = -1;
		*gotS = true; //skip this char
		if (beStc == wxUF_NOSTRICT && cI == cS)
			*gotN = true; //skip this char inside decimal separator
		break;

	default:
		res = 0; //Something must be wrong if we arrive here.
	}

	//Prevent an infinite loop, which means we have a bug
	if (!*gotS && !*gotN && res != 0)
	{
		m_lasterror = _("Code error at unformatting. Contact developer");
		wxFAIL_MSG(m_lasterror);
		res = 0;
	}

	return res;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
//The trim zeros function
//Notes:
// - strNum is unformatted, near a C-locale representation.
// - We want at least one '0' before decimal.
// - Trim zeros on integer and decimal parts, not exponential.
// - If integer and decimal parts are zero (i.e. -0.00E+25) we give back "0"
//   no matter sign or exponential.
//
void wxFormatStringAsNumber::TrimZeros(wxString& strNum, int atside)
{
	size_t lenN = strNum.Len();
	if (lenN == 0 || (atside & wxTZ_BOTH) == 0)
		return;

	wxString stmp = wxEmptyString;
	wxUniChar cI;
	size_t posNi, posNd, posNf, posNe, j;

	//Initialize the positions we need
	posNi = 0; //record here the first non-zero position from left (sign skipped)
	//dot position, first non-zero from right and exponential
	posNd = posNf = posNe = lenN;

	//At left. Only search, although we may get the sign.
	cI = strNum.GetChar(posNi);
	if (cI == m_cPlus || cI == m_cMinus)
	{
		stmp = cI; //get the sign
		posNi++;
	}
	while (posNi < lenN)
	{
		cI = strNum.GetChar(posNi);
		if (cI != m_cZero)
			break;
		else
			posNi++; //skip this '0'
	}
	//we want at least one '0' before decimal
	if (cI == m_cDot) //cI is here the first non-0 character
	{
		posNd = posNf = posNi;
		stmp.Append(m_cZero);
	}

	//Search from the right. Stop when cI == '.'
	j = lenN - 1;
	while (j > posNi)
	{
		cI = strNum.GetChar(j);
		if (cI == m_cExpU || cI == m_cExpL) //'E' 'e'
		{
			posNe = j;
			posNf = posNd; //reset
		}
		else if (cI == m_cDot) //'.'
		{
			posNd = j;
			if (posNf == lenN)
				posNf = j;
			break;
		}
		else if (posNf == posNd //dot was found before or is still posNf == lenN
			&& cI != m_cZero && fs_IsDigit(cI))
			posNf = j; //record position, but continue searching the dot
		j--;
	}

	//Let's see if number is really zero
	if (posNi == lenN || posNi == posNe
		|| (posNd < lenN && posNi == posNf))
	{
		strNum = m_cZero; //the string becomes "0"
		return;
	}

	if (!(atside & wxTZ_LEFT))
		posNi = 0;

	if (!(atside & wxTZ_RIGHT) || (posNe - posNf < 2))
	{
		posNf = lenN - 1;
		posNe = lenN;
	}

	//Should we modify the string?
	if ( //left part
		((posNi == 0 && (!(atside & wxTZ_LEFT) || strNum.GetChar(0) != m_cDot))
			|| (posNi == 1 && stmp.Len() > 0 && stmp.Right(1) != m_cZero))
		//right part
		&& (posNe - posNf < 2))
	{
		return; //changing strNum is not needed.
	}

	//Get the chars
	if (posNi > 0 || (atside & wxTZ_LEFT))
		stmp.Append(strNum.Mid(posNi, posNf - posNi + 1));
	else
		stmp = strNum.Mid(0, posNf + 1);

	if (posNe - posNf > 1) //add exponential part
		stmp.Append(strNum.Mid(posNe, lenN - posNe));

	strNum = stmp;
}

/////////////////////////////////////////////////////////////////////////////
//For a string like -001234.56e+2 (in C-locale, unformatted) we get:
// sDigits = "123456"  sSign = false   sExp = 3+2= 5
//as in scientific representation -1.23456E+5
//If the value is really 0 (i.e -00.000e0) we get 0E0
//Pass the whole string in sP.sDigits
void wxFormatStringAsNumber::StringToParts(stringParts* sP)
{
	//First strip out the sign
	if (sP->sDigits.GetChar(0) == m_cMinus)
	{
		sP->sSign = false;
		sP->sDigits.erase(0, 1);
	}
	else if (sP->sDigits.GetChar(0) == m_cPlus)
	{
		sP->sSign = true;
		sP->sDigits.erase(0, 1);
	}
	else
		sP->sSign = true;

	//Exponential
	int pos = sP->sDigits.Find(m_cExpU, true);
	if (pos == wxNOT_FOUND)
		pos = sP->sDigits.Find(m_cExpL, true);
	if (pos != wxNOT_FOUND)
	{   //Get the exponential value and remove this part
		wxString sexpo = sP->sDigits.Right((size_t)(sP->sDigits.Len() - pos - 1));
		if (!sexpo.ToCLong(&sP->sExp)) //seems we have "xxE-" (incomplete)
			sP->sExp = 0;
		sP->sDigits.erase((size_t)pos, (size_t)(sP->sDigits.Len() - pos));
	}
	else
		sP->sExp = 0;

	//Take decimal into account and remove decimal separator
	pos = sP->sDigits.Find(m_cDot);
	if (pos != wxNOT_FOUND)
	{
		sP->sDigits.erase((size_t)pos, 1);
		sP->sExp += pos - 1;
	}
	else
		sP->sExp += sP->sDigits.Len() - 1;

	//Test if number is really zero (i.e. "-00000e40" becomes "0")
	pos = 0; //the first non '0'
	while ((size_t)pos < sP->sDigits.Len())
	{
		if (sP->sDigits.GetChar((size_t)pos) != m_cZero)
			break;
		pos++;
	}
	if ((size_t)pos == sP->sDigits.Len())
	{   //It is really zero
		sP->sDigits = m_cZero;
		sP->sSign = true;
		sP->sExp = 0;
	}
	else if (pos > 0) //we've got something like 0001234
	{   //Remove zeros at left
		sP->sDigits.erase(0, (size_t)pos);
		sP->sExp -= pos;
	}
}

/////////////////////////////////////////////////////////////////////////////
// Round the unformatted srNum to the nearest number that fits in the
// sub-style and keeps its representation.
// The representation is given by the sub-style. We handle three types of
// representation: Scientific, Engineering and normal
//
// Rounding may change the number: 9.99 rounded to one decimal gives 10.0
// So, we must be aware of the representation
//
// The number 1234.98E-01 with the style "[#].'.'00N+##" is 123.49E+0 without
// rounding (note the truncation) and 123.50E+0 with rounding

// It changes srNum not only because of rounding, but also because Sci/Eng
// style

void wxFormatStringAsNumber::RoundAndForm(int usesty, wxString& srNum, bool doRound)
{
	if (srNum.IsEmpty())
		return;

	//Number of decimals to round and type of exponential
	int nuDecs;
	int typeExp;
	if (usesty == 1)
	{
		nuDecs = m_Sstypos.iround;
		typeExp = m_Sstypos.typExp;
	}
	else if (usesty == 2)
	{
		nuDecs = m_Sstyneg.iround;
		typeExp = m_Sstyneg.typExp;
	}
	else
	{
		nuDecs = m_Sstyzer.iround;
		typeExp = m_Sstyzer.typExp;
	}
	if (nuDecs < 0)
		doRound = false;

	if (!doRound && typeExp != eExpSci && typeExp != eExpEng)
		return; //nothing to do

	//Vars we use
	wxString str; //We construct this with some of srNum
	bool nsign = false;
	long vExp = 0;
	int posDot = srNum.Find(m_cDot); //The '.' position
	bool hasDot = (posDot == wxNOT_FOUND) ? false : true;
	int posExp = -1; //exponential position in srNum
	int posDig = -1; //for the digit to analyse

	//If we are using Scientific or Engineering style, get the digits
	if (typeExp == eExpSci || typeExp == eExpEng)
	{   //split number into its parts, Sci notation
		stringParts sP;
		sP.sDigits = srNum;
		StringToParts(&sP);
		str = sP.sDigits;
		nsign = sP.sSign; //store sign out of this block
		vExp = sP.sExp;   // and also exponent
		posDot = 1; //simulated dot position. str is "123" for srNum = 1.23E+98
		if (typeExp == eExpEng)
		{   //The exponent must be a multiple of 3
			while (vExp % 3 != 0)
			{
				posDot++;
				vExp--;
			}
			if ((size_t)posDot > str.Len())
				str.Append(m_cZero, (size_t)posDot - str.Len()); //1E5 => 100E3
		}
		posDig = posDot + nuDecs; //Don't add '1' because in fact there's no dot
	}
	else // a "normal" style
	{   //We need the digit to the right of the number of decimals to round
		if (posDot == wxNOT_FOUND)
			return; //no decimal part to round off

		posExp = srNum.Find(m_cExpU, true);
		if (posExp == wxNOT_FOUND)
			posExp = srNum.Find(m_cExpL, true);

		posDig = posDot + nuDecs + 1;
		if ((size_t)posDig >= srNum.Len()
			|| (posExp != wxNOT_FOUND && posDig >= posExp))
			return; //not enough decimal part to round off

		//The string we analyse
		str = srNum.SubString(0, posDig);
	}

	// Do rounding off to the nearest:  1.25 => 1.3   1.24 => 1.2  -1.88 => -1.9
	//
	// posDig is the digit we look at. If it is 0-4, we finish. Otherwise, we
	// must change (i.e. add one) the digit at its left and continue towards left
	// if this new digit is '0' (because 9+1 = 0)
	if (doRound && (size_t)posDig < str.Len())
	{   // digit 5-9 starts this process
		wxUniChar curDig;
		wxUniChar c1 = wxT('1');
		wxUniChar c5 = wxT('5');
		wxUniChar c9 = wxT('9');
		curDig = str.GetChar((size_t)posDig);
		str.Remove((size_t)posDig); //Remove this rounded digit off
		if (curDig >= c5 && curDig <= c9)
		{   //We stop when addOne is false or no more digits towards left
			bool addOne = true; // 9 + 5 = 4  and add one to the next digit

			while (--posDig >= 0)
			{
				curDig = str.GetChar((size_t)posDig);
				if (!fs_IsDigit(curDig))
					continue; //skip this character
				if (curDig == c9)
				{   //replace with '0' and continue
					str.SetChar((size_t)posDig, m_cZero);
				}
				else
				{   //I don't want to do ToLong/FromLong conversions just to add one.
					//Instead, I use the wxUniChar value. All Unicode tables show digits
					//starting with '0' the '1' then '2'... so it's safe to operate with
					//its value (the 'code point') to get the next digit.
					str.SetChar((size_t)posDig, wxUniChar(curDig.GetValue() + 1));
					addOne = false;
					break;
				}
			}
			if (addOne)
			{   //avance to the left of first digit
				posDig = 0;
				curDig = str.GetChar((size_t)posDig);
				while (!fs_IsDigit(curDig) && curDig != m_cDot)
				{
					posDig++;
					curDig = str.GetChar((size_t)posDig);
				}
				//add new digit '1' and move dot
				str.insert(posDig, c1);
				posDot++;
				if (typeExp == eExpEng && str.Len() > 3)
				{   //Don't remove now last digit, StrFor() will do it
					vExp += 3;
					posDot -= 3;
				}
				else if (typeExp == eExpSci)
				{   //Don't remove now last digit, StrFor() will do it
					vExp += 1;
					posDot--;
				}
			}
		}
	}

	//Construct the string
	if (typeExp == eExpSci || typeExp == eExpEng)
	{   //from pieces
		if ((size_t)posDot < str.Len())
			str.insert(posDot, m_cDot);
		else if (hasDot) //the original string has it, keep it
			str += m_cDot;
		srNum.Empty();
		if (!nsign)
			srNum << m_cMinus;
		srNum += str;
		srNum += m_cExpU;
		srNum << vExp;
	}
	else
	{   //just add the exponential part
		str.Append(srNum.Mid(posExp));
		srNum = str;
	}

}

