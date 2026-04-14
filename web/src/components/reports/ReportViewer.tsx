/**
 * ReportViewer — metadata-driven report renderer.
 *
 * Layout:
 *   1. Parameter panel (rendered from reportMeta.parameters)
 *   2. "Generate" button
 *   3. DataView table with grouping support and totals row
 *   4. Toolbar: CSV export, Excel (CSV), Print
 *
 * Grouping: rows with the same value in the first column are collapsible.
 * Totals: sum of all numeric columns shown in a pinned footer row.
 * CSV export: pure client-side, no dependencies.
 */

import { useState, useCallback, useMemo, useRef } from "react"
import {
  Play,
  DownloadSimple,
  CaretRight,
  CaretDown,
  Warning,
} from "@phosphor-icons/react"
import { PrintButton } from "./PrintTemplate"

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

export interface ReportParameter {
  key: string
  label: string
  type: "string" | "number" | "date" | "boolean" | "ref"
  required?: boolean
  defaultValue?: unknown
}

export interface ReportColumn {
  key: string
  title: string
  type?: "string" | "number" | "date" | "boolean"
  groupBy?: boolean // first groupBy column drives row grouping
}

export interface ReportViewerProps {
  title: string
  parameters?: ReportParameter[]
  columns: ReportColumn[]
  onGenerate: (params: Record<string, unknown>) => Promise<Record<string, unknown>[]>
  loading?: boolean
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

function formatValue(value: unknown, type?: string): string {
  if (value == null || value === "") return ""
  if (type === "number" && typeof value === "number") {
    return new Intl.NumberFormat(undefined, { maximumFractionDigits: 2 }).format(value)
  }
  if (type === "date" && typeof value === "string") {
    try {
      return new Intl.DateTimeFormat(undefined, {
        year: "numeric", month: "2-digit", day: "2-digit",
      }).format(new Date(value))
    } catch {
      return String(value)
    }
  }
  return String(value)
}

// CSV export — no external deps
function exportCSV(
  columns: ReportColumn[],
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

  const header = columns.map((c) => escape(c.title)).join(",")
  const body = rows.map((row) =>
    columns.map((c) => escape(row[c.key] ?? "")).join(","),
  )
  const csv = [header, ...body].join("\n")

  const blob = new Blob(["\ufeff" + csv], { type: "text/csv;charset=utf-8;" })
  const url = URL.createObjectURL(blob)
  const link = document.createElement("a")
  link.href = url
  link.download = filename
  link.click()
  URL.revokeObjectURL(url)
}

// ---------------------------------------------------------------------------
// Parameter panel
// ---------------------------------------------------------------------------

function ParameterPanel({
  parameters,
  values,
  onChange,
}: {
  parameters: ReportParameter[]
  values: Record<string, unknown>
  onChange: (key: string, value: unknown) => void
}) {
  if (parameters.length === 0) return null

  return (
    <div
      className="flex flex-wrap items-end gap-3 border-b border-[hsl(var(--border))] bg-[hsl(var(--secondary)/0.3)] px-4 py-3"
      data-no-print=""
    >
      {parameters.map((param) => (
        <div key={param.key} className="flex flex-col gap-1">
          <label
            htmlFor={`param-${param.key}`}
            className="text-[11px] font-medium text-[hsl(var(--muted-foreground))]"
          >
            {param.label}
            {param.required && (
              <span className="ml-0.5 text-[hsl(var(--destructive))]">*</span>
            )}
          </label>

          {param.type === "boolean" ? (
            <input
              id={`param-${param.key}`}
              type="checkbox"
              checked={Boolean(values[param.key])}
              onChange={(e) => onChange(param.key, e.target.checked)}
              className="h-4 w-4 rounded-[var(--radius)] border border-[hsl(var(--input))] accent-[hsl(var(--primary))]"
            />
          ) : (
            <input
              id={`param-${param.key}`}
              type={param.type === "number" ? "number" : param.type === "date" ? "date" : "text"}
              value={String(values[param.key] ?? "")}
              onChange={(e) => onChange(param.key, e.target.value)}
              placeholder={param.label}
              className="h-7 rounded-[var(--radius)] border border-[hsl(var(--input))] bg-[hsl(var(--background))] px-2 text-[12px] text-[hsl(var(--foreground))] outline-none focus:ring-1 focus:ring-[hsl(var(--ring))] min-w-[140px]"
            />
          )}
        </div>
      ))}
    </div>
  )
}

// ---------------------------------------------------------------------------
// Totals row
// ---------------------------------------------------------------------------

function TotalsRow({
  columns,
  rows,
  hasGroupCol,
}: {
  columns: ReportColumn[]
  rows: Record<string, unknown>[]
  hasGroupCol: boolean
}) {
  return (
    <tfoot>
      <tr
        className="border-t-2 border-[hsl(var(--border))] bg-[hsl(var(--secondary)/0.5)] font-semibold"
        style={{ fontSize: 12 }}
      >
        {hasGroupCol && <td style={{ width: 24, padding: "4px 8px" }} />}
        {columns.map((col, i) => {
          const isNumeric = col.type === "number"
          const isFirst = i === 0

          let cell: React.ReactNode = isFirst ? "Total" : null

          if (isNumeric) {
            const sum = rows.reduce((acc, row) => {
              const v = row[col.key]
              return acc + (typeof v === "number" ? v : 0)
            }, 0)
            cell = (
              <span className="tabular-nums">
                {new Intl.NumberFormat(undefined, { maximumFractionDigits: 2 }).format(sum)}
              </span>
            )
          }

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
              {cell}
            </td>
          )
        })}
      </tr>
    </tfoot>
  )
}

// ---------------------------------------------------------------------------
// Grouped row group
// ---------------------------------------------------------------------------

interface GroupedRowsProps {
  groupKey: string
  groupValue: string
  rows: Record<string, unknown>[]
  columns: ReportColumn[]
  dataColumns: ReportColumn[]
}

function GroupedRows({ groupValue, rows, columns, dataColumns }: GroupedRowsProps) {
  const [expanded, setExpanded] = useState(true)

  const groupSum: Record<string, number> = {}
  for (const col of dataColumns) {
    groupSum[col.key] = rows.reduce((acc, row) => {
      const v = row[col.key]
      return acc + (typeof v === "number" ? v : 0)
    }, 0)
  }

  return (
    <>
      {/* Group header row */}
      <tr
        className="cursor-pointer bg-[hsl(var(--secondary)/0.6)] hover:bg-[hsl(var(--secondary))] transition-colors"
        onClick={() => setExpanded((e) => !e)}
      >
        {/* Expand toggle cell */}
        <td style={{ width: 24, padding: "3px 6px", borderBottom: "1px solid hsl(var(--border))" }}>
          {expanded
            ? <CaretDown size={11} weight="bold" className="text-[hsl(var(--muted-foreground))]" />
            : <CaretRight size={11} weight="bold" className="text-[hsl(var(--muted-foreground))]" />
          }
        </td>
        {columns.map((col, i) => {
          const isNumeric = col.type === "number"
          const cell = i === 0
            ? <span className="font-medium text-[hsl(var(--foreground))]">{groupValue}</span>
            : isNumeric
              ? (
                <span className="tabular-nums text-[hsl(var(--muted-foreground))] text-[11px]">
                  {new Intl.NumberFormat(undefined, { maximumFractionDigits: 2 }).format(groupSum[col.key] ?? 0)}
                </span>
              )
              : null

          return (
            <td
              key={col.key}
              style={{
                padding: "4px 8px",
                fontSize: 12,
                textAlign: isNumeric ? "right" : "left",
                borderBottom: "1px solid hsl(var(--border))",
                borderRight: "1px solid hsl(var(--border))",
              }}
              className="last:border-r-0"
            >
              {cell}
            </td>
          )
        })}
      </tr>

      {/* Data rows */}
      {expanded &&
        rows.map((row, idx) => (
          <tr
            key={idx}
            className={[
              "transition-colors",
              idx % 2 === 0
                ? "bg-[hsl(var(--background))]"
                : "bg-[hsl(var(--secondary)/0.2)]",
              "hover:bg-[hsl(var(--primary)/0.05)]",
            ].join(" ")}
          >
            <td style={{ width: 24, borderBottom: "1px solid hsl(var(--border))" }} />
            {columns.map((col) => {
              const isNumeric = col.type === "number"
              return (
                <td
                  key={col.key}
                  style={{
                    padding: "3px 8px",
                    fontSize: 12,
                    textAlign: isNumeric ? "right" : "left",
                    borderBottom: "1px solid hsl(var(--border))",
                    borderRight: "1px solid hsl(var(--border))",
                    color: "hsl(var(--foreground))",
                    whiteSpace: "nowrap",
                    overflow: "hidden",
                    textOverflow: "ellipsis",
                  }}
                  className="last:border-r-0"
                >
                  {isNumeric ? (
                    <span className="tabular-nums">{formatValue(row[col.key], col.type)}</span>
                  ) : (
                    formatValue(row[col.key], col.type)
                  )}
                </td>
              )
            })}
          </tr>
        ))}
    </>
  )
}

// ---------------------------------------------------------------------------
// ReportViewer
// ---------------------------------------------------------------------------

export function ReportViewer({
  title,
  parameters = [],
  columns,
  onGenerate,
}: ReportViewerProps) {
  const [paramValues, setParamValues] = useState<Record<string, unknown>>(() => {
    const init: Record<string, unknown> = {}
    for (const p of parameters) {
      init[p.key] = p.defaultValue ?? ""
    }
    return init
  })

  const [rows, setRows] = useState<Record<string, unknown>[]>([])
  const [generated, setGenerated] = useState(false)
  const [generating, setGenerating] = useState(false)
  const [error, setError] = useState<string | null>(null)
  const containerRef = useRef<HTMLDivElement>(null)

  const handleParamChange = useCallback((key: string, value: unknown) => {
    setParamValues((prev) => ({ ...prev, [key]: value }))
  }, [])

  const handleGenerate = useCallback(async () => {
    setGenerating(true)
    setError(null)
    try {
      const result = await onGenerate(paramValues)
      setRows(result)
      setGenerated(true)
    } catch (err) {
      setError(err instanceof Error ? err.message : "Failed to generate report")
    } finally {
      setGenerating(false)
    }
  }, [onGenerate, paramValues])

  // Determine grouping column (first with groupBy=true, else no grouping)
  const groupByCol = columns.find((c) => c.groupBy)
  const hasGrouping = Boolean(groupByCol)
  const numericCols = columns.filter((c) => c.type === "number")

  // Group rows by the groupBy column value
  const groupedRows = useMemo<Map<string, Record<string, unknown>[]>>(() => {
    if (!groupByCol) return new Map()
    const map = new Map<string, Record<string, unknown>[]>()
    for (const row of rows) {
      const key = String(row[groupByCol.key] ?? "")
      const list = map.get(key) ?? []
      list.push(row)
      map.set(key, list)
    }
    return map
  }, [rows, groupByCol])

  const handleExportCSV = useCallback(() => {
    exportCSV(columns, rows, `${title.replace(/\s+/g, "_")}.csv`)
  }, [columns, rows, title])

  // ---------------------------------------------------------------------------

  return (
    <div ref={containerRef} className="flex flex-col h-full bg-[hsl(var(--background))]">
      {/* Parameter panel */}
      <ParameterPanel
        parameters={parameters}
        values={paramValues}
        onChange={handleParamChange}
      />

      {/* Action toolbar */}
      <div
        className="flex shrink-0 items-center gap-2 border-b border-[hsl(var(--border))] bg-[hsl(var(--secondary)/0.4)] px-4 py-2"
        data-no-print=""
      >
        <button
          type="button"
          onClick={handleGenerate}
          disabled={generating}
          className="flex items-center gap-1.5 h-8 px-3 rounded-[var(--radius)] border border-[hsl(var(--primary))] bg-[hsl(var(--primary))] text-[hsl(var(--primary-foreground))] text-[12px] font-medium hover:opacity-90 transition-opacity disabled:opacity-50 disabled:cursor-not-allowed"
        >
          <Play size={13} weight="duotone" />
          <span>{generating ? "Generating…" : "Generate"}</span>
        </button>

        {generated && rows.length > 0 && (
          <>
            <div className="flex-1" />
            <span className="text-[11px] text-[hsl(var(--muted-foreground))] tabular-nums">
              {rows.length} row{rows.length !== 1 ? "s" : ""}
            </span>

            <button
              type="button"
              onClick={handleExportCSV}
              className="flex items-center gap-1.5 h-7 px-2.5 rounded-[var(--radius)] border border-[hsl(var(--border))] bg-[hsl(var(--background))] text-[12px] text-[hsl(var(--foreground))] hover:bg-[hsl(var(--secondary))] transition-colors"
              title="Export to CSV"
            >
              <DownloadSimple size={13} weight="duotone" />
              <span>CSV</span>
            </button>

            {/* Excel = same CSV but with .xls extension hint */}
            <button
              type="button"
              onClick={() => exportCSV(columns, rows, `${title.replace(/\s+/g, "_")}.xls`)}
              className="flex items-center gap-1.5 h-7 px-2.5 rounded-[var(--radius)] border border-[hsl(var(--border))] bg-[hsl(var(--background))] text-[12px] text-[hsl(var(--foreground))] hover:bg-[hsl(var(--secondary))] transition-colors"
              title="Export to Excel (CSV)"
            >
              <DownloadSimple size={13} weight="duotone" />
              <span>Excel</span>
            </button>

            <PrintButton />
          </>
        )}
      </div>

      {/* Error */}
      {error && (
        <div className="flex items-center gap-2 border-b border-[hsl(var(--destructive)/0.3)] bg-[hsl(var(--destructive)/0.08)] px-4 py-2.5 text-[12px] text-[hsl(var(--destructive))]">
          <Warning size={15} weight="duotone" />
          {error}
        </div>
      )}

      {/* Content */}
      <div className="flex-1 overflow-auto">
        {!generated && !generating && (
          <div className="flex flex-col items-center justify-center h-full gap-2 text-[hsl(var(--muted-foreground))]">
            <Play size={32} weight="duotone" className="opacity-30" />
            <p className="text-[13px]">
              Set parameters and click <strong>Generate</strong> to build the report.
            </p>
          </div>
        )}

        {generating && (
          <div className="flex flex-col items-center justify-center h-full gap-3">
            <div className="flex items-end gap-1 h-8">
              {Array.from({ length: 5 }).map((_, i) => (
                <div
                  key={i}
                  className="skeleton"
                  style={{
                    width: 8,
                    height: `${40 + i * 12}%`,
                    borderRadius: "var(--radius)",
                    animationDelay: `${i * 0.1}s`,
                  }}
                />
              ))}
            </div>
            <p className="text-[12px] text-[hsl(var(--muted-foreground))]">Generating report…</p>
          </div>
        )}

        {generated && !generating && rows.length === 0 && (
          <div className="flex flex-col items-center justify-center h-full text-[12px] text-[hsl(var(--muted-foreground))]">
            No data matches the selected parameters.
          </div>
        )}

        {generated && !generating && rows.length > 0 && (
          <div data-print-content="">
            <table className="w-full border-collapse" style={{ fontSize: 12 }}>
              {/* Header */}
              <thead className="sticky top-0 z-10 bg-[hsl(var(--secondary))]">
                <tr>
                  {hasGrouping && (
                    <th
                      aria-label="Expand"
                      style={{ width: 24, padding: "5px 6px", borderBottom: "1px solid hsl(var(--border))" }}
                    />
                  )}
                  {columns.map((col) => (
                    <th
                      key={col.key}
                      style={{
                        padding: "5px 8px",
                        textAlign: col.type === "number" ? "right" : "left",
                        fontWeight: 500,
                        fontSize: 11,
                        color: "hsl(var(--muted-foreground))",
                        whiteSpace: "nowrap",
                        userSelect: "none",
                        borderBottom: "1px solid hsl(var(--border))",
                        borderRight: "1px solid hsl(var(--border))",
                      }}
                      className="last:border-r-0"
                    >
                      {col.title}
                    </th>
                  ))}
                </tr>
              </thead>

              <tbody>
                {hasGrouping && groupByCol
                  ? Array.from(groupedRows.entries()).map(([groupVal, groupRows]) => (
                      <GroupedRows
                        key={groupVal}
                        groupKey={groupByCol.key}
                        groupValue={groupVal}
                        rows={groupRows}
                        columns={columns}
                        dataColumns={numericCols}
                      />
                    ))
                  : rows.map((row, idx) => (
                      <tr
                        key={idx}
                        className={[
                          "transition-colors",
                          idx % 2 === 0
                            ? "bg-[hsl(var(--background))]"
                            : "bg-[hsl(var(--secondary)/0.3)]",
                          "hover:bg-[hsl(var(--primary)/0.05)]",
                        ].join(" ")}
                      >
                        {columns.map((col) => {
                          const isNumeric = col.type === "number"
                          return (
                            <td
                              key={col.key}
                              style={{
                                padding: "4px 8px",
                                textAlign: isNumeric ? "right" : "left",
                                borderBottom: "1px solid hsl(var(--border))",
                                borderRight: "1px solid hsl(var(--border))",
                                color: "hsl(var(--foreground))",
                                whiteSpace: "nowrap",
                                overflow: "hidden",
                                textOverflow: "ellipsis",
                              }}
                              className="last:border-r-0"
                            >
                              {isNumeric ? (
                                <span className="tabular-nums">{formatValue(row[col.key], col.type)}</span>
                              ) : (
                                formatValue(row[col.key], col.type)
                              )}
                            </td>
                          )
                        })}
                      </tr>
                    ))}
              </tbody>

              {/* Totals */}
              {numericCols.length > 0 && (
                <TotalsRow
                  columns={columns}
                  rows={rows}
                  hasGroupCol={hasGrouping}
                />
              )}
            </table>
          </div>
        )}
      </div>
    </div>
  )
}
