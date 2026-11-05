# Record locks

**What.** Two people who open the same document and both save it must not lose one of the saves without
a word. Three layers keep that:

1. **A version on the object** (`DataVersion`): a save made from a stale read is refused.
2. **A database row lock** for the length of the write's transaction: two saves racing on one row are
   taken one at a time.
3. **A lock a session holds** (`sys_lock`): while a form is open, or while a script holds it, others see
   who is editing.

**Why.** A transaction held open while a person edits would hold database locks for minutes and pin a
connection; a lock column on the data row would write to the data on every open. So the database keeps
its locks to the moment of writing, and a person's edit is an application lock, a row of its own table
that every server of the base sees.

## How it works

- **The version.** Catalogs, documents, charts of accounts, of characteristic types and of calculation
  types, and parameterized jobs carry `DataVersion`. Reading takes it; writing reads it again under the
  row lock and stamps a new one. A different stamp refuses the write: *the object was changed by another
  user*.
- **The row lock.** Inside the write's transaction the object's row is read `FOR UPDATE` (PostgreSQL) or
  `WITH LOCK` (Firebird). A second writer waits for the first to commit, then meets its version.
- **A record set** has no version: it is written whole for its key. In the transaction its existing lines
  for that key are locked for update, and a posting document holds its own row and its movements to one
  commit.
- **A constant** is written under a lock on the constants' row.
- **The session's lock.** Opening an object's form takes an exclusive lock on it; `obj.Lock()` and
  `obj.Unlock()` do the same from a script, on the object kinds that carry a version. Saving and deleting
  take it again first. It is let go when the object is, and with the session.

## What it guarantees

- **No lost update.** Of two saves made from the same read, the second is refused with the reason;
  nothing is merged, nothing is overwritten.
- **The form opens anyway.** An object another session holds opens, says who holds it, and its save is
  refused while the lock stands. Once the holder lets go, the next refresh takes it.
- **A lock names its holder** - the user and the computer, written when it was taken, so it can still be
  named after the holder's session is gone.
- **One set of locks per base.** They are rows of the base, so every server of the base sees the same
  ones ([application-cluster.md](application-cluster.md)).
- **A lock dies with its owner.** Logging out drops a session's locks, and a sweep drops every lock whose
  session is no longer alive. The question is "who is alive", so a lock cannot outlive the last mention of
  its owner and block a record for good.
- **An administrator sees and releases them.** The designer's active-users window has a page of locks
  with a forced release; over MCP, `lock_list`.
- **Reading is not locked.** Lists, reports and forms read the last committed data and never wait.

## Where it stops

- A script locks one object, exclusively. There is no lock over a range of register lines, no shared lock
  and no wait of a chosen length.
- The last writer of a record set wins: a set written whole for its key has no read-edit-write cycle for a
  version to protect.
- A conflict is found at saving, not prevented at opening: a person can edit an object someone else holds
  and learn it at Save.
