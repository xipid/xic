import { defineConfig } from "vite";
import vue from "@vitejs/plugin-vue";
import tailwindcss from "@tailwindcss/vite";
import fictif from "fictif/plugin";

export default defineConfig({
  plugins: [
    fictif(),

    vue({
      include: [/\.vue$/, /\.md$/],
      template: {
        compilerOptions: {
          isCustomElement: (tag) => tag.startsWith("mjx-"),
        },
      },
    }),

    tailwindcss(),
  ],
  build: {
    emptyOutDir: false,
  },
});
