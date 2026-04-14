import { useEffect } from "react"
import { useNavigate } from "react-router-dom"
import { useGetIdentity } from "@refinedev/core"
import {
  House,
  FileText,
  Package,
  ChartBar,
  Database,
  ArrowRight,
} from "@phosphor-icons/react"
import { useTabStore } from "@/stores/tab-store"
import { useMetadata } from "@/hooks/useMetadata"
import { DashboardWidget } from "@/components/charts/DashboardWidget"

interface Identity {
  id: string
  name?: string
}

interface QuickLink {
  label: string
  description: string
  icon: React.ReactNode
  path: string
}

const QUICK_LINKS: QuickLink[] = [
  {
    label: "Catalogs",
    description: "Browse product catalogs, counterparties, and reference data",
    icon: <Package size={20} weight="duotone" />,
    path: "/catalogs",
  },
  {
    label: "Documents",
    description: "Create and manage business documents",
    icon: <FileText size={20} weight="duotone" />,
    path: "/documents",
  },
  {
    label: "Reports",
    description: "View analytical reports and summaries",
    icon: <ChartBar size={20} weight="duotone" />,
    path: "/reports",
  },
  {
    label: "Registers",
    description: "Information and accumulation registers",
    icon: <Database size={20} weight="duotone" />,
    path: "/informationRegisters",
  },
]

export function DashboardPage() {
  const navigate = useNavigate()
  const { data: identity } = useGetIdentity<Identity>()
  const { tabs, addTab } = useTabStore()
  const { tree, load } = useMetadata()

  useEffect(() => { load() }, [load])

  // Register dashboard tab
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

  // Recent tabs (exclude dashboard itself)
  const recentTabs = tabs
    .filter((t) => t.id !== "dashboard" && t.closable)
    .slice(-6)
    .reverse()

  // Quick metadata counts
  const catalogCount  = tree?.catalogs?.length ?? 0
  const documentCount = tree?.documents?.length ?? 0
  const reportCount   = tree?.reports?.length ?? 0

  return (
    <div className="p-6 max-w-4xl space-y-8">
      {/* Welcome header */}
      <div className="flex items-center gap-3">
        <div className="flex h-10 w-10 items-center justify-center rounded-full bg-[hsl(var(--primary))] text-[hsl(var(--primary-foreground))] text-[14px] font-semibold select-none">
          <House size={20} weight="duotone" />
        </div>
        <div>
          <h1 className="text-[20px] font-semibold text-[hsl(var(--foreground))]">
            Welcome, {userName}
          </h1>
          <p className="text-[12px] text-[hsl(var(--muted-foreground))]">
            OES Enterprise — your business management platform
          </p>
        </div>
      </div>

      {/* Metadata summary stats */}
      {tree && (
        <div className="grid grid-cols-3 gap-3">
          {[
            { label: "Catalogs",  count: catalogCount,  icon: <Package size={16} weight="duotone" />,  path: "/catalogs" },
            { label: "Documents", count: documentCount, icon: <FileText size={16} weight="duotone" />, path: "/documents" },
            { label: "Reports",   count: reportCount,   icon: <ChartBar size={16} weight="duotone" />, path: "/reports" },
          ].map((stat) => (
            <button
              key={stat.label}
              type="button"
              onClick={() => navigate(stat.path)}
              className="group flex items-center gap-3 border border-[hsl(var(--border))] bg-[hsl(var(--card))] p-4 text-left transition-colors hover:border-[hsl(var(--ring))] hover:bg-[hsl(var(--secondary)/0.3)]"
              style={{ borderRadius: "var(--radius)" }}
            >
              <span className="text-[hsl(var(--primary))]">{stat.icon}</span>
              <div>
                <p className="text-[11px] text-[hsl(var(--muted-foreground))]">{stat.label}</p>
                <p className="text-[18px] font-semibold text-[hsl(var(--foreground))]">{stat.count}</p>
              </div>
              <ArrowRight
                size={14}
                weight="bold"
                className="ml-auto opacity-0 group-hover:opacity-60 transition-opacity text-[hsl(var(--muted-foreground))]"
              />
            </button>
          ))}
        </div>
      )}

      {/* Recent tabs */}
      {recentTabs.length > 0 && (
        <div>
          <h2 className="mb-3 text-[11px] font-semibold uppercase tracking-widest text-[hsl(var(--muted-foreground))]">
            Recently Opened
          </h2>
          <div className="space-y-1">
            {recentTabs.map((tab) => (
              <button
                key={tab.id}
                type="button"
                onClick={() => navigate(tab.path)}
                className="group flex w-full items-center gap-3 rounded-[var(--radius)] border border-transparent px-3 py-2 text-left text-[12px] transition-colors hover:border-[hsl(var(--border))] hover:bg-[hsl(var(--secondary)/0.5)]"
              >
                <FileText size={14} weight="duotone" className="shrink-0 text-[hsl(var(--muted-foreground))]" />
                <span className="flex-1 truncate text-[hsl(var(--foreground))]">{tab.title}</span>
                <span className="text-[11px] text-[hsl(var(--muted-foreground))] opacity-0 group-hover:opacity-100 transition-opacity">
                  {tab.path}
                </span>
                <ArrowRight size={12} weight="bold" className="opacity-0 group-hover:opacity-50 transition-opacity text-[hsl(var(--muted-foreground))]" />
              </button>
            ))}
          </div>
        </div>
      )}

      {/* Quick navigation */}
      <div>
        <h2 className="mb-3 text-[11px] font-semibold uppercase tracking-widest text-[hsl(var(--muted-foreground))]">
          Quick Navigation
        </h2>
        <div className="grid grid-cols-2 gap-3">
          {QUICK_LINKS.map((link) => (
            <button
              key={link.path}
              type="button"
              onClick={() => navigate(link.path)}
              className="group flex items-start gap-3 border border-[hsl(var(--border))] bg-[hsl(var(--card))] p-4 text-left transition-colors hover:border-[hsl(var(--ring))] hover:bg-[hsl(var(--secondary)/0.3)]"
              style={{ borderRadius: "var(--radius)" }}
            >
              <span className="mt-0.5 shrink-0 text-[hsl(var(--primary))]">{link.icon}</span>
              <div className="min-w-0">
                <p className="text-[13px] font-medium text-[hsl(var(--foreground))]">{link.label}</p>
                <p className="text-[11px] text-[hsl(var(--muted-foreground))] mt-0.5 line-clamp-2">
                  {link.description}
                </p>
              </div>
              <ArrowRight
                size={14}
                weight="bold"
                className="ml-auto mt-0.5 shrink-0 opacity-0 group-hover:opacity-50 transition-opacity text-[hsl(var(--muted-foreground))]"
              />
            </button>
          ))}
        </div>
      </div>

      {/* Analytics widgets */}
      <div>
        <h2 className="mb-3 text-[11px] font-semibold uppercase tracking-widest text-[hsl(var(--muted-foreground))]">
          Analytics Overview
        </h2>
        <div className="grid grid-cols-1 gap-3 sm:grid-cols-2 lg:grid-cols-3">
          <DashboardWidget
            title="Monthly Revenue"
            type="area"
            color="hsl(221, 83%, 48%)"
            data={[
              { name: "Jan", value: 42000 },
              { name: "Feb", value: 38500 },
              { name: "Mar", value: 51200 },
              { name: "Apr", value: 47800 },
              { name: "May", value: 55100 },
              { name: "Jun", value: 62400 },
            ]}
          />
          <DashboardWidget
            title="Documents by Status"
            type="bar"
            color="hsl(173, 58%, 39%)"
            data={[
              { name: "Posted",    value: 84 },
              { name: "Draft",     value: 31 },
              { name: "Cancelled", value: 7  },
            ]}
          />
          <DashboardWidget
            title="Catalog Distribution"
            type="pie"
            data={[
              { name: "Products",        value: 412 },
              { name: "Counterparties",  value: 187 },
              { name: "Warehouses",      value: 24  },
              { name: "Employees",       value: 63  },
            ]}
          />
        </div>
      </div>

      {/* Getting started hint when no metadata */}
      {!tree && (
        <div
          className="border border-[hsl(var(--border))] bg-[hsl(var(--secondary)/0.3)] px-5 py-4 text-[12px] text-[hsl(var(--muted-foreground))]"
          style={{ borderRadius: "var(--radius)" }}
        >
          <p className="font-medium text-[hsl(var(--foreground))] mb-1">No metadata loaded</p>
          <p>Make sure the OES backend is running and accessible. The sidebar will populate with your business objects once connected.</p>
        </div>
      )}
    </div>
  )
}
