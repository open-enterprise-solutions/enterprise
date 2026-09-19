#ifndef __IB_DEBUGGER_SOCKET_LOCK_H__
#define __IB_DEBUGGER_SOCKET_LOCK_H__

#include <mutex>

#include <wx/socket.h>

// ⭐⭐ A SOCKET THAT MORE THAN ONE THREAD MAY WANT TO CLOSE — and wxSocketBase::Close() is not written
// to be called twice at once.
//
// wxSocketImpl::Close() is `if (m_fd != INVALID_SOCKET) { DoClose(); m_fd = INVALID_SOCKET; }`: a check,
// then the work, with nothing between them. Two threads that both pass the check both run DoClose().
// On Windows and Linux that is a second closesocket() on a dead descriptor. On macOS DoClose() removes
// the socket's source from the run loop and RELEASES it, so the second thread would hand
// CFRunLoopRemoveSource a source that is already gone — which is the shape of the designer's crash in
// #155 (`CFRunLoopRemoveSource -> CFSetContainsValue -> CFHash`, on the connection's own thread).
//
// ⚠ THAT IS AN INFERENCE FROM THE STACK, NOT A REPRODUCTION. Two threads calling Close() on one
// loopback socket, 6000 rounds on macOS 15.6 / arm64, did not crash (tests/test_socketLock.cpp keeps the
// control): the window is a few instructions wide. What is certain is that the debugger's client
// connection DOES close one socket from two threads — the designer's main thread ends a session
// (DetachConnection, from the Debug menu or `app_run restart`) while the connection's own thread,
// having just watched the far end go away, closes it on its way out — and that closing twice at once
// is not something wx promises to survive.
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
