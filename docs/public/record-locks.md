# Record locks

**What.** Two people who open the same document and both save it must not lose one of the saves without
a word, and two postings must not both sell the same balance. Four layers keep that:

1. **A version on the object** (`DataVersion`): a save made from a stale read is refused.
2. **A database row lock** for the length of the write's transaction: two saves racing on one row are
   taken one at a time.
3. **A lock a session holds** (`sys_lock`): while a form is open, or while a script holds it, others see
   who is editing.
4. **A query's `LOCK`** (`sys_lock` too): values of a table's fields, held from where it stands in a
   package until the transaction ends, or while a `TempDataManager` holds it - a balance read after it is
   the one the posting writes from.

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

## The query's lock

```
SELECT Product, Warehouse INTO Goods FROM Document.Sale.Goods WHERE Ref = &Ref;
LOCK FROM AccumulationRegister.Stock WHERE (Product, Warehouse) IN (SELECT Product, Warehouse FROM Goods);
SELECT Product, Warehouse, QuantityBalance
FROM AccumulationRegister.Stock.Balance(, Product IN (SELECT Product FROM Goods))
```

- **A statement of the package:** `LOCK [SHARED] [<name>] FROM <table> { JOIN <table> ON … } [WHERE …]`.
  It takes its locks where it stands; what is read after it is read under it. Exclusive unless `SHARED`.
  It answers with the number of keys it holds.
- **It lives until the transaction ends** - committed or rolled back; an `UNLOCK` of it lets go there too.
  Without a transaction and without a manager it is refused.
- **A manager holds it longer.** A query given a `TempDataManager` (its `TempDataManager` property) leaves
  the lock to the manager alone: past the commit, with no transaction at all, through every query the
  manager is given to - until `Close()`, an `UNLOCK <name>` in any of them, or the manager is gone.

  ```
  Manager = New TempDataManager();
  Q = New Query("LOCK Held FROM AccumulationRegister.Stock WHERE Product = &Product");
  Q.SetParameter("Product", Product);
  Q.TempDataManager = Manager;
  Q.Execute();
  // ... the stock of Product stays locked while Manager holds it
  Manager.Close();
  ```
- **A report holds it until the report is built** - from the prepare, with no transaction; others wait while
  it builds. A dynamic list's query takes none.
- **A conflict is waited out** for up to 30 s, then refused, naming the holder and since when. A wait whose
  holder waits for the one asking (a deadlock) is refused at once.
- **An object's write holds its `Ref`:** if its `Lock()` was called (an editor calls it as it opens the
  object), that lock - refused at once if another holds it; otherwise for the transaction, a held one
  waited out.

**A record set takes the same locks itself** (its register's, keyed by the set's filter):

- **Written or deleted** - exclusive, until the transaction ends: the filter with each line's dimensions, for
  the lines replaced and the lines written. A `LOCK SHARED` on a product's balance holds back the write of
  that product, not of another. Past 1000 lines, the filter alone.
- **Read in a transaction** - shared: its writers wait, other readers read.
- **`LockForUpdate`** - set before `Read()`, the read holds the filter exclusively: read, change and write
  are one stretch nobody writes into. Outside a transaction it is refused.

  ```
  BeginTransaction();
  Set = AccumulationRegisters.Stock.CreateRecordSet();
  Set.Filter.Recorder.Set(Ref);
  Set.LockForUpdate = True;
  Set.Read();
  // ... change the lines
  Set.Write();
  CommitTransaction();
  ```
- On SQLite (tests and logging only) an open transaction is the lock: one writer at a time holds the whole
  base there, so a `LOCK`, a set and an object in a transaction add none; a `TempDataManager` with no
  transaction holds its `LOCK` as anywhere else.

**In a LINQ block** `lock [shared]` is the same `LOCK` over the block's source, keyed by the `where` right
before it:

```
from s in Data.AccumulationRegisters.Stock.Balance
    where s.Product == product && s.Warehouse == warehouse
    lock
    where s.QuantityBalance > 0
    select s
```

- Taken before the first row is read. A `where` after it only filters; with none before it, the table is
  locked whole.
- The `where` is read as a query's condition: `field == value` keys it, the rest narrows nothing. A `where`
  that does not read as a query's condition is refused.
- One source: beside a `join` or a second `from` it is refused - a query's `LOCK` says what a join locks.

**Which records are locked:**

- **Values, not rows.** Every record of the table with those values - the ones there now and the ones
  written while the lock is held.
- **A table's key is its fields that `WHERE` - and, for a joined table, `ON` - ties to a value:**
  `field = value`, `field IN (list)`, `field IN (SELECT …)`, `(f1, f2) IN (SELECT …)`,
  `field = <table>.field`, joined by `AND`. A field tied to nothing is any value; a table with no key is
  locked whole. Every table the statement names is locked.
- **Any other condition is a query's, and narrows nothing** - a range, `<`, `OR`, `LIKE`, a walk through a
  reference: its field is held whole. `WHERE Item = &Item AND Period BETWEEN &From AND &To` locks every
  period of that item. Its names are checked as a query's.
- **A virtual table locks its table.** Its condition is part of the key, its period or date is not:
  `SliceLast(&Date, Product = &P)` locks every period of that product. A tabular section locks its
  object's table.
- **A field tied to another table's field takes the values that table has in the statement read without
  the locked one** - a product with no price yet is still locked with its order line. The `ON` of a `LEFT
  JOIN` does not key the side it keeps; `RIGHT` mirrors it; `FULL` keys neither side.
- **Two locks of one table meet when they agree on every field both name;** a field only one of them names
  agrees with anything. Shared meets shared without a conflict; exclusive meets everything.
- **An object's own lock is a lock of its `Ref`** (`obj.Lock()`, and the one its open form asks for):
  `LOCK FROM Catalog.Goods WHERE Ref = &Item` waits for that item's open form; a `LOCK` keyed by another field
  meets every open form of the catalog.

## What it guarantees

- **No lost update.** Of two saves made from the same read, the second is refused with the reason;
  nothing is merged, nothing is overwritten.
- **The form opens anyway.** An object another session holds opens, says who holds it, and its save is
  refused while the lock stands. Once the holder lets go, the next refresh takes it.
- **A lock names its holder** - the user and the computer, written when it was taken, so it can still be
  named after the holder's session is gone.
- **One set of locks per base.** They are kept in the base's lock file, `sys.locks` - beside a file base,
  in a server base's own folder - which every process of the base opens, so every server and every client
  of the base sees the same ones ([application-cluster.md](application-cluster.md)). No transaction is
  spent on a lock, and the database server takes no part.
- **A lock dies with its owner.** Logging out drops a session's locks, and a sweep drops every lock whose
  session is no longer alive. The question is "who is alive", so a lock cannot outlive the last mention of
  its owner and block a record for good.
- **An administrator sees and releases them.** The studio's active-users window has a page of locks
  with a forced release; over MCP, `lock_list`.
- **A query's read is not locked.** Lists, reports and forms read the last committed data and never wait;
  a record set read in a transaction is read under its filter.

## Where it stops

- `obj.Lock()` locks one object, exclusively. A `LOCK` holds values, not ranges: a range holds its field
  whole. The wait is 30 s, not chosen per lock.
- The locks are the application's: another `LOCK`, a form and a record set's read or write see them, a
  plain query read does not. Every path that must not race takes one.
- A value is first brought to its field's type, as an assignment brings it: `Price = 1.005` on a field with two
  decimals locks `1.01`.
- A value is told apart by its hash - a number by its value, `1` and `1.0` alike - so two different values
  may, rarely, count as one: more may be locked, never less.
- `UNLOCK` lets go of a manager's lock at once; a lock the transaction holds lives to its end.
- A manager's lock outlives the transaction by design: a manager forgotten in a long-lived variable holds
  its records until it is closed or gone.
- A record set keeps no version: one read outside a transaction, changed and written over a newer write,
  replaces it. Read it in the transaction that writes it - with `LockForUpdate` to change it.
- A conflict is found at saving, not prevented at opening: a person can edit an object someone else holds
  and learn it at Save.
