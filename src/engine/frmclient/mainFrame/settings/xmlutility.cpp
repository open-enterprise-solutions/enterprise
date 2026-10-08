
#include "xmlutility.h"

#include <assert.h>

#include <wx/wx.h>
#include <wx/xml/xml.h>

void Unescape(const wxString& src, wxString& dst)
{
    for (size_t i = 0; i < src.Length(); ++i)
    {       
        if (src[i] == '&' && (i + 1 < src.Length()) && src[i + 1] == '#')
        {
            // Numeric entity "&#<digits>;". Scanned with wxString rather than sscanf: the
            // format-string route needs an exact argument type (an "%x"/"%d" into a wider
            // variable writes only part of it on LP64), and c_str() into a vararg has no
            // fixed narrow/wide answer.
            size_t j = i + 2;
            wxString digits;

            while (j < src.Length() && wxIsdigit(src[j])) {
                digits += src[j];
                ++j;
            }

            unsigned long code = 0;
            if (!digits.empty() && j < src.Length() && src[j] == ';' && digits.ToULong(&code)) {
                dst += static_cast<wxChar>(code);
                i = j;              // the loop's ++i steps past the ';'
                continue;
            }

            dst += src[i];          // not an entity after all — keep the '&' verbatim
        }
        else
        {
            dst += src[i];
        }

    }

}

wxXmlNode* WriteXmlNode(const wxString& tag, unsigned int data)
{
    return WriteXmlNode(tag, wxString::Format("%d", data));
}

wxXmlNode* WriteXmlNode(const wxString& tag, int data)
{
    return WriteXmlNode(tag, wxString::Format("%d", data));
}

wxXmlNode* WriteXmlNode(const wxString& tag, const wxString& data)
{
    wxXmlNode* node  = new wxXmlNode(wxXML_ELEMENT_NODE, tag);    
    node->AddChild(new wxXmlNode(wxXML_TEXT_NODE, tag, data));
    return node;
}

wxXmlNode* WriteXmlNode(const wxString& tag, const char* data)
{
    wxXmlNode* node  = new wxXmlNode(wxXML_ELEMENT_NODE, tag);    
    node->AddChild(new wxXmlNode(wxXML_TEXT_NODE, tag, data));
    return node;
}

wxXmlNode* WriteXmlNode(const wxString& tag, const wxColour& color)
{
    unsigned int value = (color.Blue()) + (color.Green() << 8) + (color.Red() << 16);
    return WriteXmlNode(tag, wxString::Format("#%06x", value));
}

wxXmlNode* WriteXmlNodeBool(const wxString& tag, bool data)
{
    return WriteXmlNode(tag, data ? "true" : "false");
}

wxXmlNode* WriteXmlNodeRect(const wxString& tag, const wxRect& rect)
{
    wxXmlNode* node  = new wxXmlNode(wxXML_ELEMENT_NODE, tag);    
    node->AddChild( WriteXmlNode("x", rect.x) );
    node->AddChild( WriteXmlNode("y", rect.y) );
    node->AddChild( WriteXmlNode("xSize", rect.width) );
    node->AddChild( WriteXmlNode("ySize", rect.height) );
    return node;
}

bool ReadXmlNode(wxXmlNode* node, const wxString& tag, wxString& data)
{

    if (node->GetName() != tag)
    {
        return false;
    }

    wxXmlNode* child = node->GetChildren();

    while (child != nullptr)
    {
        if (child->GetType() == wxXML_TEXT_NODE)
        {
            Unescape( child->GetContent(), data );
            return true;
        }
        child = child->GetNext();
    }

    return false;

}

bool ReadXmlNode(wxXmlNode* node, const wxString& tag, unsigned int& data)
{
    
    wxString text;
    unsigned long temp;
    
    if (ReadXmlNode(node, tag, text) && text.ToULong(&temp))
    {
        data = temp;
        return true;
    }

    return false;

}

bool ReadXmlNode(wxXmlNode* node, const wxString& tag, int& data)
{
    
    wxString text;
    long temp;
    
    if (ReadXmlNode(node, tag, text) && text.ToLong(&temp))
    {
        data = temp;
        return true;
    }

    return false;

}

bool ReadXmlNode(wxXmlNode* node, const wxString& tag, wxColour& color)
{
    
    wxString text;

    if (!ReadXmlNode(node, tag, text))
        return false;

    // "#RRGGBB" parsed through wxString: "%x" writes an unsigned INT, so scanning it into
    // an unsigned long left the top four bytes uninitialised wherever long is 64-bit.
    wxString hex;
    unsigned long temp = 0;

    if (!text.Trim(true).Trim(false).StartsWith(wxT("#"), &hex) || !hex.ToULong(&temp, 16))
        return false;

    color = wxColour((temp & 0xFF0000) >> 16, (temp & 0x00FF00) >> 8, temp & 0x0000FF);
    return true;

}

bool ReadXmlNode(wxXmlNode* node, const wxString& tag, bool& data)
{
    
    wxString text;
    
    if (ReadXmlNode(node, tag, text))
    {
        data = text.IsSameAs("true", false);
        return true;
    }

    return false;

}

bool ReadXmlNodeRect(wxXmlNode* node, const wxString& tag, wxRect& rect)
{

    if (node->GetName() != tag)
    {
        return false;
    }

    wxXmlNode* xNode = FindChildNode(node, "x");
    wxXmlNode* yNode = FindChildNode(node, "y");
    wxXmlNode* xSizeNode = FindChildNode(node, "xSize");
    wxXmlNode* ySizeNode = FindChildNode(node, "ySize");

    if (xNode == nullptr || yNode == nullptr || xSizeNode == nullptr || ySizeNode == nullptr)
    {
        return false;
    }

    return ReadXmlNode(xNode, "x", rect.x) && ReadXmlNode(yNode, "y", rect.y) &&
           ReadXmlNode(xSizeNode, "xSize", rect.width) && ReadXmlNode(ySizeNode, "ySize", rect.height);

}

wxXmlNode* FindChildNode(wxXmlNode* node, const wxString& name)
{

    assert(node != nullptr);
    wxXmlNode* child = node->GetChildren();

    while (child != nullptr && child->GetName() != name)
    {
        child = child->GetNext();
    }

    return child;

}