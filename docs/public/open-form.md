# Opening a form, and ending the session

**What.** Four globals, and one wait. `OpenForm` opens a form and can be told what to call when that form closes. `AttachIdleHandler` asks the session to call a named procedure on a period. `InfoBaseConnectionString` answers with the string this base was opened with. `Exit` ends the session.

**Why.** A script that opens a form has to learn what the user did, without the next line running as if the form were still on screen. A long-running session has to do a small thing every few seconds without a window owning the clock. A script has to be able to say which base it is in, and to end that session, including from a host that has no window.

## OpenForm

```
OpenForm(name, params, owner, uniqueKey, notify, windowMode)
```

| Argument | Holds |
|---|---|
| `name` | Which form, the same name `GetCommonForm` and an object's `GetForm` already take. Empty means the object's own form |
| `params` | A `Structure` of opening parameters. Each name is written into the form request's named values |
| `owner` | The form or frame this one belongs to. Empty means none |
| `uniqueKey` | The key the window is opened under. A second open with the same key brings that window forward. Empty means one window per source |
| `notify` | A `NotifyDescription`, or empty. See below |
| `windowMode` | How the window is shown. The backend records it on the request. The frame reads it |

`OpenForm` returns the form. It uses the form request the platform already builds a form from: the name, the unique key, and the structure's names as the request's named values.

**On close.** When the form accepts `CloseForm`, and a notify was given, the form calls `notify.Call` with the close result and the description's additional parameters. A close the form refuses does not call it. The description is the value from `New NotifyDescription(procedureName, module, additionalParameters)`: `Call` runs `module.procedure(result, additionalParameters)`.

**Where it stops.** The backend does not create a window. A host with no frame still receives the form. Closing that form from the test, or from the form's own module, is what runs the notify. The frame, when there is one, is what shows the window in `windowMode`.

## Waiting until the form closes

A notify is the completion that does not block the caller. A wait is the other shape of the same close: the caller does not continue until that form has accepted its close and the notify has run, and then it continues with the value the notify received.

The wait belongs to the frame. A modal `windowMode` means `ShowForm` does not return until the window has closed, and the notify still runs on that close, so both shapes see one result. A session with no window cannot wait on a user. `OpenForm` returns the form, the script continues, and the notify is the completion. The backend does not run a window event loop.

## AttachIdleHandler

```
AttachIdleHandler(name, seconds)
DetachIdleHandler(name)
```

`name` is a procedure of the session's root module. `seconds` is the period. The session keeps a list of `{name, seconds, next}`. Something the host already does — the window's idle, or a test — asks the session to run the procedures that are due. The backend does not own a window timer.

A host that never asks (a daemon, a headless run) keeps the registration and runs nothing. A test asks, and the named procedure runs.

`DetachIdleHandler` drops that name. A second attach of the same name replaces the period.

## InfoBaseConnectionString

```
InfoBaseConnectionString()
```

Returns one string: the connection this session's base was opened with. A file base answers with its file. A server base answers with the server and the database it was given. It reads what the open already stored. It does not open a second connection, and it does not invent a second format.

## Exit

```
Exit(code)
```

Ends the current session the way `EndJob` does: `ibSession::Close`. The code is kept on the session so the process host can use it when the last session ends. It does not terminate the process. A session that refuses the close leaves the code unset and the session running. A host with no window ends the session in `Close`, as it does for `EndJob`.
