# Job schedule — the parameter surface

Reference for what a schedule can express, mapped against the shape users already know from
comparable systems (the four-tab dialog: General / Daily / Weekly / Monthly). This is a MAP, not a
plan: it says what `ibJobSchedule` holds today, what the dialog would need, and which gaps are
deliberate. The runtime semantics live in `session-registry.md` and in `jobSchedule.h` itself.

> **Design rule, restated because everything below depends on it.** A schedule answers TWO
> independent questions: does the CALENDAR allow this moment, and has enough time passed since the
> last run. A job is due when both say yes. That composition is what makes "every Monday at 02:00"
> expressible without a cron grammar — and what makes the parameter list short.

---

## The due moment

Since 2026-08-03 the manager does not ask "does this instant qualify". It computes the DUE MOMENT:

```
countFrom = (ever ran ? lastRun : registeredAt) + interval        // first run skips the interval
dueAt     = schedule.NextAllowedAfter(countFrom)                  // calendar moves it forward
due       = dueAt <= now
```

Two consequences, both intended:

- **A time of day reads as NOT BEFORE, never as ONLY AT.** Miss 02:00 because the machine was off,
  start at 07:00 — the job runs at 07:00, late. The alternative (only inside the window) is how a
  missed night silently becomes a skipped week.
- **A first run with no history skips the interval.** Otherwise a weekly job could never start: no
  desktop client lives a week, so the first gap never elapses. What keeps a plain cadence from
  firing on every launch is that it has no calendar to point at, so its interval from registration
  is the whole answer.

---

## What the structure holds today (`ibJobSchedule`)

| Field | Meaning | Dialog tab |
|---|---|---|
| `m_intervalSeconds` | minimum gap between two runs | General — "repeat every" |
| `m_startMinute` / `m_endMinute` | time-of-day window `[start, end)`, minutes from midnight; `-1` disables; start > end wraps midnight | Daily — start / end time |
| `m_daysOfWeek` | week-day bit mask (`ibJobWeekDay_Any` = every day) | Weekly — day checkboxes |
| `m_daysOfMonth` | day-of-month bit mask, bit 0 = the 1st | Monthly — "on day N" |
| `m_months` | month bit mask, bit 0 = January | Monthly — month checkboxes |
| `m_activeFrom` / `m_activeTo` | validity range; invalid = unbounded | General — start / end date |

Helpers: `EverySeconds(n)`, `Nightly(startHour, endHour)`, `AtTime(h, m)`, plus `IsAllowed(moment)`,
`NextAllowedAfter(notBefore)`, `IsValid()` and `ToString()` (a localised sentence built from the same
fields an editor would show, so a settings list and a log line cannot disagree).

---

## What the dialog has and we do not

Ordered by how much each would actually buy:

1. **Repeat WITHIN the day** — "repeat every N seconds until the end time", with an optional pause
   and a stop-after. Today a job runs once per due moment; a schedule that fires every 10 minutes
   between 09:00 and 18:00 is expressible only by making the interval 10 minutes and the window
   09:00–18:00, which is in fact the same thing — so the gap here is presentation, not power.
   ⚠ The one genuinely missing piece is **stop-after** ("finish after 18:30 even mid-pass").
2. **"Every N weeks" / "every N months"** — the current masks say WHICH days, not HOW OFTEN. Every
   other Monday needs either a phase (anchor + period) or a longer interval doing the work; today it
   is the interval, which drifts when a run is late.
3. **Nth weekday of the month** ("second Tuesday", "last Friday") — the monthly tab's second row.
   Needs an ordinal + direction (from the start / from the end) beside the weekday mask.
4. **Day-of-month counted from the END** ("the last day", "3 days before the end") — the direction
   selector on the monthly tab. The current mask is absolute days only.
5. **Named detailed day plans** — the list at the bottom of the Daily tab, i.e. several windows in
   one day. Today one window; two would need a small vector of `[start, end)` pairs.

Deliberately NOT on the list: seconds-level precision (the finest thing a schedule names is a
minute) and per-run history (`sys_job` holds one fact per job — when it last ran — and that is what
must be shared; the rest are per-process observations).

---

## Storage

`sys_job` — one row per job name: `jobName` (PK), `lastRun`, `computer`. The shared clock across
every process on the base, so N clients do not each run the job once per interval. It holds NO
schedule: a platform job declares its own in code, and a configuration's job will carry it in
metadata. Whatever the editor above eventually writes goes there, not into `sys_job`.

## Related

- `session-registry.md` — the job session, the `Job.<name>` claim, the honest remainder for
  scheduled jobs.
- `jobSchedule.h` — the field-level contract and the two-question rule.
