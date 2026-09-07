# Who owns a tab

*2026-09-07.*

A form on the web opens as a tab: `ibWebFrame::m_tabs` is a
`vector<unique_ptr<ibWebDocChildFrame>>`, and that vector is the owner. The shell
also sits in the shared doc/view code as an `ibDocChildFrameAnyBase`, and that
code has its own idea of how a child frame ends — two of them:

```cpp
// ibView::~ibView
if (m_docChildFrame && m_docChildFrame->GetView() == this) {
    m_docChildFrame->SetView(nullptr);
    m_docChildFrame->GetWindow()->Destroy();
}

// ibDocChildFrameAny::OnCloseWindow
if (CloseView(event)) this->Destroy();
```

On the desktop `wxWindow::Destroy()` is a deferred delete of a window nobody else
owns, and both lines are right. On the web `ibWebWindow::Destroy()` is
`delete this` — on an object the frame's vector is holding. The vector then had a
`unique_ptr` to freed memory, and deleted it a second time when the frame went.

## What it looked like

The second delete lands in `~ibWebFrame`, in the loop that empties `m_tabs`:

```
libwfrontend.dylib`ibWebFrame::~ibWebFrame:
  <+352>: ldr x0, [x23, #-0x8]!    ; the unique_ptr's pointer
  <+364>: ldr x8, [x0]             ; its vptr — memory that belongs to someone else now
  <+368>: ldr x8, [x8, #0x8]       ; slot 1: the deleting destructor
  <+372>: blr x8                   ; EXC_BAD_ACCESS
```

So the log always ended the same way and said nothing:

```
[life] ~ibVisualHostClient …        (one pair per open tab)
[life] ~ibFormVisualDocument …
[app] ExitMainModule: done
[app] delete m_frame: begin
<exit>
```

It needed a real browser to reproduce — open a list, open an object from a row,
reload. Headless clicking never opened a form, so the sessions being torn down
had no tabs and there was nothing to double-free. Under `lldb` it is four out of
four.

## The two workarounds it grew

Both close roads had learned to sever the view's back-link first, so `~ibView`
would find `GetView() != this` and skip its `Destroy`:

```cpp
if (ibView* const dyingView = tab->GetView())
    dyingView->SetDocChildFrame(nullptr);
tab->SetView(nullptr);
doc->DeleteAllViews();
```

That holds for the tab in hand and breaks the one beside it. Closing a document
cascades: an object form goes down with the list it was opened from. A severed
sibling is not told, so it stays in `m_tabs` pointing at a document that no
longer exists — and the next turn of the loop asks it for that document:

```
libc++abi.dylib`dyn_cast_get_derived_info    ; dynamic_cast over freed memory
```

which is the same crash wearing a different hat.

## What it is now

One owner, one road out.

- `ibWebDocChildFrame::Destroy()` does not delete itself. It asks the frame:
  `m_ownerFrame->DropTab(this)`, which erases the entry — deleting the shell
  exactly once. A shell that was never adopted still deletes itself.
- `ibWebFrame::DropTab` answers `true` while `~ibWebFrame` is already emptying
  `m_tabs`, so a re-entrant request does not ask for a delete that is in flight.
- Nobody severs any more. A cascade takes each tab it kills out of `m_tabs` on
  its way through `~ibView`, so no shell outlives its document, and the close
  loops simply take the front tab until there is none:

```cpp
while (m_frame->TabCount() > 0) {
    const std::size_t before = m_frame->TabCount();
    ibWebDocChildFrame* tab = m_frame->Tab(0);
    if (auto* doc = dynamic_cast<ibFormVisualDocument*>(tab->GetDocument()))
        doc->DeleteAllViews();
    if (m_frame->TabCount() >= before && !m_frame->DropTab(tab)) break;
    if (m_frame->TabCount() >= before) break;
}
```

- A tab records the form it was opened for (`SetTabForm`, at adopt time). The
  places that used to ask a neighbour's document which form it carried now read
  that field instead — a value, not a dereference, which is the whole point when
  the neighbour may be halfway through the same cascade.

`DrainPendingCloses` (the script / tab-X road) is the same three lines: find the
tab by its recorded form, `DeleteAllViews`, `DropTab`. It no longer erases by
index — the cascade may already have moved things.
