#ifndef __WINDOW_PTR_H__
#define __WINDOW_PTR_H__

#include <wx/wx.h>

template<typename T>
class ibWindowPtr : public wxSharedPtr<T> {

	struct CWindowDeleter {
		void operator()(wxWindow* win) {}
	};

public:

	typedef T element_type;

	explicit ibWindowPtr(element_type* win)
		: wxSharedPtr<T>(win, CWindowDeleter())
	{
		win->Bind(wxEVT_CLOSE_WINDOW,
			[=](wxCloseEvent& event) { win->Destroy(); }
		);
	}

	ibWindowPtr() {}
	ibWindowPtr(const ibWindowPtr& tocopy) : wxSharedPtr<T>(tocopy) {}

	ibWindowPtr& operator=(const ibWindowPtr& tocopy) {
		wxSharedPtr<T>::operator=(tocopy);
		return *this;
	}

	ibWindowPtr& operator=(element_type* win) {
		return operator=(ibWindowPtr(win));
	}

	void reset(T* ptr = NULL) {
		wxSharedPtr<T>::reset(ptr, CWindowDeleter());
	}
};

#endif // __WINDOW_PTR_H__