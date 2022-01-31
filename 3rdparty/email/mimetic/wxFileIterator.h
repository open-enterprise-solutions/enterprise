/////////////////////////////////////////////////////////////////////////////
// Name:        wxSMTP.h
// Purpose:     This file implements a C++ STL file iterator from a wxFileInputStream.
//              This allows minimising changes performed to the original Mimetic library
//              for an integration within this wxWidgets component
// Author:      Brice André
// Created:     2010/12/01
// RCS-ID:      $Id: mycomp.cpp 505 2007-03-31 10:31:46Z frm $
// Copyright:   (c) 2010
// Licence:     wxWidgets licence
/////////////////////////////////////////////////////////////////////////////

#ifndef WX_FILE_ITERATOR_H
#define WX_FILE_ITERATOR_H

#include "wx/wfstream.h"

class wxFileIterator : public std::iterator<std::forward_iterator_tag, char>
{
   public:


      wxFileIterator(wxFile& file, bool is_begin)
      {
         internal = new Internal();
         internal->nb_references = 1;
         internal->is_begin = is_begin;
         if (is_begin)
         {
            internal->stream = new wxFileInputStream(file);
         }
         else
         {
            internal->stream = NULL;
         }
      }

      wxFileIterator(const wxFileIterator& other)
      {
         internal = other.internal;
         internal->nb_references++;
      }

      ~wxFileIterator()
      {
         internal->nb_references--;
         if (internal->nb_references == 0)
         {
            if (internal->is_begin)
            {
               delete internal->stream;
            }
            delete internal;
         }
      }

      wxFileIterator& operator++()
      {
         internal->stream->GetC();
         return *this;
      }

      char operator*() const
      {
          char val = internal->stream->Peek();
          return val;
      }

      bool operator!=(const wxFileIterator& other) const
      {
         if (!other.internal->is_begin)
         {
            if (!this->internal->is_begin)
            {
               return false;
            }
            else
            {
               /* Note that EOF is discovered when we try to read byte */
               internal->stream->Peek();
               return !internal->stream->Eof();
            }
         }
         else
         {
            if (!this->internal->is_begin)
            {
               /* Note that EOF is discovered when we try to read byte */
               other.internal->stream->Peek();
               return !other.internal->stream->Eof();
            }
            else
            {
               return this->internal->stream->TellI() != other.internal->stream->TellI();
            }
         }
      }

      wxFileIterator& operator=(const wxFileIterator& other) const;

      wxFileIterator operator +(const wxFileIterator& b) const;
      wxFileIterator operator -(const wxFileIterator& b) const;
      wxFileIterator operator +() const;
      wxFileIterator operator -() const;
      wxFileIterator operator *(const wxFileIterator& b) const;
      wxFileIterator operator /(const wxFileIterator& b) const;
      wxFileIterator operator %(const wxFileIterator& b) const;
      wxFileIterator operator ++(int);
      wxFileIterator& operator --();
      wxFileIterator operator --(int);

      bool operator ==(const wxFileIterator& b) const
      {
         return !(*this != b);
      }
      bool operator >(const wxFileIterator& b) const;
      bool operator <(const wxFileIterator& b) const;
      bool operator >=(const wxFileIterator& b) const;
      bool operator <=(const wxFileIterator& b) const;

      bool operator !() const;
      bool operator &&(const wxFileIterator& b) const;
      bool operator ||(const wxFileIterator& b) const;

      wxFileIterator operator ~() const;
      wxFileIterator operator &(const wxFileIterator& b) const;
      wxFileIterator operator |(const wxFileIterator& b) const;
      wxFileIterator operator ^(const wxFileIterator& b) const;
      wxFileIterator operator <<(const wxFileIterator& b) const;
      wxFileIterator operator >>(const wxFileIterator& b) const;

      wxFileIterator& operator +=(const wxFileIterator& b);
      wxFileIterator& operator -=(const wxFileIterator& b);
      wxFileIterator& operator *=(const wxFileIterator& b);
      wxFileIterator& operator /=(const wxFileIterator& b);
      wxFileIterator& operator %=(const wxFileIterator& b);
      wxFileIterator& operator &=(const wxFileIterator& b);
      wxFileIterator& operator |=(const wxFileIterator& b);
      wxFileIterator& operator ^=(const wxFileIterator& b);
      wxFileIterator& operator <<=(const wxFileIterator& b);
      wxFileIterator& operator >>=(const wxFileIterator& b);


   private:

      class Internal
      {
         public:
            unsigned long nb_references;
            wxFileInputStream* stream;
            bool is_begin;
      };

      Internal* internal;
};

#endif /* WX_FILE_ITERATOR_H */
