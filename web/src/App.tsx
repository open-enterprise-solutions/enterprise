import { Refine, Authenticated } from "@refinedev/core"
import routerBindings from "@refinedev/react-router"
import { BrowserRouter, Routes, Route, Outlet, Navigate, useParams, useNavigate } from "react-router-dom"
import { oesDataProvider } from "./providers/oes-data-provider"
import { oesAuthProvider } from "./providers/oes-auth-provider"
import { AppShell } from "./components/layout/AppShell"
import { LoginPage } from "./pages/login"
import { OesSchemaForm } from "./components/forms/OesSchemaForm"

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
// Paths:  /:section/:resource/create   → create mode
//         /:section/:resource/:id       → edit mode
// The `resource` param is joined with `section` to form a dot-notation key,
// e.g. section="catalogs", resource="products" → "catalogs.products"
// ---------------------------------------------------------------------------

function FormCreateRoute() {
  const { section, resource } = useParams<{ section: string; resource: string }>()
  const navigate = useNavigate()

  if (!section || !resource) return null

  const fullResource = `${section}.${resource}`

  return (
    <OesSchemaForm
      resource={fullResource}
      onSave={(values) => {
        // After create, redirect to edit page with the new id
        const newId = values?.id as string | undefined
        if (newId) {
          navigate(`/${section}/${resource}/${newId}`, { replace: true })
        } else {
          navigate(`/${section}/${resource}`, { replace: true })
        }
      }}
      onCancel={() => navigate(-1)}
    />
  )
}

function FormEditRoute() {
  const { section, resource, id } = useParams<{ section: string; resource: string; id: string }>()
  const navigate = useNavigate()

  if (!section || !resource || !id) return null

  const fullResource = `${section}.${resource}`

  return (
    <OesSchemaForm
      resource={fullResource}
      id={id}
      onSave={() => {
        // Stay on the same page after save — the form will reflect updated values
      }}
      onCancel={() => navigate(-1)}
    />
  )
}

// ---------------------------------------------------------------------------
// App
// ---------------------------------------------------------------------------

export default function App() {
  return (
    <BrowserRouter>
      <Refine
        dataProvider={oesDataProvider}
        authProvider={oesAuthProvider}
        routerProvider={routerBindings}
        resources={[
          { name: "catalogs", list: "/catalogs" },
          { name: "documents", list: "/documents" },
          { name: "reports", list: "/reports" },
        ]}
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
            <Route index element={null} />

            {/* Section list pages (Phase 4 will add list components here) */}
            <Route path="/catalogs" element={null} />
            <Route path="/documents" element={null} />
            <Route path="/reports" element={null} />

            {/* Form routes — generic: /:section/:resource/create and /:section/:resource/:id */}
            <Route path="/:section/:resource/create" element={<FormCreateRoute />} />
            <Route path="/:section/:resource/:id" element={<FormEditRoute />} />

            <Route path="*" element={null} />
          </Route>
        </Routes>
      </Refine>
    </BrowserRouter>
  )
}
