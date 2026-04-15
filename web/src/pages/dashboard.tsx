import { useEffect } from "react"
import { useNavigate } from "react-router-dom"
import { useGetIdentity } from "@refinedev/core"
import {
  FileText,
  Package,
  ChartBar,
  Database,
  ArrowRight,
  Stack,
  Calculator,
  Clipboard,
  Clock,
  PlusCircle,
  FolderOpen,
} from "@phosphor-icons/react"
import { useTabStore } from "@/stores/tab-store"
import { useMetadata } from "@/hooks/useMetadata"

interface Identity {
  id: string
  name?: string
}

export function DashboardPage() {
  const navigate = useNavigate()
  const { data: identity } = useGetIdentity<Identity>()
  const { tabs, addTab } = useTabStore()
  const { tree, load } = useMetadata()

  useEffect(() => { load() }, [load])

  useEffect(() => {
    addTab({
      id: "dashboard",
      title: "Dashboard",
      path: "/",
      icon: "house",
      closable: false,
    })
  }, [addTab])

  const userName = identity?.name ?? "User"

  const recentTabs = tabs
    .filter((t) => t.id !== "dashboard" && t.closable)
    .slice(-8)
    .reverse()

  const stats = tree
    ? [
        { label: "Catalogs",             count: tree.catalogs?.length             ?? 0, icon: <Package    size={18} weight="duotone" />, path: "/catalogs",              color: "hsl(var(--primary))" },
        { label: "Documents",            count: tree.documents?.length            ?? 0, icon: <FileText   size={18} weight="duotone" />, path: "/documents",             color: "hsl(173 58% 39%)" },
        { label: "Reports",              count: tree.reports?.length              ?? 0, icon: <ChartBar   size={18} weight="duotone" />, path: "/reports",               color: "hsl(24 95% 53%)" },
        { label: "Info Registers",       count: tree.informationRegisters?.length ?? 0, icon: <Database   size={18} weight="duotone" />, path: "/informationRegisters",  color: "hsl(280 67% 49%)" },
        { label: "Accum. Registers",     count: tree.accumulationRegisters?.length ?? 0, icon: <Stack    size={18} weight="duotone" />, path: "/accumulationRegisters", color: "hsl(142 71% 45%)" },
        { label: "Accounting Registers", count: tree.accountingRegisters?.length  ?? 0, icon: <Calculator size={18} weight="duotone" />, path: "/accountingRegisters",  color: "hsl(221 83% 48%)" },
        { label: "Data Processors",      count: tree.dataProcessors?.length       ?? 0, icon: <Clipboard  size={18} weight="duotone" />, path: "/dataProcessors",       color: "hsl(0 84% 60%)" },
      ].filter((s) => s.count > 0)
    : []

  const quickActions = [
    { label: "New Document",  description: "Create a new business document",       icon: <PlusCircle size={16} weight="duotone" />, path: "/documents" },
    { label: "Open Catalog",  description: "Browse reference data and catalogs",   icon: <FolderOpen size={16} weight="duotone" />, path: "/catalogs" },
    { label: "View Reports",  description: "Analytical reports and summaries",     icon: <ChartBar   size={16} weight="duotone" />, path: "/reports" },
    { label: "Registers",     description: "Information and accumulation data",    icon: <Database   size={16} weight="duotone" />, path: "/informationRegisters" },
  ]

  return (
    <div className="p-5 max-w-5xl space-y-5">
      {/* Page title */}
      <div className="border-b border-[hsl(var(--border))] pb-4">
        <h1 className="text-[16px] font-semibold text-[hsl(var(--foreground))]">
          Welcome, {userName}
        </h1>
        <p className="mt-0.5 text-[12px] text-[hsl(var(--muted-foreground))]">
          OES Enterprise — business management platform
        </p>
      </div>

      {/* Metadata stat cards */}
      {stats.length > 0 && (
        <section>
          <h2 className="mb-2.5 text-[10px] font-semibold uppercase tracking-[0.08em] text-[hsl(var(--muted-foreground))]">
            Configuration Objects
          </h2>
          <div className="grid grid-cols-4 gap-2.5">
            {stats.map((stat) => (
              <button
                key={stat.label}
                type="button"
                onClick={() => navigate(stat.path)}
                className="group flex items-center gap-3 border border-[hsl(var(--border))] bg-[hsl(var(--card))] p-3 text-left shadow-card transition-[border-color,box-shadow] duration-150 hover:border-[hsl(var(--primary)/0.4)] hover:shadow-card-hover"
              >
                <span style={{ color: stat.color }} className="shrink-0">
                  {stat.icon}
                </span>
                <div className="min-w-0 flex-1">
                  <p className="text-[10px] text-[hsl(var(--muted-foreground))] truncate">{stat.label}</p>
                  <p className="text-[20px] font-semibold leading-tight text-[hsl(var(--foreground))]">
                    {stat.count}
                  </p>
                </div>
                <ArrowRight
                  size={12}
                  weight="bold"
                  className="shrink-0 opacity-0 group-hover:opacity-40 transition-opacity text-[hsl(var(--muted-foreground))]"
                />
              </button>
            ))}
          </div>
        </section>
      )}

      <div className="grid grid-cols-[1fr_280px] gap-5">
        {/* Left column */}
        <div className="space-y-5">
          {/* Quick actions */}
          <section>
            <h2 className="mb-2.5 text-[10px] font-semibold uppercase tracking-[0.08em] text-[hsl(var(--muted-foreground))]">
              Quick Actions
            </h2>
            <div className="grid grid-cols-2 gap-2">
              {quickActions.map((action) => (
                <button
                  key={action.path}
                  type="button"
                  onClick={() => navigate(action.path)}
                  className="group flex items-start gap-3 border border-[hsl(var(--border))] bg-[hsl(var(--card))] px-3 py-3 text-left shadow-card transition-[border-color,box-shadow] duration-150 hover:border-[hsl(var(--primary)/0.4)] hover:shadow-card-hover"
                >
                  <span className="mt-0.5 shrink-0 text-[hsl(var(--primary))]">{action.icon}</span>
                  <div className="min-w-0">
                    <p className="text-[12px] font-medium text-[hsl(var(--foreground))]">{action.label}</p>
                    <p className="mt-0.5 text-[11px] text-[hsl(var(--muted-foreground))] line-clamp-1">
                      {action.description}
                    </p>
                  </div>
                  <ArrowRight
                    size={12}
                    weight="bold"
                    className="ml-auto mt-1 shrink-0 opacity-0 group-hover:opacity-40 transition-opacity text-[hsl(var(--muted-foreground))]"
                  />
                </button>
              ))}
            </div>
          </section>

          {/* No metadata hint */}
          {!tree && (
            <div className="border border-[hsl(var(--border))] bg-[hsl(var(--card))] px-4 py-4 text-[12px]">
              <p className="font-medium text-[hsl(var(--foreground))] mb-1">No metadata loaded</p>
              <p className="text-[hsl(var(--muted-foreground))]">
                Make sure the OES backend is running. The sidebar will populate with your business objects once connected.
              </p>
            </div>
          )}
        </div>

        {/* Right column — recently opened */}
        <section>
          <h2 className="mb-2.5 text-[10px] font-semibold uppercase tracking-[0.08em] text-[hsl(var(--muted-foreground))]">
            Recently Opened
          </h2>
          {recentTabs.length > 0 ? (
            <div className="border border-[hsl(var(--border))] bg-[hsl(var(--card))] shadow-card overflow-hidden">
              {recentTabs.map((tab, i) => (
                <button
                  key={tab.id}
                  type="button"
                  onClick={() => navigate(tab.path)}
                  className={[
                    "group flex w-full items-center gap-2.5 px-3 py-2 text-left transition-colors duration-150",
                    "hover:bg-[hsl(var(--secondary)/0.5)]",
                    i > 0 ? "border-t border-[hsl(var(--border))]" : "",
                  ].join(" ")}
                >
                  <Clock size={12} weight="duotone" className="shrink-0 text-[hsl(var(--muted-foreground))]" />
                  <span className="flex-1 truncate text-[12px] text-[hsl(var(--foreground))]">
                    {tab.title}
                  </span>
                  <ArrowRight
                    size={11}
                    weight="bold"
                    className="shrink-0 opacity-0 group-hover:opacity-40 transition-opacity text-[hsl(var(--muted-foreground))]"
                  />
                </button>
              ))}
            </div>
          ) : (
            <div className="border border-[hsl(var(--border))] bg-[hsl(var(--card))] px-3 py-4 text-center">
              <p className="text-[11px] text-[hsl(var(--muted-foreground))]">No recent items</p>
            </div>
          )}
        </section>
      </div>
    </div>
  )
}
