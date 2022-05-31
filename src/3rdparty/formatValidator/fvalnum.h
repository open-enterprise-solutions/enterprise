/////////////////////////////////////////////////////////////////////////////
// Name:        fvalnum.h
// Purpose:     wxNumberValidator header
// Author:      Manuel Martin
// Modified by: MM Apr 2012
// Created:     Mar 2003
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _VALNUM_H
#define _VALNUM_H

#ifdef __GNUG__
#pragma interface "fvalnum.h"
#endif

#if wxUSE_VALIDATORS

#if wxUSE_TEXTCTRL
#include "wx/textctrl.h"
#endif
#if wxUSE_COMBOBOX
#include "wx/combobox.h"
#endif

#include "wx/validate.h"

#include "forstrnu.h"

enum
{ //behaviour flags
	wxUF_ROUND = 128,
	wxVAL_ON_CHAR = 256,
	wxVAL_ON_ENTER_TAB = 1024,
	wxVAL_ON_KILL_FOCUS = 2048,
	wxTRANSFER_UNFORMATTED = 4096,
	wxVAL_RETAIN_FOCUS = 8192,
	wxVAL_NO_MSG_SHOWN = 16384,
	wxTRANSFER_UNFORMATTED_NO_ZEROS = wxTRANSFER_UNFORMATTED | wxTZ_BOTH,
	wxVAL_DEFAULT = wxUF_BESTRICT | wxVAL_ON_ENTER_TAB,
	wxVAL_ON_EDIT = wxUF_NOSTRICT | wxVAL_ON_CHAR | wxVAL_ON_ENTER_TAB
};

enum
{   //other flags
	fVALFORMAT_NONE = 1,
	fVALFORMAT_NORMAL = 2,
	fVALFORMAT_TYPING = 4,
	fVALFORMAT_CURRENT = 8,
	fCHE_MIN = 16,
	fCHE_MAX = 32,
	fCHE_EXCLUDED = 64,
	fCHE_LIMITTOMIN = 128,
	fCHE_LIMITTOMAX = 256
};

struct wxFValNumColors
{
	wxColor colOKForegn;
	wxColor colOKBackgn;
	wxColor colFaForegn;
	wxColor colFaBackgn;
};

/////////////////////////////////////////////////////////////////////////////
class wxNumberValidator : public wxValidator
{
	wxDECLARE_DYNAMIC_CLASS(wxNumberValidator);
public:
	wxNumberValidator(const wxString& valStyle = wxT("-[#].'.'*E-###"),
		int behavior = wxVAL_DEFAULT,
		wxString *val = 0);
	wxNumberValidator(const wxNumberValidator& val);

	~wxNumberValidator();

	// Make a clone of this validator (or return NULL) - currently necessary
	// if you're passing a reference to a validator.
	// Another possibility is to always pass a pointer to a new validator
	// (so the calling code can use a copy constructor of the relevant class).
	virtual wxObject *Clone() const { return new wxNumberValidator(*this); }
	bool Copy(const wxNumberValidator& val);

	// Called when the value in the window must be validated.
	// This function can pop up an error message.
	virtual bool Validate(wxWindow *parent);

	// Called to transfer data to the window
	virtual bool TransferToWindow();

	// Called to transfer data from the window
	virtual bool TransferFromWindow();

	// ACCESSORS
	inline int GetBehavior() const { return m_validatorBehavior; }
	inline void SetBehavior(int behavior) { m_validatorBehavior = behavior; }

	//Gets the decimal separator used
	wxString GetDecSep() { return m_styleWorker->GetDecSep(); }

	//Return the currently used style
	wxString GetStyle() { return m_styleWorker->GetStyle(); }
	//The two used styles
	wxString GetNormalStyle() { return m_normalStyle; }
	wxString GetTypingStyle() { return m_typingStyle; }
	//The normal style
	int SetStyle(const wxString& valstyle);
	//The style to use while typing
	int SetStyleForTyping(const wxString& valOCstyle);

	//Validation
	int ValidateQuiet();
	inline wxString GetLastError() { return m_lastError; }

	//Shows a message or perhaps a sound
	void MsgOrSound(wxWindow* parent);

	//Formatted/unformatted value
	wxString GetFormatted(int formatType = fVALFORMAT_CURRENT);
	void SetFormatted(int formatType = fVALFORMAT_CURRENT);

	//Min and max values
	inline wxString GetMin() { return m_stringMin; }
	inline wxString GetMax() { return m_stringMax; }
	bool SetMin(const wxString& smin, bool asLimit = false);
	bool SetMax(const wxString& smax, bool asLimit = false);
	inline bool IsMinLimited() const { return m_MinLimit; }
	inline bool IsMaxLimited() const { return m_MaxLimit; }
	bool CheckMinMax(const wxString& unformatted, wxString &sRes);

	//Other accepted strings
	inline void SetAlsoValid(wxArrayString& aOther) { m_otherStrings = aOther; }
	inline wxArrayString& GetAlsoValid() { return m_otherStrings; }

	//How we treat empty
	bool SetEmptyAs(bool allowIt = true, const wxString& emptyAs = wxT("0"));
	inline bool IsEmptyAllowed() const { return m_AllowEmpty; }
	wxString GetEmptySurrogate() { return m_emptySurrogate; }

	//Colors
	void SetColors(const wxFValNumColors& valColors)
	{
		m_colors.colOKForegn = valColors.colOKForegn;
		m_colors.colOKBackgn = valColors.colOKBackgn;
		m_colors.colFaForegn = valColors.colFaForegn;
		m_colors.colFaBackgn = valColors.colFaBackgn;
	}
	wxFValNumColors& GetColors() { return m_colors; }

	//'Esc' key processing
	inline void RestoreWithEsc(bool toUse = true)
	{
		m_EscIsOn = toUse;
		m_EscFired = false;
	}

	DECLARE_EVENT_TABLE()

protected:
	//The associated base class wxTextEntry, common to wxTextCtrl and wxComboBox
	wxTextEntry* GetTextEntry() const;

	void OnKeyDown(wxKeyEvent& event);
	void OnChar(wxKeyEvent& event);
	void OnSetFocus(wxFocusEvent& event);
	void OnKillFocus(wxFocusEvent& event);
	void OnIdle(wxIdleEvent& event);
	//Deletion-keys and selection removing
	long DelAndSelection(int delType);
	//Test the string and, if valid, set it to control
	void TestAndSet(const wxString& testStr, wxTextEntry* control, long rPos);

	//Set the style and return 'strict' or 'no-strict'
	int StyleMode();

	//Test the other valid string, returning a matched entry
	wxString TestOthers(const wxString& sToTest, int beStrict);

	//Returns value with or without format
	wxString DoGetFormatted(int formatType, bool &isReplaced);

	//Make the control colored.
	enum { cNormal = 1, cOK, cFA }; //tell the color for control
	bool GetNormalColors(wxColor &fgColor, wxColor &bgColor);
	void SetForeBackColors(int colvalType);

private:
	//Dealing with big numbers, we do their comparison ourselves
	long CompParts(const stringParts& sP1, const stringParts& sP2);

	int                         m_validatorBehavior;
	wxString *                  m_stringValue;
	wxString                    m_stringMin;
	wxString                    m_stringMax;
	wxString                    m_normalStyle;
	wxString                    m_typingStyle;
	wxString                    m_emptySurrogate;
	wxString                    m_lastValue;
	wxString                    m_lastError;
	wxArrayString               m_otherStrings;
	wxFormatStringAsNumber *    m_styleWorker;
	wxFValNumColors             m_colors;
	bool                        m_MinLimit;
	bool                        m_MaxLimit;
	bool                        m_AllowEmpty;
	bool                        m_UseNormalStyle;
	bool                        m_ValueIsValid;
	bool                        m_LastCharWasNUMPADDEC;
	bool                        m_WantFocus;
	bool                        m_EscIsOn;
	bool                        m_EscFired;
	int                         m_CheckRes;

private:
	// Cannot use
	//  DECLARE_NO_COPY_CLASS(wxNumberValidator)
	// because copy constructor is explicitly declared above;
	// but no copy assignment operator is defined, so declare
	// it private to prevent the compiler from defining it:
	wxNumberValidator& operator=(const wxNumberValidator&);
};

#endif // wxUSE_VALIDATORS

#endif //_VALNUM_H

