import { useState, useRef, useCallback, useEffect } from "react"
import {
  Package,
  FileText,
  ListBullets,
  Database,
  Stack,
  Calculator,
  ChartBar,
  Clipboard,
  Sliders,
  FolderOpen,
  Folder,
  Code,
  Table,
  Shapes,
  Tag,
  PencilSimple,
  Trash,
  Copy,
  Plus,
  Spinner,
  WarningCircle,
  ArrowCounterClockwise,
} from "@phosphor-icons/react"
import { api } from "@/lib/axios"

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

export type MetaNodeType =
  | "configuration"
  | "section"
  | "object"
  | "attributes"
  | "attribute"
  | "tables"
  | "table"
  | "forms"
  | "form"
  | "modules"
  | "module"

export interface DesignerMetaNode {
  id: string
  name: string
  type: MetaNodeType
  /** Backend meta type key, e.g. "catalogs", "documents" */
  metaType?: string
  children?: DesignerMetaNode[]
  /** moduleId for opening in code editor */
  moduleId?: string
}

export type ContextAction = "add" | "rename" | "delete" | "copy"

interface MetadataTreeProps {
  onOpenModule: (moduleId: string, title: string, objectName: string) => void
  onSelectNode: (node: DesignerMetaNode) => void
}

// ---------------------------------------------------------------------------
// Section definitions
// ---------------------------------------------------------------------------

const SECTIONS: { key: string; label: string; icon: React.ReactNode }[] = [
  { key: "catalogs",             label: "Catalogs",             icon: <Package         size={14} weight="duotone" /> },
  { key: "documents",            label: "Documents",            icon: <FileText        size={14} weight="duotone" /> },
  { key: "enumerations",         label: "Enumerations",         icon: <ListBullets     size={14} weight="duotone" /> },
  { key: "informationRegisters", label: "Info Registers",       icon: <Database        size={14} weight="duotone" /> },
  { key: "accumulationRegisters",label: "Accum. Registers",     icon: <Stack           size={14} weight="duotone" /> },
  { key: "accountingRegisters",  label: "Accounting Registers", icon: <Calculator      size={14} weight="duotone" /> },
  { key: "reports",              label: "Reports",              icon: <ChartBar        size={14} weight="duotone" /> },
  { key: "dataProcessors",       label: "Data Processors",      icon: <Clipboard       size={14} weight="duotone" /> },
  { key: "constants",            label: "Constants",            icon: <Sliders         size={14} weight="duotone" /> },
]

// ---------------------------------------------------------------------------
// Context menu
// ---------------------------------------------------------------------------

interface ContextMenuState {
  x: number
  y: number
  node: DesignerMetaNode
}

// ---------------------------------------------------------------------------
// Component
// ---------------------------------------------------------------------------

export function MetadataTree({ onOpenModule, onSelectNode }: MetadataTreeProps) {
  const [tree, setTree] = useState<Record<string, DesignerMetaNode[]> | null>(null)
  const [loading, setLoading] = useState(false)
  const [error, setError] = useState<string | null>(null)
  const [expanded, setExpanded] = useState<Set<string>>(new Set(["configuration"]))
  const [contextMenu, setContextMenu] = useState<ContextMenuState | null>(null)
  const [renaming, setRenaming] = useState<{ id: string; value: string } | null>(null)
  const containerRef = useRef<HTMLDivElement>(null)

  const load = useCallback(async () => {
    setLoading(true)
    setError(null)
    try {
      const { data } = await api.get<Record<string, DesignerMetaNode[]>>("/designer/metadata")
      setTree(data)
    } catch (err) {
      setError(err instanceof Error ? err.message : "Failed to load metadata")
    } finally {
      setLoading(false)
    }
  }, [])

  useEffect(() => { load() }, [load])

  // Close context menu on outside click
  useEffect(() => {
    if (!contextMenu) return
    const handler = (e: MouseEvent) => {
      if (!(e.target as Element).closest("[data-context-menu]")) {
        setContextMenu(null)
      }
    }
    document.addEventListener("mousedown", handler)
    return () => document.removeEventListener("mousedown", handler)
  }, [contextMenu])

  function toggleExpanded(id: string) {
    setExpanded((prev) => {
      const next = new Set(prev)
      next.has(id) ? next.delete(id) : next.add(id)
      return next
    })
  }

  function handleContextMenu(e: React.MouseEvent, node: DesignerMetaNode) {
    e.preventDefault()
    e.stopPropagation()
    setContextMenu({ x: e.clientX, y: e.clientY, node })
  }

  async function handleContextAction(action: ContextAction, node: DesignerMetaNode) {
    setContextMenu(null)
    switch (action) {
      case "add": {
        if (!node.metaType) break
        const name = prompt("Object name:")
        if (!name) break
        await api.post(`/designer/metadata/${node.metaType}`, { name })
        await load()
        break
      }
      case "rename": {
        setRenaming({ id: node.id, value: node.name })
        break
      }
      case "delete": {
        if (!confirm(`Delete "${node.name}"?`)) break
        await api.delete(`/designer/metadata/${node.id}`)
        await load()
        break
      }
      case "copy": {
        const name = prompt("New name:", `${node.name}_Copy`)
        if (!name) break
        await api.post(`/designer/metadata/${node.metaType ?? "catalogs"}`, {
          name,
          copyFrom: node.id,
        })
        await load()
        break
      }
    }
  }

  async function commitRename() {
    if (!renaming) return
    await api.put(`/designer/metadata/${renaming.id}`, { name: renaming.value })
    setRenaming(null)
    await load()
  }

  // ---------------------------------------------------------------------------
  // Render
  // ---------------------------------------------------------------------------

  return (
    <div
      ref={containerRef}
      className="flex flex-col h-full select-none overflow-hidden"
    >
      {/* Header */}
      <div className="flex h-9 shrink-0 items-center justify-between border-b border-[hsl(var(--border))] px-3">
        <span className="text-[11px] font-semibold uppercase tracking-wide text-[hsl(var(--muted-foreground))]">
          Configuration
        </span>
        <button
          type="button"
          onClick={load}
          title="Reload"
          disabled={loading}
          className="flex h-6 w-6 items-center justify-center rounded-[2px] hover:bg-[hsl(var(--secondary))] text-[hsl(var(--muted-foreground))] transition-colors"
        >
          {loading
            ? <Spinner size={12} weight="duotone" className="animate-spin" />
            : <ArrowCounterClockwise size={12} weight="duotone" />}
        </button>
      </div>

      {/* Tree body */}
      <div className="flex-1 overflow-y-auto overflow-x-hidden py-1">
        {error && (
          <div className="flex items-center gap-2 px-3 py-2 text-[11px] text-[hsl(var(--destructive))]">
            <WarningCircle size={13} weight="duotone" />
            {error}
          </div>
        )}

        {!error && !loading && !tree && (
          <div className="px-3 py-2 text-[11px] text-[hsl(var(--muted-foreground))]">
            No configuration loaded.
          </div>
        )}

        {/* Root: Configuration */}
        {tree && (
          <TreeRow
            label="Configuration"
            nodeId="configuration"
            depth={0}
            icon={expanded.has("configuration")
              ? <FolderOpen size={14} weight="duotone" />
              : <Folder    size={14} weight="duotone" />}
            expanded={expanded.has("configuration")}
            onToggle={() => toggleExpanded("configuration")}
            onContextMenu={(e) =>
              handleContextMenu(e, { id: "configuration", name: "Configuration", type: "configuration" })
            }
          >
            {SECTIONS.map((section) => {
              const items = tree[section.key] ?? []
              const sectionId = `section_${section.key}`
              return (
                <TreeRow
                  key={sectionId}
                  label={`${section.label} (${items.length})`}
                  nodeId={sectionId}
                  depth={1}
                  icon={section.icon}
                  expanded={expanded.has(sectionId)}
                  onToggle={() => toggleExpanded(sectionId)}
                  onContextMenu={(e) =>
                    handleContextMenu(e, {
                      id: sectionId,
                      name: section.label,
                      type: "section",
                      metaType: section.key,
                    })
                  }
                >
                  {items.map((obj) => (
                    <ObjectNode
                      key={obj.id}
                      node={{ ...obj, metaType: section.key }}
                      depth={2}
                      expanded={expanded}
                      renaming={renaming}
                      onToggle={toggleExpanded}
                      onContextMenu={handleContextMenu}
                      onSelect={onSelectNode}
                      onOpenModule={onOpenModule}
                      onRenameChange={(v) => setRenaming((r) => r ? { ...r, value: v } : null)}
                      onRenameCommit={commitRename}
                      onRenameCancel={() => setRenaming(null)}
                    />
                  ))}
                </TreeRow>
              )
            })}
          </TreeRow>
        )}
      </div>

      {/* Context menu */}
      {contextMenu && (
        <ContextMenuPopup
          x={contextMenu.x}
          y={contextMenu.y}
          node={contextMenu.node}
          onAction={handleContextAction}
          onClose={() => setContextMenu(null)}
        />
      )}
    </div>
  )
}

// ---------------------------------------------------------------------------
// Object node (Catalog/Document/etc.) with sub-nodes
// ---------------------------------------------------------------------------

interface ObjectNodeProps {
  node: DesignerMetaNode
  depth: number
  expanded: Set<string>
  renaming: { id: string; value: string } | null
  onToggle: (id: string) => void
  onContextMenu: (e: React.MouseEvent, node: DesignerMetaNode) => void
  onSelect: (node: DesignerMetaNode) => void
  onOpenModule: (moduleId: string, title: string, objectName: string) => void
  onRenameChange: (v: string) => void
  onRenameCommit: () => void
  onRenameCancel: () => void
}

function ObjectNode({
  node,
  depth,
  expanded,
  renaming,
  onToggle,
  onContextMenu,
  onSelect,
  onOpenModule,
  onRenameChange,
  onRenameCommit,
  onRenameCancel,
}: ObjectNodeProps) {
  const isExpanded = expanded.has(node.id)
  const isRenaming = renaming?.id === node.id

  // Sub-nodes built from children
  const attributeNodes = (node.children ?? []).filter((c) => c.type === "attribute")
  const tableNodes     = (node.children ?? []).filter((c) => c.type === "table")
  const formNodes      = (node.children ?? []).filter((c) => c.type === "form")
  const moduleNodes    = (node.children ?? []).filter((c) => c.type === "module")

  return (
    <TreeRow
      label={node.name}
      nodeId={node.id}
      depth={depth}
      icon={<Tag size={13} weight="duotone" />}
      expanded={isExpanded}
      onToggle={() => { onToggle(node.id); onSelect(node) }}
      onDblClick={() => onSelect(node)}
      onContextMenu={(e) => onContextMenu(e, node)}
      renaming={isRenaming}
      renamingValue={renaming?.value ?? ""}
      onRenameChange={onRenameChange}
      onRenameCommit={onRenameCommit}
      onRenameCancel={onRenameCancel}
    >
      {/* Attributes */}
      {isExpanded && (
        <TreeRow
          label={`Attributes (${attributeNodes.length})`}
          nodeId={`${node.id}_attrs`}
          depth={depth + 1}
          icon={<Shapes size={13} weight="duotone" />}
          expanded={expanded.has(`${node.id}_attrs`)}
          onToggle={() => onToggle(`${node.id}_attrs`)}
        >
          {attributeNodes.map((attr) => (
            <TreeRow
              key={attr.id}
              label={attr.name}
              nodeId={attr.id}
              depth={depth + 2}
              icon={<PencilSimple size={12} weight="duotone" />}
              onContextMenu={(e) => onContextMenu(e, { ...attr, metaType: node.metaType })}
              onDblClick={() => onSelect(attr)}
            />
          ))}
        </TreeRow>
      )}

      {/* Tabular sections */}
      {isExpanded && tableNodes.length > 0 && (
        <TreeRow
          label={`Tables (${tableNodes.length})`}
          nodeId={`${node.id}_tables`}
          depth={depth + 1}
          icon={<Table size={13} weight="duotone" />}
          expanded={expanded.has(`${node.id}_tables`)}
          onToggle={() => onToggle(`${node.id}_tables`)}
        >
          {tableNodes.map((tbl) => (
            <TreeRow
              key={tbl.id}
              label={tbl.name}
              nodeId={tbl.id}
              depth={depth + 2}
              icon={<Table size={12} weight="duotone" />}
              onDblClick={() => onSelect(tbl)}
            />
          ))}
        </TreeRow>
      )}

      {/* Forms */}
      {isExpanded && formNodes.length > 0 && (
        <TreeRow
          label={`Forms (${formNodes.length})`}
          nodeId={`${node.id}_forms`}
          depth={depth + 1}
          icon={<Clipboard size={13} weight="duotone" />}
          expanded={expanded.has(`${node.id}_forms`)}
          onToggle={() => onToggle(`${node.id}_forms`)}
        >
          {formNodes.map((frm) => (
            <TreeRow
              key={frm.id}
              label={frm.name}
              nodeId={frm.id}
              depth={depth + 2}
              icon={<Clipboard size={12} weight="duotone" />}
              onDblClick={() => onSelect(frm)}
            />
          ))}
        </TreeRow>
      )}

      {/* Modules */}
      {isExpanded && moduleNodes.length > 0 && (
        <TreeRow
          label="Modules"
          nodeId={`${node.id}_modules`}
          depth={depth + 1}
          icon={<Code size={13} weight="duotone" />}
          expanded={expanded.has(`${node.id}_modules`)}
          onToggle={() => onToggle(`${node.id}_modules`)}
        >
          {moduleNodes.map((mod) => (
            <TreeRow
              key={mod.id}
              label={mod.name}
              nodeId={mod.id}
              depth={depth + 2}
              icon={<Code size={12} weight="duotone" />}
              onDblClick={() => {
                if (mod.moduleId) {
                  onOpenModule(mod.moduleId, `${node.name}.${mod.name}`, node.name)
                }
              }}
            />
          ))}
        </TreeRow>
      )}
    </TreeRow>
  )
}

// ---------------------------------------------------------------------------
// Generic tree row
// ---------------------------------------------------------------------------

interface TreeRowProps {
  label: string
  nodeId: string
  depth: number
  icon?: React.ReactNode
  expanded?: boolean
  onToggle?: () => void
  onContextMenu?: (e: React.MouseEvent) => void
  onDblClick?: () => void
  children?: React.ReactNode
  renaming?: boolean
  renamingValue?: string
  onRenameChange?: (v: string) => void
  onRenameCommit?: () => void
  onRenameCancel?: () => void
}

function TreeRow({
  label,
  nodeId: _nodeId,
  depth,
  icon,
  expanded,
  onToggle,
  onContextMenu,
  onDblClick,
  children,
  renaming = false,
  renamingValue = "",
  onRenameChange,
  onRenameCommit,
  onRenameCancel,
}: TreeRowProps) {
  const hasChildren = !!children
  const indentPx = depth * 14

  return (
    <div>
      <div
        className="group flex h-7 cursor-pointer items-center gap-1 pr-2 text-[11px] hover:bg-[hsl(var(--secondary))] transition-colors"
        style={{ paddingLeft: `${indentPx + 6}px` }}
        onClick={onToggle}
        onDoubleClick={onDblClick}
        onContextMenu={onContextMenu}
      >
        {/* Expand chevron */}
        <span className="w-3 shrink-0 text-center text-[hsl(var(--muted-foreground))]">
          {hasChildren
            ? <span className={`inline-block transition-transform duration-100 ${expanded ? "rotate-90" : ""}`}>▶</span>
            : null}
        </span>

        {/* Icon */}
        {icon && (
          <span className="shrink-0 text-[hsl(var(--muted-foreground))]">{icon}</span>
        )}

        {/* Label / rename input */}
        {renaming ? (
          <input
            autoFocus
            value={renamingValue}
            onChange={(e) => onRenameChange?.(e.target.value)}
            onBlur={onRenameCommit}
            onKeyDown={(e) => {
              if (e.key === "Enter") onRenameCommit?.()
              if (e.key === "Escape") onRenameCancel?.()
              e.stopPropagation()
            }}
            onClick={(e) => e.stopPropagation()}
            className="flex-1 rounded-[2px] border border-[hsl(var(--ring))] bg-[hsl(var(--background))] px-1 text-[11px] outline-none"
          />
        ) : (
          <span className="flex-1 truncate text-[hsl(var(--foreground))]">{label}</span>
        )}
      </div>

      {/* Children */}
      {expanded && children && (
        <div>{children}</div>
      )}
    </div>
  )
}

// ---------------------------------------------------------------------------
// Context menu popup
// ---------------------------------------------------------------------------

interface ContextMenuPopupProps {
  x: number
  y: number
  node: DesignerMetaNode
  onAction: (action: ContextAction, node: DesignerMetaNode) => void
  onClose: () => void
}

function ContextMenuPopup({ x, y, node, onAction, onClose: _ }: ContextMenuPopupProps) {
  const canAdd    = node.type === "section" || node.type === "object" || node.type === "attributes" || node.type === "tables"
  const canRename = node.type === "object" || node.type === "attribute"
  const canDelete = node.type === "object" || node.type === "attribute"
  const canCopy   = node.type === "object"

  // Clamp to viewport
  const style: React.CSSProperties = {
    position: "fixed",
    top: Math.min(y, window.innerHeight - 160),
    left: Math.min(x, window.innerWidth - 160),
    zIndex: 9999,
  }

  return (
    <div
      data-context-menu
      style={style}
      className="w-40 rounded-[2px] border border-[hsl(var(--border))] bg-[hsl(var(--popover))] py-1 shadow-lg"
    >
      {canAdd && (
        <ContextMenuItem
          icon={<Plus size={12} weight="bold" />}
          label="Add"
          onClick={() => onAction("add", node)}
        />
      )}
      {canRename && (
        <ContextMenuItem
          icon={<PencilSimple size={12} weight="duotone" />}
          label="Rename"
          onClick={() => onAction("rename", node)}
        />
      )}
      {canCopy && (
        <ContextMenuItem
          icon={<Copy size={12} weight="duotone" />}
          label="Copy"
          onClick={() => onAction("copy", node)}
        />
      )}
      {canDelete && (
        <>
          <div className="my-1 border-t border-[hsl(var(--border))]" />
          <ContextMenuItem
            icon={<Trash size={12} weight="duotone" />}
            label="Delete"
            onClick={() => onAction("delete", node)}
            danger
          />
        </>
      )}
    </div>
  )
}

function ContextMenuItem({
  icon,
  label,
  onClick,
  danger = false,
}: {
  icon: React.ReactNode
  label: string
  onClick: () => void
  danger?: boolean
}) {
  return (
    <button
      type="button"
      onClick={onClick}
      className={[
        "flex w-full items-center gap-2 px-3 py-1.5 text-[11px] transition-colors",
        danger
          ? "text-[hsl(var(--destructive))] hover:bg-[hsl(var(--destructive)/0.1)]"
          : "text-[hsl(var(--foreground))] hover:bg-[hsl(var(--secondary))]",
      ].join(" ")}
    >
      {icon}
      {label}
    </button>
  )
}
