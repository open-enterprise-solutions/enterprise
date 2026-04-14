import { Refine, Authenticated } from "@refinedev/core"
import routerBindings from "@refinedev/react-router"
import {
  BrowserRouter,
  Routes,
  Route,
  Outlet,
  Navigate,
  useParams,
  useNavigate,
  useSearchParams,
} from "react-router-dom"
import { oesDataProvider } from "./providers/oes-data-provider"
import { oesAuthProvider } from "./providers/oes-auth-provider"
import { AppShell } from "./components/layout/AppShell"
import { EmbedLayout } from "./components/layout/EmbedLayout"
import { LoginPage } from "./pages/login"
import { DashboardPage } from "./pages/dashboard"
import { ResourceList } from "./pages/resource-list"
import { ReportPage } from "./pages/report-page"
import { RegisterPage } from "./pages/register-page"
import { DesignerPage } from "./pages/designer"
import { OesSchemaForm } from "./components/forms/OesSchemaForm"
import { useTabStore } from "./stores/tab-store"
import { useEventSource } from "./hooks/useEventSource"
import { useCallback } from "react"

// Sections rendered with RegisterView instead of ResourceList
const REGISTER_SECTIONS = new Set([
  "informationRegisters",
  "accumulationRegisters",
  "accountingRegisters",
])

// ---------------------------------------------------------------------------
// SSE initialiser — mounted once inside the authenticated tree
// ---------------------------------------------------------------------------

function SseProvider() {
  // Connects to /api/events, auto-reconnects, dispatches to stores
  useEventSource()
  return null
}

// ---------------------------------------------------------------------------
// Layout wrappers
// ---------------------------------------------------------------------------

function ProtectedLayout() {
  return (
    <Authenticated key="protected-layout" redirectOnFail="/login">
      <SseProvider />
      <AppShell>
        <Outlet />
      </AppShell>
    </Authenticated>
  )
}

function GuestLayout() {
  return (
    <Authenticated key="guest-layout" fallback={<Outlet />}>
      <Navigate to="/" replace />
    </Authenticated>
  )
}

// Embed layout wrapper — no auth check needed (token passed in query param)
function EmbedProtectedLayout() {
  return (
    <Authenticated key="embed-layout" redirectOnFail="/login">
      <EmbedLayout>
        <Outlet />
      </EmbedLayout>
    </Authenticated>
  )
}

// ---------------------------------------------------------------------------
// Embed detection helper
// Reads ?embed=true from any URL and redirects to the /embed/ path
// so the EmbedLayout is applied. Only active in ProtectedLayout routes.
// ---------------------------------------------------------------------------

function EmbedRedirectGuard({ children }: { children: React.ReactNode }) {
  const [searchParams] = useSearchParams()
  const { section, resource, id } = useParams<{
    section?: string
    resource?: string
    id?: string
  }>()

  if (searchParams.get("embed") === "true" && section && resource) {
    const embedPath = id
      ? `/embed/${section}/${resource}/${id}`
      : `/embed/${section}/${resource}`
    return <Navigate to={embedPath} replace />
  }

  return <>{children}</>
}

// ---------------------------------------------------------------------------
// Form route adapters
// ---------------------------------------------------------------------------

function FormCreateRoute() {
  const { section, resource } = useParams<{ section: string; resource: string }>()
  const navigate = useNavigate()
  const { addTab, removeTab } = useTabStore()

  const fullResource = section && resource ? `${section}/${resource}` : ""

  const handleSave = useCallback(
    (values: Record<string, unknown>) => {
      const newId = values?.id as string | undefined
      if (newId && section && resource) {
        const path = `/${section}/${resource}/${newId}`
        removeTab(`${section}/${resource}/create`)
        addTab({
          id: `${section}/${resource}/${newId}`,
          title: String(values.name ?? values.description ?? newId),
          path,
          icon: "file-text",
          closable: true,
        })
        navigate(path, { replace: true })
      } else if (section && resource) {
        navigate(`/${section}/${resource}`, { replace: true })
      }
    },
    [section, resource, navigate, addTab, removeTab],
  )

  const handleCancel = useCallback(() => {
    if (section && resource) {
      removeTab(`${section}/${resource}/create`)
    }
    navigate(-1)
  }, [section, resource, navigate, removeTab])

  if (!section || !resource) return null

  return (
    <OesSchemaForm
      resource={fullResource}
      onSave={handleSave}
      onCancel={handleCancel}
    />
  )
}

function FormEditRoute() {
  const { section, resource, id } = useParams<{
    section: string
    resource: string
    id: string
  }>()
  const navigate = useNavigate()

  if (!section || !resource || !id) return null

  const fullResource = `${section}/${resource}`

  return (
    <OesSchemaForm
      resource={fullResource}
      id={id}
      onSave={() => {
        // Stay on the same page after save
      }}
      onCancel={() => navigate(-1)}
    />
  )
}

// Embed variants — same forms, no tab management, no AppShell navigation
function EmbedListRoute() {
  const { section, resource } = useParams<{ section: string; resource: string }>()
  if (!section || !resource) return null
  return <ResourceList />
}

function EmbedEditRoute() {
  const { section, resource, id } = useParams<{
    section: string
    resource: string
    id: string
  }>()
  const navigate = useNavigate()

  if (!section || !resource || !id) return null

  return (
    <OesSchemaForm
      resource={`${section}/${resource}`}
      id={id}
      onSave={() => { /* stay in place */ }}
      onCancel={() => navigate(-1)}
    />
  )
}

// ---------------------------------------------------------------------------
// App
// ---------------------------------------------------------------------------

// All known metadata section keys — used to register Refine resources
const OES_SECTIONS = [
  "catalogs",
  "documents",
  "enumerations",
  "informationRegisters",
  "accumulationRegisters",
  "accountingRegisters",
  "reports",
  "dataProcessors",
  "constants",
]

export default function App() {
  return (
    <BrowserRouter>
      <Refine
        dataProvider={oesDataProvider}
        authProvider={oesAuthProvider}
        routerProvider={routerBindings}
        resources={OES_SECTIONS.map((name) => ({
          name,
          list: `/${name}`,
        }))}
        options={{
          syncWithLocation: true,
          warnWhenUnsavedChanges: true,
        }}
      >
        <Routes>
          {/* Guest-only routes */}
          <Route element={<GuestLayout />}>
            <Route path="/login" element={<LoginPage />} />
          </Route>

          {/* Designer — standalone layout, authenticated */}
          <Route
            path="/designer"
            element={
              <Authenticated key="designer-layout" redirectOnFail="/login">
                <DesignerPage />
              </Authenticated>
            }
          />

          {/* Embed routes — EmbedLayout, authenticated */}
          <Route element={<EmbedProtectedLayout />}>
            <Route path="/embed/:section/:resource"     element={<EmbedListRoute />} />
            <Route path="/embed/:section/:resource/:id" element={<EmbedEditRoute />} />
          </Route>

          {/* Protected routes — rendered inside AppShell */}
          <Route element={<ProtectedLayout />}>
            {/* Dashboard */}
            <Route index element={<DashboardPage />} />

            {/* Generic list view: /:section/:resource */}
            <Route
              path="/:section/:resource"
              element={
                <EmbedRedirectGuard>
                  <ResourceListRoute />
                </EmbedRedirectGuard>
              }
            />

            {/* Report viewer: /:section/:resource/report */}
            <Route
              path="/:section/:resource/report"
              element={<ReportPage />}
            />

            {/* Create form */}
            <Route
              path="/:section/:resource/create"
              element={<FormCreateRoute />}
            />

            {/* Edit form */}
            <Route
              path="/:section/:resource/:id"
              element={
                <EmbedRedirectGuard>
                  <FormEditRoute />
                </EmbedRedirectGuard>
              }
            />

            <Route path="*" element={<Navigate to="/" replace />} />
          </Route>
        </Routes>
      </Refine>
    </BrowserRouter>
  )
}

// Guard: skip list route if `:resource` is literally "create".
// For register sections, render RegisterPage; for reports, ResourceList still shows the list.
function ResourceListRoute() {
  const { section = "", resource } = useParams<{ section: string; resource: string }>()
  if (resource === "create") return <Navigate to="/" replace />
  if (REGISTER_SECTIONS.has(section)) return <RegisterPage />
  return <ResourceList />
}
