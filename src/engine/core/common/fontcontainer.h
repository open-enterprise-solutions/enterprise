#ifndef _FONTCONTAINER_H__
#define _FONTCONTAINER_H__

#include <wx/font.h>

/** @class wxFontContainer
    @brief Because the class wxFont cannot be a container for invalid font data (like default values).
*/
class wxFontContainer : public wxObject
{
public:
    int m_pointSize;		///< Point Size
	wxFontFamily m_family;  ///< Family
	wxFontStyle m_style;    ///< Style
	wxFontWeight m_weight;  ///< Weight
    bool m_underlined;		///< Underlined
    wxString m_faceName;	///< Face Name

    inline void InitDefaults()
    {
		m_pointSize = -1;
		m_family = wxFONTFAMILY_DEFAULT;
		m_style = wxFONTSTYLE_NORMAL;
		m_weight = wxFONTWEIGHT_NORMAL;
		m_underlined = false;
		m_faceName = wxT("Segoe UI");
    }

    wxFontContainer()
    {
    	InitDefaults();
	}

    inline wxFontContainer( const wxFont& font )
    {
        if ( !font.IsOk() )
        {
        	InitDefaults();
        }
        else
        {
        	m_pointSize = font.GetPointSize();
        	m_family = font.GetFamily();
        	m_style = font.GetStyle();
        	m_weight = font.GetWeight();
        	m_underlined = font.GetUnderlined();
        	m_faceName = font.GetFaceName();
        }
    }

	inline wxFontContainer(int pointSize, wxFontFamily family = wxFONTFAMILY_DEFAULT,
	                       wxFontStyle style = wxFONTSTYLE_NORMAL,
	                       wxFontWeight weight = wxFONTWEIGHT_NORMAL, bool underlined = false,
	                       const wxString& faceName = wxEmptyString)
	: m_pointSize(pointSize), m_family(family), m_style(style), m_weight(weight),
	  m_underlined(underlined), m_faceName(faceName) {
	}

	wxFont GetFont() const
	{
		int pointSize = m_pointSize <= 0 ? wxNORMAL_FONT->GetPointSize() : m_pointSize;
		return wxFont( pointSize, m_family, m_style, m_weight, m_underlined, m_faceName );
	}

	// Duplicate wxFont's interface for backward compatiblity
	#define MAKE_GET_AND_SET( NAME, TYPE, VARIABLE ) 	\
		TYPE Get##NAME() const { return VARIABLE; }		\
		void Set##NAME( TYPE value ){ VARIABLE = value; }

	MAKE_GET_AND_SET( PointSize, int, m_pointSize )
	MAKE_GET_AND_SET(Family, wxFontFamily, m_family)
	MAKE_GET_AND_SET(Style, wxFontStyle, m_style)
	MAKE_GET_AND_SET(Weight, wxFontWeight, m_weight)
	MAKE_GET_AND_SET( Underlined, bool, m_underlined )
	MAKE_GET_AND_SET( FaceName, wxString, m_faceName )

	// Allow implicit cast to wxFont
	operator wxFont() const
	{
		return GetFont();
	}
};

#endif