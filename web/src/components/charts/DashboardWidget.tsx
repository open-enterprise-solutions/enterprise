/**
 * DashboardWidget — compact Recharts card for dashboard grid layout.
 *
 * Supports: bar | line | pie | area chart types.
 * Responsive via ResponsiveContainer.
 * Shows skeleton while loading.
 */

import {
  ResponsiveContainer,
  BarChart,
  Bar,
  LineChart,
  Line,
  PieChart,
  Pie,
  Cell,
  AreaChart,
  Area,
  XAxis,
  YAxis,
  Tooltip,
  CartesianGrid,
} from "recharts"
import { ChartBar, ChartLine, ChartPie, Waves } from "@phosphor-icons/react"

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

export interface DashboardWidgetProps {
  title: string
  type: "bar" | "line" | "pie" | "area"
  data: { name: string; value: number }[]
  color?: string
  loading?: boolean
  /** Height in px for the chart area. Default: 140 */
  height?: number
  /** Extra class names for the card wrapper */
  className?: string
}

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

// OES Enterprise primary + chart palette
const DEFAULT_COLOR = "hsl(221, 83%, 48%)"
const PIE_COLORS = [
  "hsl(221, 83%, 48%)",
  "hsl(173, 58%, 39%)",
  "hsl(24,  95%, 53%)",
  "hsl(280, 67%, 49%)",
  "hsl(142, 71%, 45%)",
]

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

function typeIcon(type: DashboardWidgetProps["type"]) {
  switch (type) {
    case "bar":  return <ChartBar  size={13} weight="duotone" />
    case "line": return <ChartLine size={13} weight="duotone" />
    case "pie":  return <ChartPie  size={13} weight="duotone" />
    case "area": return <Waves     size={13} weight="duotone" />
  }
}

// Compact tooltip
function CompactTooltip({
  active,
  payload,
  label,
}: {
  active?: boolean
  payload?: { value: number }[]
  label?: string
}) {
  if (!active || !payload?.length) return null
  const val = payload[0].value
  return (
    <div
      className="rounded-[var(--radius)] border border-[hsl(var(--border))] bg-[hsl(var(--popover))] px-2 py-1 text-[11px] shadow-sm"
      style={{ color: "hsl(var(--popover-foreground))" }}
    >
      <span className="font-medium">{label}: </span>
      <span className="tabular-nums">
        {new Intl.NumberFormat().format(val)}
      </span>
    </div>
  )
}

// ---------------------------------------------------------------------------
// Skeleton
// ---------------------------------------------------------------------------

function WidgetSkeleton({ height }: { height: number }) {
  return (
    <div aria-hidden="true" style={{ height }} className="flex items-end gap-1 px-2 pb-1">
      {Array.from({ length: 7 }).map((_, i) => (
        <div
          key={i}
          className="skeleton flex-1"
          style={{
            height: `${30 + ((i * 17 + 11) % 60)}%`,
            borderRadius: "var(--radius)",
          }}
        />
      ))}
    </div>
  )
}

// ---------------------------------------------------------------------------
// Chart renders
// ---------------------------------------------------------------------------

function BarChartInner({
  data,
  color,
  height,
}: {
  data: DashboardWidgetProps["data"]
  color: string
  height: number
}) {
  return (
    <ResponsiveContainer width="100%" height={height}>
      <BarChart data={data} margin={{ top: 4, right: 4, bottom: 0, left: -20 }}>
        <CartesianGrid vertical={false} stroke="hsl(var(--border))" strokeDasharray="3 3" />
        <XAxis
          dataKey="name"
          tick={{ fontSize: 10, fill: "hsl(var(--muted-foreground))" }}
          axisLine={false}
          tickLine={false}
        />
        <YAxis
          tick={{ fontSize: 10, fill: "hsl(var(--muted-foreground))" }}
          axisLine={false}
          tickLine={false}
          tickFormatter={(v: number) =>
            v >= 1000 ? `${(v / 1000).toFixed(0)}k` : String(v)
          }
        />
        <Tooltip content={<CompactTooltip />} />
        <Bar dataKey="value" fill={color} radius={[2, 2, 0, 0]} maxBarSize={32} />
      </BarChart>
    </ResponsiveContainer>
  )
}

function LineChartInner({
  data,
  color,
  height,
}: {
  data: DashboardWidgetProps["data"]
  color: string
  height: number
}) {
  return (
    <ResponsiveContainer width="100%" height={height}>
      <LineChart data={data} margin={{ top: 4, right: 4, bottom: 0, left: -20 }}>
        <CartesianGrid vertical={false} stroke="hsl(var(--border))" strokeDasharray="3 3" />
        <XAxis
          dataKey="name"
          tick={{ fontSize: 10, fill: "hsl(var(--muted-foreground))" }}
          axisLine={false}
          tickLine={false}
        />
        <YAxis
          tick={{ fontSize: 10, fill: "hsl(var(--muted-foreground))" }}
          axisLine={false}
          tickLine={false}
          tickFormatter={(v: number) =>
            v >= 1000 ? `${(v / 1000).toFixed(0)}k` : String(v)
          }
        />
        <Tooltip content={<CompactTooltip />} />
        <Line
          type="monotone"
          dataKey="value"
          stroke={color}
          strokeWidth={2}
          dot={false}
          activeDot={{ r: 3, fill: color }}
        />
      </LineChart>
    </ResponsiveContainer>
  )
}

function AreaChartInner({
  data,
  color,
  height,
}: {
  data: DashboardWidgetProps["data"]
  color: string
  height: number
}) {
  return (
    <ResponsiveContainer width="100%" height={height}>
      <AreaChart data={data} margin={{ top: 4, right: 4, bottom: 0, left: -20 }}>
        <defs>
          <linearGradient id="area-fill" x1="0" y1="0" x2="0" y2="1">
            <stop offset="5%" stopColor={color} stopOpacity={0.25} />
            <stop offset="95%" stopColor={color} stopOpacity={0} />
          </linearGradient>
        </defs>
        <CartesianGrid vertical={false} stroke="hsl(var(--border))" strokeDasharray="3 3" />
        <XAxis
          dataKey="name"
          tick={{ fontSize: 10, fill: "hsl(var(--muted-foreground))" }}
          axisLine={false}
          tickLine={false}
        />
        <YAxis
          tick={{ fontSize: 10, fill: "hsl(var(--muted-foreground))" }}
          axisLine={false}
          tickLine={false}
          tickFormatter={(v: number) =>
            v >= 1000 ? `${(v / 1000).toFixed(0)}k` : String(v)
          }
        />
        <Tooltip content={<CompactTooltip />} />
        <Area
          type="monotone"
          dataKey="value"
          stroke={color}
          strokeWidth={2}
          fill="url(#area-fill)"
          dot={false}
        />
      </AreaChart>
    </ResponsiveContainer>
  )
}

function PieChartInner({
  data,
  height,
}: {
  data: DashboardWidgetProps["data"]
  height: number
}) {
  return (
    <ResponsiveContainer width="100%" height={height}>
      <PieChart>
        <Pie
          data={data}
          dataKey="value"
          nameKey="name"
          cx="50%"
          cy="50%"
          innerRadius="40%"
          outerRadius="70%"
          paddingAngle={2}
        >
          {data.map((_, idx) => (
            <Cell
              key={idx}
              fill={PIE_COLORS[idx % PIE_COLORS.length]}
            />
          ))}
        </Pie>
        <Tooltip
          content={({ active, payload }) => {
            if (!active || !payload?.length) return null
            const entry = payload[0]
            return (
              <div
                className="rounded-[var(--radius)] border border-[hsl(var(--border))] bg-[hsl(var(--popover))] px-2 py-1 text-[11px] shadow-sm"
                style={{ color: "hsl(var(--popover-foreground))" }}
              >
                <span className="font-medium">{entry.name}: </span>
                <span className="tabular-nums">
                  {new Intl.NumberFormat().format(entry.value as number)}
                </span>
              </div>
            )
          }}
        />
      </PieChart>
    </ResponsiveContainer>
  )
}

// ---------------------------------------------------------------------------
// DashboardWidget — main export
// ---------------------------------------------------------------------------

export function DashboardWidget({
  title,
  type,
  data,
  color = DEFAULT_COLOR,
  loading = false,
  height = 140,
  className,
}: DashboardWidgetProps) {
  // Compute simple summary stat (total)
  const total = data.reduce((acc, d) => acc + d.value, 0)

  return (
    <div
      className={[
        "flex flex-col border border-[hsl(var(--border))] bg-[hsl(var(--card))] overflow-hidden",
        className ?? "",
      ].join(" ")}
      style={{ borderRadius: "var(--radius)" }}
    >
      {/* Card header */}
      <div className="flex items-center justify-between gap-2 px-3 pt-3 pb-1.5">
        <div className="flex items-center gap-1.5 min-w-0">
          <span className="text-[hsl(var(--muted-foreground))]">
            {typeIcon(type)}
          </span>
          <span className="text-[12px] font-medium text-[hsl(var(--foreground))] truncate">
            {title}
          </span>
        </div>
        {/* Total pill */}
        {!loading && (
          <span className="shrink-0 text-[11px] font-semibold tabular-nums text-[hsl(var(--primary))]">
            {new Intl.NumberFormat().format(total)}
          </span>
        )}
      </div>

      {/* Chart area */}
      <div className="flex-1 px-1 pb-2">
        {loading ? (
          <WidgetSkeleton height={height} />
        ) : (
          <>
            {type === "bar"  && <BarChartInner  data={data} color={color} height={height} />}
            {type === "line" && <LineChartInner data={data} color={color} height={height} />}
            {type === "area" && <AreaChartInner data={data} color={color} height={height} />}
            {type === "pie"  && <PieChartInner  data={data} height={height} />}
          </>
        )}
      </div>
    </div>
  )
}
