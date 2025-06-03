import adapter from '@sveltejs/adapter-auto';
import { vitePreprocess } from '@sveltejs/vite-plugin-svelte';

const dev = process.argv.includes('dev');

const config = {
	preprocess: vitePreprocess(),
	kit: { adapter: adapter({  pages: 'build',
      assets: 'build',
      fallback: null}) ,paths: {
			base: dev ? "" : '/m5myroomnostr',
		
		}}
};

export default config;
