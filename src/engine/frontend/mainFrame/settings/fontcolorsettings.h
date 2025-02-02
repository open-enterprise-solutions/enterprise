#ifndef FONT_COLOR_SETTINGS_H
#define FONT_COLOR_SETTINGS_H

#include <wx/wx.h>
#include <vector>

#include "frontend/frontend.h"

//
// Forward declarations.
//

class wxXmlNode;

/**
 * This class stores font and color settings for code display and syntax
 * highlighting.
 */
class FRONTEND_API FontColorSettings
{

public:

    enum DisplayItem
    {
        DisplayItem_Default,
        DisplayItem_Comment,
        DisplayItem_Keyword,
        DisplayItem_Operator,
        DisplayItem_String,
        DisplayItem_Number,
        DisplayItem_Error,
        DisplayItem_Warning,
        DisplayItem_Selection,
        DisplayItem_Last,
    };

    struct Colors
    {
        wxColour    foreColor;
        wxColour    backColor;
        bool        bold;
        bool        italic;
    };

    /**
     * Constructor. Sets the font and colors to the defaults.
     */
    FontColorSettings();

    /**
     * Returns the number of display items the settings store information for.
     */
    unsigned int GetNumDisplayItems() const;

    /**
     * Returns the human-readable name for the ith display item.
     */
    const char* GetDisplayItemName(unsigned int i) const;

    /**
     * Sets the font used to display all of the styles.
     */
    void SetFont(const wxFont& font);

    /**
     * Gets the font used to display all of the styles.
     */
    const wxFont& GetFont() const;

    /**
     * Gets the font used to display a specific item. This includes the bold
     * and italic modifiers.
     */
    wxFont GetFont(DisplayItem displayItem) const;

    /**
     * Returns the colors for the specified display item.
     */
    const Colors& GetColors(DisplayItem displayItem) const;

    /**
     * Sets the colors for the specified display item.
     */
    void SetColors(DisplayItem displayItem, const Colors& colors);

    /**
     * Saves the font and color settings in XML format. The tag is the name that is given
     * to the root node.
     */
    wxXmlNode* Save(const wxString& tag) const;

    /**
     * Loads the font and color settings from XML format.
     */
    void Load(wxXmlNode* root);

private:

    static const char*          s_displayItemName[DisplayItem_Last];

    wxFont                      m_font;
    Colors                      m_colors[DisplayItem_Last];

};

#endif