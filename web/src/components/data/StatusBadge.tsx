/**
 * StatusBadge — document/record status indicator.
 *
 * Uses CSS variables from globals.css:
 *   --status-posted, --status-draft, --status-deleted, --status-progress
 *
 * Size variants:
 *   sm — 11px text, tight padding (for dense table cells)
 *   md — 12px text, normal padding (for headers, cards)
 */

export type DocumentStatus = "posted" | "draft" | "deleted" | "inProgress"

export interface StatusBadgeProps {
  status: DocumentStatus
  size?: "sm" | "md"
}

// Each status maps to: label, CSS variable suffix, dot color class
const STATUS_CONFIG: Record<
  DocumentStatus,
  { label: string; bgVar: string; textVar: string; borderVar: string }
> = {
  posted: {
    label: "Posted",
    bgVar: "var(--status-posted)",
    textVar: "var(--status-posted)",
    borderVar: "var(--status-posted)",
  },
  draft: {
    label: "Draft",
    bgVar: "var(--status-draft)",
    textVar: "var(--status-draft)",
    borderVar: "var(--status-draft)",
  },
  deleted: {
    label: "Deleted",
    bgVar: "var(--status-deleted)",
    textVar: "var(--status-deleted)",
    borderVar: "var(--status-deleted)",
  },
  inProgress: {
    label: "In Progress",
    bgVar: "var(--status-progress)",
    textVar: "var(--status-progress)",
    borderVar: "var(--status-progress)",
  },
}

export function StatusBadge({ status, size = "md" }: StatusBadgeProps) {
  const cfg = STATUS_CONFIG[status] ?? STATUS_CONFIG.draft

  const fontSize = size === "sm" ? "11px" : "12px"
  const padding = size === "sm" ? "0 5px" : "1px 7px"
  const dotSize = size === "sm" ? 5 : 6

  return (
    <span
      role="status"
      aria-label={cfg.label}
      style={{
        display: "inline-flex",
        alignItems: "center",
        gap: 4,
        borderRadius: "var(--radius)",
        border: `1px solid hsl(${cfg.borderVar} / 0.35)`,
        background: `hsl(${cfg.bgVar} / 0.1)`,
        color: `hsl(${cfg.textVar})`,
        fontSize,
        fontWeight: 500,
        padding,
        lineHeight: "18px",
        whiteSpace: "nowrap",
        userSelect: "none",
      }}
    >
      {/* Status dot */}
      <span
        aria-hidden="true"
        style={{
          display: "inline-block",
          width: dotSize,
          height: dotSize,
          borderRadius: "50%",
          background: `hsl(${cfg.bgVar})`,
          flexShrink: 0,
        }}
      />
      {cfg.label}
    </span>
  )
}
