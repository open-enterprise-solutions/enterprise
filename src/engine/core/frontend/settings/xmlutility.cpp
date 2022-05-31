
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

            int code = 0;
            int length = 0;

            if (sscanf(src.c_str() + i + 2, "%d;%n", &code, &length) == 1)
            {
                dst += (char)code;
                i += length + 1; 
            }

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

    while (child != NULL)
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
    unsigned long temp;
    
    if (ReadXmlNode(node, tag, text) && sscanf(text, " #%x ", &temp) == 1)
    {
        color = wxColour((temp & 0xFF0000) >> 16, (temp & 0x00FF00) >> 8, temp & 0x0000FF);
        return true;
    }

    return false;

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

    if (xNode == NULL || yNode == NULL || xSizeNode == NULL || ySizeNode == NULL)
    {
        return false;
    }

    return ReadXmlNode(xNode, "x", rect.x) && ReadXmlNode(yNode, "y", rect.y) &&
           ReadXmlNode(xSizeNode, "xSize", rect.width) && ReadXmlNode(ySizeNode, "ySize", rect.height);

}

wxXmlNode* FindChildNode(wxXmlNode* node, const wxString& name)
{

    assert(node != NULL);
    wxXmlNode* child = node->GetChildren();

    while (child != NULL && child->GetName() != name)
    {
        child = child->GetNext();
    }

    return child;

}