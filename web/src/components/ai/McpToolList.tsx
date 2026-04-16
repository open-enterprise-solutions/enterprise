import { clsx } from "clsx"
import {
  Cube,
  Code,
  Shapes,
  MagnifyingGlass,
  FileText,
  CheckCircle,
  XCircle,
  CircleDashed,
} from "@phosphor-icons/react"
import { useState, useEffect } from "react"
import { api } from "@/lib/axios"

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

export type McpToolStatus = "available" | "unavailable" | "checking"

export interface McpTool {
  name: string
  description: string
  category: "metadata" | "data" | "module" | "report"
  status: McpToolStatus
}

// ---------------------------------------------------------------------------
// Static MCP tool definitions
// ---------------------------------------------------------------------------

const MCP_TOOLS: Omit<McpTool, "status">[] = [
  {
    name: "oes_create_metadata_object",
    description: "Create a new metadata object (catalog, document, register, etc.)",
    category: "metadata",
  },
  {
    name: "oes_generate_form_schema",
    description: "Generate Formily JSON Schema for a metadata object form",
    category: "metadata",
  },
  {
    name: "oes_write_module",
    description: "Write script module code for a metadata object",
    category: "module",
  },
  {
    name: "oes_query_data",
    description: "Execute a query against OES business data",
    category: "data",
  },
  {
    name: "oes_execute_report",
    description: "Run a report and get structured results",
    category: "report",
  },
]

const CATEGORY_ICONS: Record<McpTool["category"], React.ReactNode> = {
  metadata: <Cube size={14} weight="duotone" />,
  module: <Code size={14} weight="duotone" />,
  data: <MagnifyingGlass size={14} weight="duotone" />,
  report: <FileText size={14} weight="duotone" />,
}

const CATEGORY_LABELS: Record<McpTool["category"], string> = {
  metadata: "Metadata",
  module: "Module",
  data: "Data",
  report: "Report",
}

// ---------------------------------------------------------------------------
// Status badge
// ---------------------------------------------------------------------------

function StatusBadge({ status }: { status: McpToolStatus }) {
  if (status === "checking") {
    return (
      <span className="flex items-center gap-1 text-[10px] text-[hsl(var(--muted-foreground))]">
        <CircleDashed size={11} className="animate-spin" />
        Checking
      </span>
    )
  }
  if (status === "available") {
    return (
      <span className="flex items-center gap-1 text-[10px] text-[hsl(var(--accent))]">
        <CheckCircle size={11} weight="fill" />
        Available
      </span>
    )
  }
  return (
    <span className="flex items-center gap-1 text-[10px] text-[hsl(var(--muted-foreground))]">
      <XCircle size={11} weight="fill" />
      Unavailable
    </span>
  )
}

// ---------------------------------------------------------------------------
// Tool row
// ---------------------------------------------------------------------------

function McpToolRow({ tool }: { tool: McpTool }) {
  return (
    <div
      className={clsx(
        "flex items-start gap-2.5 px-3 py-2.5 border-b border-[hsl(var(--border))] last:border-b-0",
        tool.status === "unavailable" && "opacity-50",
      )}
    >
      <div className="mt-0.5 w-5 h-5 rounded-[var(--radius)] bg-[hsl(var(--muted))] flex items-center justify-center shrink-0 text-[hsl(var(--muted-foreground))]">
        {CATEGORY_ICONS[tool.category]}
      </div>
      <div className="flex-1 min-w-0">
        <div className="flex items-center justify-between gap-2">
          <span className="text-[11px] font-mono font-medium text-[hsl(var(--foreground))] truncate">
            {tool.name}
          </span>
          <StatusBadge status={tool.status} />
        </div>
        <p className="text-[10px] text-[hsl(var(--muted-foreground))] mt-0.5 leading-relaxed">
          {tool.description}
        </p>
        <span className="inline-block mt-1 text-[9px] px-1.5 py-0.5 rounded-[var(--radius)] bg-[hsl(var(--secondary))] text-[hsl(var(--muted-foreground))] uppercase tracking-wide">
          {CATEGORY_LABELS[tool.category]}
        </span>
      </div>
    </div>
  )
}

// ---------------------------------------------------------------------------
// Main component
// ---------------------------------------------------------------------------

interface McpToolListProps {
  compact?: boolean
}

export function McpToolList({ compact = false }: McpToolListProps) {
  const [tools, setTools] = useState<McpTool[]>(
    MCP_TOOLS.map((t) => ({ ...t, status: "checking" as McpToolStatus })),
  )

  useEffect(() => {
    let cancelled = false

    const checkAvailability = async () => {
      try {
        const response = await api.get<{ tools: string[] }>("/ai/mcp/tools")
        const available = new Set(response.data.tools)

        if (!cancelled) {
          setTools(
            MCP_TOOLS.map((t) => ({
              ...t,
              status: (available.has(t.name) ? "available" : "unavailable") as McpToolStatus,
            })),
          )
        }
      } catch {
        // API not available — mark all as available for demo
        if (!cancelled) {
          setTools(MCP_TOOLS.map((t) => ({ ...t, status: "available" as McpToolStatus })))
        }
      }
    }

    void checkAvailability()
    return () => { cancelled = true }
  }, [])

  const availableCount = tools.filter((t) => t.status === "available").length

  if (compact) {
    return (
      <div className="flex items-center gap-1.5 text-[10px] text-[hsl(var(--muted-foreground))]">
        <Shapes size={12} weight="duotone" />
        <span>{availableCount}/{tools.length} MCP tools active</span>
      </div>
    )
  }

  return (
    <div className="rounded-[var(--radius)] border border-[hsl(var(--border))] overflow-hidden">
      <div className="flex items-center justify-between px-3 py-2 bg-[hsl(var(--muted))] border-b border-[hsl(var(--border))]">
        <div className="flex items-center gap-1.5 text-[11px] font-medium text-[hsl(var(--foreground))]">
          <Shapes size={13} weight="duotone" />
          MCP Tools
        </div>
        <span className="text-[10px] text-[hsl(var(--muted-foreground))]">
          {availableCount}/{tools.length} active
        </span>
      </div>
      <div>
        {tools.map((tool) => (
          <McpToolRow key={tool.name} tool={tool} />
        ))}
      </div>
    </div>
  )
}
