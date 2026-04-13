import axios from "axios"

const API_BASE = import.meta.env.VITE_API_BASE_URL || "/api"

export const api = axios.create({
  baseURL: API_BASE,
  timeout: Number(import.meta.env.VITE_API_TIMEOUT) || 30000,
  headers: {
    "Content-Type": "application/json",
  },
})

api.interceptors.request.use((config) => {
  const token = localStorage.getItem("oes_token")
  if (token) {
    config.headers.Authorization = `Bearer ${token}`
  }
  return config
})

api.interceptors.response.use(
  (response) => response,
  (error) => {
    if (error.response?.status === 401) {
      localStorage.removeItem("oes_token")
      localStorage.removeItem("oes_user")
      window.location.href = "/login"
    }
    return Promise.reject(error)
  },
)

export const getApiUrl = () => API_BASE
