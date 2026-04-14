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
} from "react-router-dom"
import { oesDataProvider } from "./providers/oes-data-provider"
import { oesAuthProvider } from "./providers/oes-auth-provider"
import { AppShell } from "./components/layout/AppShell"
import { LoginPage } from "./pages/login"
import { DashboardPage } from "./pages/dashboard"
import { ResourceList } from "./pages/resource-list"
import { OesSchemaForm } from "./components/forms/OesSchemaForm"
import { useTabStore } from "./stores/tab-store"
import { useCallback } from "react"

// ---------------------------------------------------------------------------
// Layout wrappers
// ---------------------------------------------------------------------------

function ProtectedLayout() {
  return (
    <Authenticated key="protected-layout" redirectOnFail="/login">
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
        // Replace create tab with the new edit tab
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

          {/* Protected routes — rendered inside AppShell */}
          <Route element={<ProtectedLayout />}>
            {/* Dashboard */}
            <Route index element={<DashboardPage />} />

            {/* Generic list view: /:section/:resource */}
            <Route
              path="/:section/:resource"
              element={<ResourceListRoute />}
            />

            {/* Create form */}
            <Route
              path="/:section/:resource/create"
              element={<FormCreateRoute />}
            />

            {/* Edit form */}
            <Route
              path="/:section/:resource/:id"
              element={<FormEditRoute />}
            />

            <Route path="*" element={<Navigate to="/" replace />} />
          </Route>
        </Routes>
      </Refine>
    </BrowserRouter>
  )
}

// Guard: skip list route if `:resource` is literally "create"
function ResourceListRoute() {
  const { resource } = useParams<{ resource: string }>()
  if (resource === "create") return <Navigate to="/" replace />
  return <ResourceList />
}
