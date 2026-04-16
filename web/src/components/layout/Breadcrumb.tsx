import { Link, useLocation } from "react-router-dom"
import { CaretRight } from "@phosphor-icons/react"

const SEGMENT_LABELS: Record<string, string> = {
  catalogs:              "Catalogs",
  documents:             "Documents",
  enumerations:          "Enumerations",
  informationRegisters:  "Info Registers",
  accumulationRegisters: "Accum. Registers",
  accountingRegisters:   "Accounting Registers",
  reports:               "Reports",
  dataProcessors:        "Data Processors",
  constants:             "Constants",
  settings:              "Settings",
  designer:              "Designer",
  ai:                    "AI Generator",
  plugins:               "Plugins",
  new:                   "New",
  edit:                  "Edit",
}

function labelFor(segment: string): string {
  return SEGMENT_LABELS[segment] ?? segment
}

interface Crumb {
  label: string
  path: string
}

function buildCrumbs(pathname: string): Crumb[] {
  if (pathname === "/" || pathname === "") return []

  const parts = pathname.split("/").filter(Boolean)
  const crumbs: Crumb[] = [{ label: "Home", path: "/" }]

  let accumulated = ""
  for (const part of parts) {
    accumulated += `/${part}`
    crumbs.push({ label: labelFor(part), path: accumulated })
  }

  return crumbs
}

export function Breadcrumb() {
  const { pathname } = useLocation()
  const crumbs = buildCrumbs(pathname)

  if (crumbs.length <= 1) return null

  return (
    <nav
      aria-label="Breadcrumb"
      className="flex items-center gap-1 px-4 h-7 border-b border-[hsl(var(--border))] bg-[hsl(var(--background))] shrink-0"
    >
      {crumbs.map((crumb, i) => {
        const isLast = i === crumbs.length - 1
        return (
          <span key={crumb.path} className="flex items-center gap-1">
            {i > 0 && (
              <CaretRight
                size={10}
                weight="bold"
                className="text-[hsl(var(--muted-foreground)/0.5)] shrink-0"
              />
            )}
            {isLast ? (
              <span className="text-[11px] text-[hsl(var(--foreground))] font-medium truncate max-w-[180px]">
                {crumb.label}
              </span>
            ) : (
              <Link
                to={crumb.path}
                className="text-[11px] text-[hsl(var(--muted-foreground))] hover:text-[hsl(var(--foreground))] transition-colors truncate max-w-[180px]"
              >
                {crumb.label}
              </Link>
            )}
          </span>
        )
      })}
    </nav>
  )
}
