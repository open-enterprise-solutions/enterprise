/**
 * PrintTemplate — wraps printable content.
 *
 * On screen: renders children normally.
 * On print: hides navigation chrome (via print.css) and shows a print header
 * with company name, report title, and generation date.
 *
 * Usage:
 *   <PrintTemplate title="Sales Report" company="Acme Corp">
 *     <ReportViewer ... />
 *   </PrintTemplate>
 *
 * The Print button calls window.print() which triggers @media print from
 * print.css — no extra dependencies needed.
 */

import { useCallback } from "react"
import { Printer } from "@phosphor-icons/react"

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

export interface PrintTemplateProps {
  title: string
  company?: string
  children: React.ReactNode
  /** If true the Print button is rendered inline (no CommandBar integration). */
  showPrintButton?: boolean
  className?: string
}

// ---------------------------------------------------------------------------
// Print header — shown only on @media print via data-print-only
// ---------------------------------------------------------------------------

function PrintHeader({ title, company }: { title: string; company?: string }) {
  const now = new Date().toLocaleString(undefined, {
    year: "numeric",
    month: "2-digit",
    day: "2-digit",
    hour: "2-digit",
    minute: "2-digit",
  })

  return (
    <div
      data-print-only=""
      className="print-header"
      aria-hidden="true"
      style={{
        display: "none", // overridden by print.css
        fontFamily: "'IBM Plex Sans', Arial, sans-serif",
        color: "#000",
      }}
    >
      <div style={{ display: "flex", justifyContent: "space-between", alignItems: "flex-end" }}>
        <div>
          {company && (
            <div style={{ fontSize: "10pt", color: "#555", marginBottom: 2 }}>
              {company}
            </div>
          )}
          <div style={{ fontSize: "14pt", fontWeight: 600 }}>{title}</div>
        </div>
        <div style={{ fontSize: "9pt", color: "#777", textAlign: "right" }}>
          <div>Generated</div>
          <div>{now}</div>
        </div>
      </div>
    </div>
  )
}

// ---------------------------------------------------------------------------
// Print button — can be used standalone or embedded in a toolbar
// ---------------------------------------------------------------------------

export function PrintButton({ label = "Print" }: { label?: string }) {
  const handlePrint = useCallback(() => {
    window.print()
  }, [])

  return (
    <button
      type="button"
      onClick={handlePrint}
      data-no-print=""
      className="flex items-center gap-1.5 h-7 px-2.5 rounded-[var(--radius)] border border-[hsl(var(--border))] bg-[hsl(var(--background))] text-[12px] text-[hsl(var(--foreground))] hover:bg-[hsl(var(--secondary))] transition-colors"
    >
      <Printer size={14} weight="duotone" />
      <span>{label}</span>
    </button>
  )
}

// ---------------------------------------------------------------------------
// PrintTemplate — main export
// ---------------------------------------------------------------------------

export function PrintTemplate({
  title,
  company,
  children,
  showPrintButton = false,
  className,
}: PrintTemplateProps) {
  return (
    <div className={className} data-print-content="">
      {/* Visible only in print media */}
      <PrintHeader title={title} company={company} />

      {/* Optional inline print button (screen only) */}
      {showPrintButton && (
        <div className="mb-3 flex justify-end" data-no-print="">
          <PrintButton />
        </div>
      )}

      {/* Actual content */}
      {children}
    </div>
  )
}
