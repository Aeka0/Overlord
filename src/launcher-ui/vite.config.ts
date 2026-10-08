import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";
import { fileURLToPath } from "node:url";

export default defineConfig(({ command }) => ({
  plugins: [
    react(),
    {
      name: "launcher-development-csp",
      transformIndexHtml(html) {
        return command === "serve"
          ? html.replace(/<meta http-equiv="Content-Security-Policy"[^>]*>/, "")
          : html;
      },
    },
  ],
  server: {
    host: "127.0.0.1",
    strictPort: true,
    port: 5173,
    fs: {
      allow: [
        fileURLToPath(new URL(".", import.meta.url)),
        fileURLToPath(new URL("../client/resources/launcher", import.meta.url)),
      ],
    },
  },
  build: {
    target: "es2022",
    outDir: "../../build/launcher-ui/dist",
    emptyOutDir: true,
  },
}));
