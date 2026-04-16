/**
 * PeriodSelector — compact inline date-range picker with quick-select presets.
 *
 * Provides From/To date inputs plus one-click preset buttons:
 *   Today, This Week, This Month, This Quarter, This Year, Custom
 *
 * Designed to be embedded in RegisterView and ReportViewer toolbars.
 */

import { useState, useCallback } from "react"
import { CalendarBlank, CaretDown } from "@phosphor-icons/react"

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

export interface DateRange {
  from: string // ISO date string YYYY-MM-DD
  to: string
}

export type PeriodPreset =
  | "today"
  | "this-week"
  | "this-month"
  | "this-quarter"
  | "this-year"
  | "custom"

export interface PeriodSelectorProps {
  value: DateRange
  onChange: (range: DateRange) => void
  className?: string
}

// ---------------------------------------------------------------------------
// Preset helpers
// ---------------------------------------------------------------------------

function toISODate(d: Date): string {
  return d.toISOString().slice(0, 10)
}

function getRangeForPreset(preset: Exclude<PeriodPreset, "custom">): DateRange {
  const now = new Date()
  const year = now.getFullYear()
  const month = now.getMonth()

  switch (preset) {
    case "today": {
      const today = toISODate(now)
      return { from: today, to: today }
    }
    case "this-week": {
      const dow = now.getDay() // 0=Sun
      const monday = new Date(now)
      monday.setDate(now.getDate() - ((dow + 6) % 7))
      const sunday = new Date(monday)
      sunday.setDate(monday.getDate() + 6)
      return { from: toISODate(monday), to: toISODate(sunday) }
    }
    case "this-month": {
      const first = new Date(year, month, 1)
      const last = new Date(year, month + 1, 0)
      return { from: toISODate(first), to: toISODate(last) }
    }
    case "this-quarter": {
      const q = Math.floor(month / 3)
      const first = new Date(year, q * 3, 1)
      const last = new Date(year, q * 3 + 3, 0)
      return { from: toISODate(first), to: toISODate(last) }
    }
    case "this-year": {
      return {
        from: `${year}-01-01`,
        to: `${year}-12-31`,
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Preset label map
// ---------------------------------------------------------------------------

const PRESETS: { id: PeriodPreset; label: string }[] = [
  { id: "today",        label: "Today"     },
  { id: "this-week",    label: "This Week" },
  { id: "this-month",   label: "This Month"},
  { id: "this-quarter", label: "This Quarter" },
  { id: "this-year",    label: "This Year" },
  { id: "custom",       label: "Custom"    },
]

// ---------------------------------------------------------------------------
// Component
// ---------------------------------------------------------------------------

export function PeriodSelector({ value, onChange, className }: PeriodSelectorProps) {
  const [activePreset, setActivePreset] = useState<PeriodPreset>("this-month")

  const handlePreset = useCallback(
    (preset: PeriodPreset) => {
      setActivePreset(preset)
      if (preset !== "custom") {
        onChange(getRangeForPreset(preset))
      }
    },
    [onChange],
  )

  const handleFromChange = useCallback(
    (e: React.ChangeEvent<HTMLInputElement>) => {
      setActivePreset("custom")
      onChange({ ...value, from: e.target.value })
    },
    [value, onChange],
  )

  const handleToChange = useCallback(
    (e: React.ChangeEvent<HTMLInputElement>) => {
      setActivePreset("custom")
      onChange({ ...value, to: e.target.value })
    },
    [value, onChange],
  )

  return (
    <div
      className={[
        "flex items-center gap-2 flex-wrap",
        className ?? "",
      ].join(" ")}
      role="group"
      aria-label="Period selector"
    >
      {/* Quick preset buttons */}
      <div className="flex items-center rounded-[var(--radius)] border border-[hsl(var(--border))] overflow-hidden shrink-0">
        {PRESETS.map(({ id, label }) => (
          <button
            key={id}
            type="button"
            onClick={() => handlePreset(id)}
            className={[
              "h-7 px-2.5 text-[11px] font-medium transition-colors border-r border-[hsl(var(--border))] last:border-r-0 whitespace-nowrap",
              activePreset === id
                ? "bg-[hsl(var(--primary))] text-[hsl(var(--primary-foreground))]"
                : "bg-[hsl(var(--background))] text-[hsl(var(--muted-foreground))] hover:bg-[hsl(var(--secondary))] hover:text-[hsl(var(--foreground))]",
            ].join(" ")}
          >
            {id === "custom"
              ? <span className="flex items-center gap-1">{label} <CaretDown size={10} /></span>
              : label
            }
          </button>
        ))}
      </div>

      {/* Date range inputs */}
      <div className="flex items-center gap-1.5 shrink-0">
        <CalendarBlank
          size={13}
          weight="duotone"
          className="text-[hsl(var(--muted-foreground))] shrink-0"
        />
        <input
          type="date"
          value={value.from}
          onChange={handleFromChange}
          aria-label="Period from"
          className="h-7 rounded-[var(--radius)] border border-[hsl(var(--input))] bg-[hsl(var(--background))] px-2 text-[12px] text-[hsl(var(--foreground))] outline-none focus:ring-1 focus:ring-[hsl(var(--ring))]"
        />
        <span className="text-[11px] text-[hsl(var(--muted-foreground))]">—</span>
        <input
          type="date"
          value={value.to}
          onChange={handleToChange}
          aria-label="Period to"
          className="h-7 rounded-[var(--radius)] border border-[hsl(var(--input))] bg-[hsl(var(--background))] px-2 text-[12px] text-[hsl(var(--foreground))] outline-none focus:ring-1 focus:ring-[hsl(var(--ring))]"
        />
      </div>
    </div>
  )
}
