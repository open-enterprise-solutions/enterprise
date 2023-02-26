/***************************************************************************
	copyright            : (C) 2002-2008 by Stefano Barbato
	email                : stefano@codesink.org

	$Id: field.cxx,v 1.7 2009-02-27 15:56:34 tat Exp $
 ***************************************************************************/

 /***************************************************************************

  Licence:     wxWidgets licence

  This file has been copied from the project Mimetic
  (http://codesink.org/mimetic_mime_library.html) and relicenced from the MIT
  licence to the wxWidgets one with authorisation received from Stefano Barbato

  ***************************************************************************/

#include <3rdparty/email/mimetic/rfc822/field.h>
#include <3rdparty/email/mimetic/strutils.h>
#include <3rdparty/email/mimetic/utils.h>
#include <3rdparty/email/mimetic/contenttype.h>
#include <3rdparty/email/mimetic/rfc822/mailboxlist.h>

namespace mimetic
{
	using namespace std;

	// static init
	const Field Field::null;

	/**
		Constructs a null Field
	*/
	Field::Field()
		:m_pValue(0)
	{
	}


	/**
		Parses \p line and sets \e name, \e value and \e valueText of *this
		\param line input field string as defined in RFC822 (fieldname: text)
	*/
	Field::Field(const string& line)
		:m_pValue(0)
	{
		string::size_type colon = line.find(':');
		if (colon != string::npos)
		{
			m_name.assign(line.begin(), line.begin() + colon);
			size_t i;
			for (i = 1 + colon; i < line.length() - 1 && line[i] == ' '; ++i)
				; // skip spaces before field-body
			string val(line.begin() + i, line.end());
			value(val);
		}
	}


	/**
		Initialize *this with \p n and \p v
		\param n field %name
		\param v content of the %field
	*/
	Field::Field(const string& n, const string& v)
		:m_pValue(0)
	{
		m_name = n;
		m_pValue = new StringFieldValue(v);
	}

	/**
		Copy constructor
	*/
	Field::Field(const Field& r)
		:m_name(r.m_name), m_pValue(0)
	{
		if (r.m_pValue)
			m_pValue = r.m_pValue->clone();
	}

	Field& Field::operator=(const Field& r)
	{
		m_name = r.m_name;
		if (m_pValue)
		{
			delete m_pValue;
			m_pValue = 0;
		}
		if (r.m_pValue)
			m_pValue = r.m_pValue->clone();
		return *this;
	}

	/**
		Destructor
	*/
	Field::~Field()
	{
		if (m_pValue)
		{
			delete m_pValue;
			m_pValue = 0;
		}
	}

	/**
		Sets the field name to \p n
		\param name new %field %name
	*/
	void Field::name(const string& name)
	{
		m_name = name;
		if (m_pValue != 0)
		{
			delete m_pValue;
			m_pValue = 0;
		}
	}

	/**
		Sets the field value to \p v
		\param val new %value %name
	*/
	void Field::value(const string& val)
	{
		if (m_pValue == 0)
			m_pValue = new StringFieldValue(val);
		else
			m_pValue->set(val);
	}

	/**
		Returns the field name
	*/
	const istring& Field::name() const
	{
		return m_name;
	}




	/**
		Returns the field value.
	*/
	string Field::value() const
	{
		if (m_pValue == 0)
			return nullstring;
		return m_pValue->str();
	}

	std::ostream& operator<<(std::ostream& os, const Field& f)
	{
		return f.write(os, 0);
	}

	ostream& Field::write(ostream& os, unsigned int fold) const
	{
#if defined(_LP64) || defined(__LP64__) || defined(__arch64__) || defined(_WIN64)
		long long in_quote, prev, skip;
#else 
		int in_quote, prev, skip;
#endif 

		// default folding idiot-algorithm
		// override to customize
		if (fold)
		{
			size_t i, sp;
			string ostr = name() + ": " + value();

			// skip the "fieldname: " part just on the first inner iteration
			skip = name().length() + 2;

			while (ostr.length() > fold)
			{
				prev = 0; // prev char processed
				in_quote = 0;
				sp = 0;

				for (i = skip; i < ostr.length(); ++i)
				{
					if (ostr[i] == '"' && prev != '\\')
						in_quote = !in_quote;

					if (!in_quote && isblank(ostr[i]))
						sp = i; // last blank found

					if (i >= fold && sp)
					{
						os.write(ostr.c_str(), sp);
						ostr.erase(0, 1 + sp);
						if (ostr.length() && !utils::string_is_blank(ostr))
							os << crlf << "\t"; // keep folding
						break;
					}

					prev = ostr[i];
				}
				if (sp == 0)
					break; // can't fold anymore
				skip = 0;
			}

			os << ostr;
			return os;
		}
		else
			return os << name() << ": " << value();
	}


}
