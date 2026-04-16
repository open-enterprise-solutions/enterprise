/**
 * RegisterView — unified viewer for Information, Accumulation and Accounting registers.
 *
 * Features:
 *   - PeriodSelector (from / to dates)
 *   - For AccumulationRegister: Balance / Turnovers toggle
 *   - For AccountingRegister: Balance / Turnovers / DrCrTurnovers toggle
 *   - Dimension filters (string inputs per dimension from metadata)
 *   - Table display via Refine useList hook
 *   - Pagination
 *   - Export CSV
 */

import { useState, useCallback, useMemo, useEffect } from "react"
import { useList } from "@refinedev/core"
import {
  Funnel,
  DownloadSimple,
  ArrowsClockwise,
} from "@phosphor-icons/react"
import { PeriodSelector, type DateRange } from "./PeriodSelector"
import { PrintButton } from "./PrintTemplate"

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

export type RegisterType =
  | "information"
  | "accumulation"
  | "accounting"

export type AccumulationMode = "balance" | "turnovers"
export type AccountingMode = "balance" | "turnovers" | "drCrTurnovers"

export interface RegisterDimension {
  key: string
  label: string
  type?: "string" | "number" | "ref"
}

export interface RegisterResource {
  key: string
  label: string
  type?: "number"
}

export interface RegisterViewProps {
  resource: string // Refine resource id, e.g. "accumulationRegisters/stock"
  registerType: RegisterType
  dimensions?: RegisterDimension[]
  resources?: RegisterResource[]
  /** Extra columns to show (attributes for information registers) */
  extraColumns?: { key: string; label: string; type?: string }[]
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

function formatCellValue(value: unknown, type?: string): string {
  if (value == null || value === "") return "—"
  if (type === "number" && typeof value === "number") {
    return new Intl.NumberFormat(undefined, { maximumFractionDigits: 4 }).format(value)
  }
  if (type === "date" && typeof value === "string") {
    try {
      return new Intl.DateTimeFormat(undefined, {
        year: "numeric",
        month: "2-digit",
        day: "2-digit",
      }).format(new Date(value))
    } catch {
      return String(value)
    }
  }
  return String(value)
}

function exportCSV(
  columns: { key: string; label: string; type?: string }[],
  rows: Record<string, unknown>[],
  filename: string,
) {
  const escape = (v: unknown) => {
    const str = String(v ?? "")
    if (str.includes(",") || str.includes('"') || str.includes("\n")) {
      return `"${str.replace(/"/g, '""')}"`
    }
    return str
  }
  const header = columns.map((c) => escape(c.label)).join(",")
  const body = rows.map((row) =>
    columns.map((c) => escape(row[c.key] ?? "")).join(","),
  )
  const csv = [header, ...body].join("\n")
  const blob = new Blob(["\ufeff" + csv], { type: "text/csv;charset=utf-8;" })
  const url = URL.createObjectURL(blob)
  const a = document.createElement("a")
  a.href = url
  a.download = filename
  a.click()
  URL.revokeObjectURL(url)
}

function getToday(): DateRange {
  const now = new Date()
  const firstOfMonth = new Date(now.getFullYear(), now.getMonth(), 1)
    .toISOString()
    .slice(0, 10)
  return { from: firstOfMonth, to: now.toISOString().slice(0, 10) }
}

// ---------------------------------------------------------------------------
// Mode toggle
// ---------------------------------------------------------------------------

function ModeToggle<T extends string>({
  options,
  value,
  onChange,
}: {
  options: { id: T; label: string }[]
  value: T
  onChange: (v: T) => void
}) {
  return (
    <div
      role="group"
      aria-label="Register mode"
      className="flex items-center rounded-[var(--radius)] border border-[hsl(var(--border))] overflow-hidden"
    >
      {options.map((opt) => (
        <button
          key={opt.id}
          type="button"
          onClick={() => onChange(opt.id)}
          className={[
            "h-7 px-3 text-[11px] font-medium transition-colors border-r border-[hsl(var(--border))] last:border-r-0 whitespace-nowrap",
            value === opt.id
              ? "bg-[hsl(var(--primary))] text-[hsl(var(--primary-foreground))]"
              : "bg-[hsl(var(--background))] text-[hsl(var(--muted-foreground))] hover:bg-[hsl(var(--secondary))] hover:text-[hsl(var(--foreground))]",
          ].join(" ")}
        >
          {opt.label}
        </button>
      ))}
    </div>
  )
}

// ---------------------------------------------------------------------------
// Dimension filter row
// ---------------------------------------------------------------------------

function DimensionFilters({
  dimensions,
  values,
  onChange,
}: {
  dimensions: RegisterDimension[]
  values: Record<string, string>
  onChange: (key: string, val: string) => void
}) {
  if (dimensions.length === 0) return null

  return (
    <div className="flex flex-wrap items-end gap-3 px-4 py-2 border-b border-[hsl(var(--border))] bg-[hsl(var(--secondary)/0.2)]">
      <span className="flex items-center gap-1 text-[11px] text-[hsl(var(--muted-foreground))] shrink-0">
        <Funnel size={12} weight="duotone" />
        Filters:
      </span>
      {dimensions.map((dim) => (
        <div key={dim.key} className="flex flex-col gap-0.5">
          <label
            htmlFor={`dim-${dim.key}`}
            className="text-[10px] text-[hsl(var(--muted-foreground))]"
          >
            {dim.label}
          </label>
          <input
            id={`dim-${dim.key}`}
            type="text"
            value={values[dim.key] ?? ""}
            onChange={(e) => onChange(dim.key, e.target.value)}
            placeholder={`All ${dim.label}`}
            className="h-7 rounded-[var(--radius)] border border-[hsl(var(--input))] bg-[hsl(var(--background))] px-2 text-[12px] text-[hsl(var(--foreground))] outline-none focus:ring-1 focus:ring-[hsl(var(--ring))] min-w-[120px]"
          />
        </div>
      ))}
    </div>
  )
}

// ---------------------------------------------------------------------------
// Table skeleton
// ---------------------------------------------------------------------------

function TableSkeleton({ cols }: { cols: number }) {
  return (
    <>
      {Array.from({ length: 8 }).map((_, i) => (
        <tr key={i} aria-hidden="true">
          {Array.from({ length: cols }).map((_, j) => (
            <td key={j} style={{ padding: "4px 8px", borderBottom: "1px solid hsl(var(--border))" }}>
              <div
                className="skeleton"
                style={{ height: 13, width: `${45 + ((i * 7 + j * 13) % 45)}%`, borderRadius: "var(--radius)" }}
              />
            </td>
          ))}
        </tr>
      ))}
    </>
  )
}

// ---------------------------------------------------------------------------
// RegisterView — main component
// ---------------------------------------------------------------------------

export function RegisterView({
  resource,
  registerType,
  dimensions = [],
  resources: resourceColumns = [],
  extraColumns = [],
}: RegisterViewProps) {
  const [period, setPeriod] = useState<DateRange>(getToday)
  const [accMode, setAccMode] = useState<AccumulationMode>("balance")
  const [acctMode, setAcctMode] = useState<AccountingMode>("balance")
  const [dimFilters, setDimFilters] = useState<Record<string, string>>({})
  const [page, setPage] = useState(1)
  const pageSize = 50

  // Reset page when period/mode changes
  useEffect(() => { setPage(1) }, [period, accMode, acctMode, dimFilters])

  const handleDimChange = useCallback((key: string, val: string) => {
    setDimFilters((prev) => ({ ...prev, [key]: val }))
  }, [])

  // Build query resource path including mode suffix
  const queryResource = useMemo(() => {
    if (registerType === "accumulation") {
      return `${resource}/${accMode}`
    }
    if (registerType === "accounting") {
      return `${resource}/${acctMode}`
    }
    return resource
  }, [resource, registerType, accMode, acctMode])

  // Build Refine filters
  const filters = useMemo(() => {
    const f: { field: string; operator: "eq" | "gte" | "lte"; value: unknown }[] = [
      { field: "period_from", operator: "gte", value: period.from },
      { field: "period_to",   operator: "lte", value: period.to },
    ]
    for (const [key, val] of Object.entries(dimFilters)) {
      if (val.trim()) {
        f.push({ field: key, operator: "eq", value: val.trim() })
      }
    }
    return f
  }, [period, dimFilters])

  const listResult = useList({
    resource: queryResource,
    pagination: { currentPage: page, pageSize },
    filters,
  })

  const isLoading = listResult.query?.isLoading ?? false
  const isError = listResult.query?.isError ?? false
  const rows = (listResult.result?.data ?? []) as Record<string, unknown>[]
  const total = listResult.result?.total ?? 0
  const pageCount = Math.max(1, Math.ceil(total / pageSize))

  // Build column list
  const allColumns = useMemo(() => {
    const cols: { key: string; label: string; type?: string }[] = []

    // Period column for non-information registers
    if (registerType !== "information") {
      cols.push({ key: "period", label: "Period", type: "date" })
    }

    // Dimension columns
    for (const dim of dimensions) {
      cols.push({ key: dim.key, label: dim.label, type: dim.type ?? "string" })
    }

    // Resource (measure) columns
    for (const res of resourceColumns) {
      cols.push({ key: res.key, label: res.label, type: "number" })
    }

    // Extra attribute columns
    for (const ex of extraColumns) {
      cols.push({ key: ex.key, label: ex.label, type: ex.type })
    }

    // For DrCrTurnovers: show Debit/Credit columns
    if (registerType === "accounting" && acctMode === "drCrTurnovers") {
      // Replace resource columns with Dr/Cr pairs
      for (const res of resourceColumns) {
        cols.push({ key: `${res.key}_dr`, label: `${res.label} Dr`, type: "number" })
        cols.push({ key: `${res.key}_cr`, label: `${res.label} Cr`, type: "number" })
      }
    }

    return cols
  }, [dimensions, resourceColumns, extraColumns, registerType, acctMode])

  const handleExport = useCallback(() => {
    exportCSV(allColumns, rows, `${resource.replace(/\//g, "_")}.csv`)
  }, [allColumns, rows, resource])

  // ---------------------------------------------------------------------------

  return (
    <div className="flex flex-col h-full bg-[hsl(var(--background))]">
      {/* Toolbar */}
      <div
        className="flex shrink-0 flex-wrap items-center gap-3 border-b border-[hsl(var(--border))] bg-[hsl(var(--secondary)/0.4)] px-4 py-2"
        data-no-print=""
      >
        {/* Period selector */}
        <PeriodSelector value={period} onChange={setPeriod} />

        {/* Mode toggles */}
        {registerType === "accumulation" && (
          <ModeToggle<AccumulationMode>
            options={[
              { id: "balance",   label: "Balance"   },
              { id: "turnovers", label: "Turnovers" },
            ]}
            value={accMode}
            onChange={setAccMode}
          />
        )}

        {registerType === "accounting" && (
          <ModeToggle<AccountingMode>
            options={[
              { id: "balance",       label: "Balance"          },
              { id: "turnovers",     label: "Turnovers"        },
              { id: "drCrTurnovers", label: "Dr / Cr Turnovers"},
            ]}
            value={acctMode}
            onChange={setAcctMode}
          />
        )}

        <div className="flex-1" />

        {/* Refresh / export / print */}
        <button
          type="button"
          onClick={() => listResult.query?.refetch()}
          className="flex items-center gap-1.5 h-7 px-2.5 rounded-[var(--radius)] border border-[hsl(var(--border))] bg-[hsl(var(--background))] text-[12px] text-[hsl(var(--foreground))] hover:bg-[hsl(var(--secondary))] transition-colors"
          title="Refresh"
        >
          <ArrowsClockwise size={13} weight="duotone" />
        </button>

        <button
          type="button"
          onClick={handleExport}
          disabled={rows.length === 0}
          className="flex items-center gap-1.5 h-7 px-2.5 rounded-[var(--radius)] border border-[hsl(var(--border))] bg-[hsl(var(--background))] text-[12px] text-[hsl(var(--foreground))] hover:bg-[hsl(var(--secondary))] transition-colors disabled:opacity-40"
          title="Export CSV"
        >
          <DownloadSimple size={13} weight="duotone" />
          <span>CSV</span>
        </button>

        <PrintButton />
      </div>

      {/* Dimension filters */}
      <DimensionFilters
        dimensions={dimensions}
        values={dimFilters}
        onChange={handleDimChange}
      />

      {/* Error */}
      {isError && (
        <div className="flex items-center justify-center flex-1 text-[12px] text-[hsl(var(--destructive))]">
          Failed to load register data. Check your connection.
        </div>
      )}

      {/* Table */}
      {!isError && (
        <div className="flex-1 overflow-auto">
          <table className="w-full border-collapse" style={{ fontSize: 12 }}>
            <thead className="sticky top-0 z-10 bg-[hsl(var(--secondary))]">
              <tr>
                {allColumns.map((col) => (
                  <th
                    key={col.key}
                    style={{
                      padding: "5px 8px",
                      fontWeight: 500,
                      fontSize: 11,
                      color: "hsl(var(--muted-foreground))",
                      textAlign: col.type === "number" ? "right" : "left",
                      whiteSpace: "nowrap",
                      userSelect: "none",
                      borderBottom: "1px solid hsl(var(--border))",
                      borderRight: "1px solid hsl(var(--border))",
                    }}
                    className="last:border-r-0"
                  >
                    {col.label}
                  </th>
                ))}
              </tr>
            </thead>

            <tbody>
              {isLoading ? (
                <TableSkeleton cols={allColumns.length} />
              ) : rows.length === 0 ? (
                <tr>
                  <td
                    colSpan={allColumns.length}
                    className="px-4 py-10 text-center text-[12px] text-[hsl(var(--muted-foreground))]"
                  >
                    No records found for the selected period.
                  </td>
                </tr>
              ) : (
                rows.map((row, idx) => (
                  <tr
                    key={idx}
                    className={
                      idx % 2 === 0
                        ? "bg-[hsl(var(--background))]"
                        : "bg-[hsl(var(--secondary)/0.3)]"
                    }
                  >
                    {allColumns.map((col) => {
                      const isNumeric = col.type === "number"
                      return (
                        <td
                          key={col.key}
                          style={{
                            padding: "3px 8px",
                            textAlign: isNumeric ? "right" : "left",
                            borderBottom: "1px solid hsl(var(--border))",
                            borderRight: "1px solid hsl(var(--border))",
                            color: "hsl(var(--foreground))",
                            whiteSpace: "nowrap",
                          }}
                          className="last:border-r-0"
                        >
                          {isNumeric ? (
                            <span className="tabular-nums">
                              {formatCellValue(row[col.key], col.type)}
                            </span>
                          ) : (
                            formatCellValue(row[col.key], col.type)
                          )}
                        </td>
                      )
                    })}
                  </tr>
                ))
              )}
            </tbody>

            {/* Totals for numeric columns */}
            {rows.length > 0 && (
              <tfoot>
                <tr
                  className="border-t-2 border-[hsl(var(--border))] bg-[hsl(var(--secondary)/0.5)] font-semibold"
                  style={{ fontSize: 12 }}
                >
                  {allColumns.map((col, i) => {
                    const isNumeric = col.type === "number"
                    const sum = isNumeric
                      ? rows.reduce((acc, row) => acc + (typeof row[col.key] === "number" ? (row[col.key] as number) : 0), 0)
                      : null

                    return (
                      <td
                        key={col.key}
                        style={{
                          padding: "4px 8px",
                          textAlign: isNumeric ? "right" : "left",
                          borderRight: "1px solid hsl(var(--border))",
                          color: "hsl(var(--foreground))",
                        }}
                        className="last:border-r-0"
                      >
                        {i === 0 && !isNumeric ? (
                          <span className="text-[hsl(var(--muted-foreground))]">Total</span>
                        ) : isNumeric && sum !== null ? (
                          <span className="tabular-nums">
                            {new Intl.NumberFormat(undefined, { maximumFractionDigits: 4 }).format(sum)}
                          </span>
                        ) : null}
                      </td>
                    )
                  })}
                </tr>
              </tfoot>
            )}
          </table>
        </div>
      )}

      {/* Pagination footer */}
      {!isError && total > 0 && (
        <div
          className="flex shrink-0 items-center gap-3 border-t border-[hsl(var(--border))] bg-[hsl(var(--secondary)/0.3)] px-4 py-2 text-[11px] text-[hsl(var(--muted-foreground))]"
          data-no-print=""
        >
          <span className="tabular-nums">
            {(page - 1) * pageSize + 1}–{Math.min(page * pageSize, total)} of {total}
          </span>
          <div className="flex-1" />
          <div className="flex items-center gap-1">
            <button
              type="button"
              disabled={page <= 1}
              onClick={() => setPage(page - 1)}
              className="flex h-6 w-6 items-center justify-center rounded-[var(--radius)] border border-[hsl(var(--border))] bg-[hsl(var(--background))] hover:bg-[hsl(var(--secondary))] disabled:opacity-40 disabled:cursor-not-allowed text-[11px] transition-colors"
            >
              ‹
            </button>
            <span className="px-2 tabular-nums">
              {page} / {pageCount}
            </span>
            <button
              type="button"
              disabled={page >= pageCount}
              onClick={() => setPage(page + 1)}
              className="flex h-6 w-6 items-center justify-center rounded-[var(--radius)] border border-[hsl(var(--border))] bg-[hsl(var(--background))] hover:bg-[hsl(var(--secondary))] disabled:opacity-40 disabled:cursor-not-allowed text-[11px] transition-colors"
            >
              ›
            </button>
          </div>
        </div>
      )}
    </div>
  )
}
