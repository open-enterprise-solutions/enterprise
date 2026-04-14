import {
  useState,
  useEffect,
  useRef,
  useCallback,
  type ReactNode,
} from "react"
import { useNavigate, useLocation } from "react-router-dom"
import {
  House,
  FileText,
  Package,
  ChartBar,
  Gear,
  MagnifyingGlass,
  Bell,
  List,
  CaretDown,
  CaretRight,
  Database,
  Stack,
  Calculator,
  Clipboard,
  ChartLineUp,
} from "@phosphor-icons/react"
import { useMetadata, type MetaObjectRef, type MetadataTree } from "@/hooks/useMetadata"
import { useUiStore } from "@/stores/ui-store"
import { useTabStore } from "@/stores/tab-store"
import { TabBar } from "./TabBar"
import { UserMenu } from "./UserMenu"

// ---------------------------------------------------------------------------
// Section config — maps metadata tree keys to display data
// ---------------------------------------------------------------------------

interface SectionConfig {
  key: keyof MetadataTree
  label: string
  icon: ReactNode
  routePrefix: string
}

const SECTIONS: SectionConfig[] = [
  { key: "catalogs",             label: "Catalogs",              icon: <Package size={16} weight="duotone" />,      routePrefix: "catalogs" },
  { key: "documents",            label: "Documents",             icon: <FileText size={16} weight="duotone" />,     routePrefix: "documents" },
  { key: "enumerations",         label: "Enumerations",          icon: <List size={16} weight="duotone" />,         routePrefix: "enumerations" },
  { key: "informationRegisters", label: "Info Registers",        icon: <Database size={16} weight="duotone" />,     routePrefix: "informationRegisters" },
  { key: "accumulationRegisters",label: "Accum. Registers",      icon: <Stack size={16} weight="duotone" />,        routePrefix: "accumulationRegisters" },
  { key: "accountingRegisters",  label: "Accounting Registers",  icon: <Calculator size={16} weight="duotone" />,   routePrefix: "accountingRegisters" },
  { key: "reports",              label: "Reports",               icon: <ChartBar size={16} weight="duotone" />,     routePrefix: "reports" },
  { key: "dataProcessors",       label: "Data Processors",       icon: <Clipboard size={16} weight="duotone" />,    routePrefix: "dataProcessors" },
  { key: "constants",            label: "Constants",             icon: <ChartLineUp size={16} weight="duotone" />,  routePrefix: "constants" },
]

// ---------------------------------------------------------------------------
// Sidebar item button
// ---------------------------------------------------------------------------

interface SidebarItemProps {
  label: string
  active?: boolean
  depth?: number
  icon?: ReactNode
  trailingIcon?: ReactNode
  onClick?: () => void
  collapsed?: boolean
}

function SidebarItem({
  label,
  active = false,
  depth = 0,
  icon,
  trailingIcon,
  onClick,
  collapsed = false,
}: SidebarItemProps) {
  return (
    <button
      type="button"
      onClick={onClick}
      title={collapsed ? label : undefined}
      className={[
        "group flex w-full items-center gap-2 text-[12px] transition-colors outline-none",
        depth === 0
          ? "h-8 px-3 font-medium"
          : "h-7 font-normal",
        depth === 1 && !collapsed ? "pl-8 pr-3" : depth === 1 ? "px-3" : "",
        active
          ? "bg-[hsl(var(--sidebar-accent)/0.12)] text-[hsl(var(--sidebar-accent))]"
          : "text-[hsl(var(--muted-foreground))] hover:bg-[hsl(var(--secondary))] hover:text-[hsl(var(--foreground))]",
      ].join(" ")}
    >
      {icon && <span className="shrink-0">{icon}</span>}
      {!collapsed && (
        <span className="flex-1 truncate text-left">{label}</span>
      )}
      {!collapsed && trailingIcon && (
        <span className="shrink-0 text-[hsl(var(--muted-foreground))]">
          {trailingIcon}
        </span>
      )}
    </button>
  )
}

// ---------------------------------------------------------------------------
// Sidebar section (collapsible group)
// ---------------------------------------------------------------------------

interface SidebarSectionProps {
  config: SectionConfig
  items: MetaObjectRef[]
  sidebarCollapsed: boolean
  searchQuery: string
  activePath: string
  onItemClick: (item: MetaObjectRef, prefix: string) => void
}

function SidebarSection({
  config,
  items,
  sidebarCollapsed,
  searchQuery,
  activePath,
  onItemClick,
}: SidebarSectionProps) {
  const [expanded, setExpanded] = useState(false)

  const filtered = searchQuery
    ? items.filter((item) =>
        item.name.toLowerCase().includes(searchQuery.toLowerCase()),
      )
    : items

  // Auto-expand when search has results
  useEffect(() => {
    if (searchQuery && filtered.length > 0) setExpanded(true)
  }, [searchQuery, filtered.length])

  // Auto-expand if active path matches one of our items
  useEffect(() => {
    const prefix = `/${config.routePrefix}/`
    if (activePath.startsWith(prefix)) setExpanded(true)
  }, [activePath, config.routePrefix])

  if (filtered.length === 0 && searchQuery) return null
  if (items.length === 0) return null

  const sectionActive = activePath.startsWith(`/${config.routePrefix}/`)

  return (
    <div>
      <SidebarItem
        label={config.label}
        icon={config.icon}
        active={sectionActive && !expanded}
        collapsed={sidebarCollapsed}
        trailingIcon={
          expanded
            ? <CaretDown size={11} weight="bold" />
            : <CaretRight size={11} weight="bold" />
        }
        onClick={() => {
          if (!sidebarCollapsed) setExpanded((v) => !v)
        }}
      />

      {expanded && !sidebarCollapsed && (
        <div>
          {filtered.map((item) => {
            const path = `/${config.routePrefix}/${item.id}`
            const isActive = activePath === path || activePath.startsWith(path + "/")
            return (
              <SidebarItem
                key={item.id}
                label={item.name}
                active={isActive}
                depth={1}
                onClick={() => onItemClick(item, config.routePrefix)}
              />
            )
          })}
        </div>
      )}
    </div>
  )
}

// ---------------------------------------------------------------------------
// AppShell
// ---------------------------------------------------------------------------

interface AppShellProps {
  children?: ReactNode
}

export function AppShell({ children }: AppShellProps) {
  const navigate = useNavigate()
  const location = useLocation()
  const searchRef = useRef<HTMLInputElement>(null)
  const [searchQuery, setSearchQuery] = useState("")
  const [notifCount] = useState(0)

  const { sidebarCollapsed, toggleSidebar } = useUiStore()
  const { addTab } = useTabStore()
  const { tree, loading: metaLoading, load } = useMetadata()

  // Load metadata on mount
  useEffect(() => {
    load()
  }, [load])

  // Ctrl+F focuses search
  useEffect(() => {
    const handler = (e: KeyboardEvent) => {
      if ((e.ctrlKey || e.metaKey) && e.key === "f") {
        e.preventDefault()
        searchRef.current?.focus()
      }
      if (e.key === "Escape" && document.activeElement === searchRef.current) {
        setSearchQuery("")
        searchRef.current?.blur()
      }
    }
    window.addEventListener("keydown", handler)
    return () => window.removeEventListener("keydown", handler)
  }, [])

  // Navigate to home and register dashboard tab on mount
  useEffect(() => {
    addTab({
      id: "dashboard",
      title: "Dashboard",
      path: "/",
      icon: "house",
      closable: false,
    })
  }, [addTab])

  const handleItemClick = useCallback(
    (item: MetaObjectRef, prefix: string) => {
      const path = `/${prefix}/${item.id}`
      addTab({
        id: `${prefix}/${item.id}`,
        title: item.name,
        path,
        icon: "list",
        closable: true,
      })
      navigate(path)
    },
    [navigate, addTab],
  )

  const activePath = location.pathname

  return (
    <div className="flex h-screen overflow-hidden density-compact">
      {/* ------------------------------------------------------------------ */}
      {/* Sidebar                                                              */}
      {/* ------------------------------------------------------------------ */}
      <aside
        className={[
          "flex flex-col border-r border-[hsl(var(--sidebar-border))] bg-[hsl(var(--sidebar))]",
          "transition-[width] duration-150 overflow-hidden",
          sidebarCollapsed ? "w-12" : "w-56",
        ].join(" ")}
      >
        {/* Logo / collapse toggle */}
        <div className="flex h-12 shrink-0 items-center justify-between border-b border-[hsl(var(--sidebar-border))] px-3">
          {!sidebarCollapsed && (
            <span className="text-[12px] font-semibold text-[hsl(var(--foreground))] tracking-wide truncate pr-1">
              OES
            </span>
          )}
          <button
            type="button"
            onClick={toggleSidebar}
            title={sidebarCollapsed ? "Expand sidebar" : "Collapse sidebar"}
            className="flex h-7 w-7 shrink-0 items-center justify-center rounded-[var(--radius)] hover:bg-[hsl(var(--secondary))] transition-colors"
          >
            <List size={16} weight="duotone" />
          </button>
        </div>

        {/* Dashboard link */}
        <div className="pt-1">
          <SidebarItem
            label="Dashboard"
            icon={<House size={16} weight="duotone" />}
            active={activePath === "/"}
            collapsed={sidebarCollapsed}
            onClick={() => {
              navigate("/")
              addTab({
                id: "dashboard",
                title: "Dashboard",
                path: "/",
                icon: "house",
                closable: false,
              })
            }}
          />
        </div>

        {/* Metadata sections */}
        <nav className="flex-1 overflow-y-auto overflow-x-hidden py-1">
          {metaLoading && (
            <div className="space-y-1 px-3 py-2">
              {[1, 2, 3, 4].map((i) => (
                <div key={i} className="skeleton h-7 w-full" />
              ))}
            </div>
          )}

          {tree &&
            SECTIONS.map((section) => {
              const items = (tree[section.key] as MetaObjectRef[] | undefined) ?? []
              return (
                <SidebarSection
                  key={section.key}
                  config={section}
                  items={items}
                  sidebarCollapsed={sidebarCollapsed}
                  searchQuery={searchQuery}
                  activePath={activePath}
                  onItemClick={handleItemClick}
                />
              )
            })}
        </nav>

        {/* Settings link at bottom */}
        <div className="border-t border-[hsl(var(--sidebar-border))] py-1">
          <SidebarItem
            label="Settings"
            icon={<Gear size={16} weight="duotone" />}
            active={activePath.startsWith("/settings")}
            collapsed={sidebarCollapsed}
            onClick={() => navigate("/settings")}
          />
        </div>
      </aside>

      {/* ------------------------------------------------------------------ */}
      {/* Main content area                                                    */}
      {/* ------------------------------------------------------------------ */}
      <div className="flex flex-1 flex-col overflow-hidden min-w-0">
        {/* Top toolbar */}
        <header className="flex h-12 shrink-0 items-center gap-3 border-b border-[hsl(var(--border))] px-4">
          {/* Global search */}
          <div className="flex flex-1 items-center gap-2 rounded-[var(--radius)] border border-[hsl(var(--input))] bg-[hsl(var(--background))] px-3 py-1.5 text-[12px] max-w-[380px]">
            <MagnifyingGlass size={14} className="shrink-0 text-[hsl(var(--muted-foreground))]" />
            <input
              ref={searchRef}
              type="text"
              value={searchQuery}
              onChange={(e) => setSearchQuery(e.target.value)}
              placeholder="Search..."
              aria-label="Search"
              className="flex-1 bg-transparent outline-none placeholder:text-[hsl(var(--muted-foreground))]"
            />
            <kbd className="hidden text-[10px] text-[hsl(var(--muted-foreground))] sm:inline opacity-60">
              Ctrl+F
            </kbd>
          </div>

          <div className="flex-1" />

          {/* Notifications */}
          <button
            type="button"
            title="Notifications"
            className="relative flex h-8 w-8 items-center justify-center rounded-[var(--radius)] hover:bg-[hsl(var(--secondary))] transition-colors"
          >
            <Bell size={17} weight="duotone" />
            {notifCount > 0 && (
              <span className="absolute right-1 top-1 flex h-3.5 w-3.5 items-center justify-center rounded-full bg-[hsl(var(--destructive))] text-[9px] font-bold text-white">
                {notifCount > 9 ? "9+" : notifCount}
              </span>
            )}
          </button>

          {/* User menu */}
          <UserMenu />
        </header>

        {/* Tab bar */}
        <TabBar />

        {/* Content */}
        <main className="flex-1 overflow-auto">
          {children}
        </main>
      </div>
    </div>
  )
}
