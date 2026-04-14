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
import { toApiResource } from "@/lib/resource-utils"
import { AppShell } from "./components/layout/AppShell"
import { LoginPage } from "./pages/login"
import { useTabStore } from "./stores/tab-store"
import { useCallback, lazy, Suspense } from "react"

// Lazy load heavy pages (Monaco Editor = 2MB, Recharts, Formily)
const DashboardPage = lazy(() => import("./pages/dashboard").then(m => ({ default: m.DashboardPage })))
const ResourceList = lazy(() => import("./pages/resource-list").then(m => ({ default: m.ResourceList })))
const ReportPage = lazy(() => import("./pages/report-page").then(m => ({ default: m.ReportPage })))
const RegisterPage = lazy(() => import("./pages/register-page").then(m => ({ default: m.RegisterPage })))
const DesignerPage = lazy(() => import("./pages/designer").then(m => ({ default: m.DesignerPage })))
const DebuggerPage = lazy(() => import("./pages/debugger").then(m => ({ default: m.DebuggerPage })))
const OesSchemaForm = lazy(() => import("./components/forms/OesSchemaForm").then(m => ({ default: m.OesSchemaForm })))
const AiConfigGeneratorPage = lazy(() => import("./pages/ai-config-generator").then(m => ({ default: m.AiConfigGeneratorPage })))
const PluginManager = lazy(() => import("./components/plugins/PluginManager").then(m => ({ default: m.PluginManager })))
const EmbedLayout = lazy(() => import("./components/layout/EmbedLayout").then(m => ({ default: m.EmbedLayout })))

// Sections rendered with RegisterView instead of ResourceList
const REGISTER_SECTIONS = new Set([
  "informationRegisters",
  "accumulationRegisters",
  "accountingRegisters",
])

// ---------------------------------------------------------------------------
// Layout wrappers
// ---------------------------------------------------------------------------

function ProtectedLayout() {
  return (
    <Authenticated key="protected-layout" redirectOnFail="/login">
      {/* SseProvider and AiAssistant disabled until backend is running */}
      {/* <SseProvider /> */}
      <AppShell>
        <Suspense fallback={<div className="flex h-full items-center justify-center"><div className="skeleton h-8 w-48" /></div>}>
          <Outlet />
        </Suspense>
      </AppShell>
      {/* <AiAssistant /> */}
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

  const fullResource = section && resource ? toApiResource(section, resource) : ""

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

  const fullResource = toApiResource(section, resource)

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
      resource={toApiResource(section, resource)}
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

          {/* Debugger — standalone layout, authenticated */}
          <Route
            path="/debugger"
            element={
              <Authenticated key="debugger-layout" redirectOnFail="/login">
                <DebuggerPage />
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

            {/* AI Config Generator */}
            <Route path="/ai" element={<AiConfigGeneratorPage />} />

            {/* Plugin Manager */}
            <Route path="/plugins" element={<PluginManager />} />

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
