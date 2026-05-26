<script setup lang="ts">
import { useSlots, computed } from "vue";
import MarkdownIt from "markdown-it";
import mathjax3 from "markdown-it-mathjax3";

const md = new MarkdownIt({
  html: true,
  linkify: true,
  typography: true,
}).use(mathjax3);

const slots = useSlots();

const renderedHtml = computed(() => {
  // Extract text from the slot
  const content = (slots.default?.()[0]?.children as string) || "";
  return md.render(content);
});
</script>

<template>
  <div
    v-html="renderedHtml"
    class="prose prose-montserrat max-w-none dark:prose-invert"
  ></div>
</template>

<style scoped>
/* Optional: Ensure MathJax equations don't overflow on small screens */
:deep(.mjx-container) {
  @apply overflow-x-auto overflow-y-hidden py-2;
}
</style>
