#ifndef __IB_DEBUGGER_SOCKET_LOCK_H__
#define __IB_DEBUGGER_SOCKET_LOCK_H__

#include <mutex>

#include <wx/socket.h>

// ⭐⭐ A SOCKET THAT MORE THAN ONE THREAD MAY WANT TO CLOSE — and wxSocketBase::Close() is not safe to
// call twice at once.
//
// wxSocketImpl::Close() is `if (m_fd != INVALID_SOCKET) { DoClose(); m_fd = INVALID_SOCKET; }`: a check,
// then the work, with nothing between them. Two threads that both pass the check both run DoClose(). On
// Windows and Linux that is a second closesocket() on a dead descriptor — harmless. On macOS DoClose()
// removes the socket's source from the run loop and RELEASES it, so the second thread hands
// CFRunLoopRemoveSource a source that is already gone, and the process dies inside CoreFoundation
// (`CFRunLoopRemoveSource -> CFSetContainsValue -> CFHash`), at an address that says nothing about who
// was late.
//
// The debugger's client connection is exactly that shape: the designer's main thread ends a session
// (DetachConnection, from the Debug menu or from `app_run restart`) while the connection's own thread,
// which has just watched the far end go away, closes the same socket on its way out. Both are right to
// close it. What was wrong was closing at the same moment.
//
// The lock takes them in turn: the second Close finds the descriptor already invalid and does nothing.
// Destroy takes the pointer OUT of its slot under the lock, so a Close that arrives after it finds the
// slot empty instead of a socket that is being freed — and only one thread ever calls Destroy on it.
//
// ⚠ IT SERIALISES CLOSING, NOT USING. A thread that is blocked in WaitForRead on the socket while
// another closes it is a separate hazard, and this does not remove it; the slot is what the owning
// thread reads through, and only the owner should be doing anything but Close with it.
class ibSocketLock {
public:

	// Close the socket in `slot`, if there is one. Any thread; two at once are taken in turn.
	// `slot` is read UNDER the lock — take it by reference, never by value at the call site.
	template <class Socket>
	void Close(Socket* const& slot) {
		const std::lock_guard<std::mutex> hold(m_mutex);
		if (slot != nullptr)
			slot->Close();
	}

	// Empty `slot` and destroy what was in it, once. Whoever gets here first destroys; everyone after
	// finds it empty.
	template <class Socket>
	void Destroy(Socket*& slot) {
		Socket* taken = nullptr;
		{
			const std::lock_guard<std::mutex> hold(m_mutex);
			taken = slot;
			slot = nullptr;
		}
		if (taken != nullptr)
			taken->Destroy();
	}

	// Put a new socket into `slot`. Under the lock so a concurrent Close sees the old one or the new
	// one, never a half-written pointer.
	template <class Socket>
	void Assign(Socket*& slot, Socket* socket) {
		const std::lock_guard<std::mutex> hold(m_mutex);
		slot = socket;
	}

	// For a reader that must look at the slot and the socket in one step (IsConnected) — the socket
	// cannot be destroyed between the null check and the call while this is held.
	std::unique_lock<std::mutex> Hold() const {
		return std::unique_lock<std::mutex>(m_mutex);
	}

private:
	mutable std::mutex m_mutex;
};

#endif // __IB_DEBUGGER_SOCKET_LOCK_H__
