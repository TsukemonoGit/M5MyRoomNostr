import adapter from '@sveltejs/adapter-static';
import { vitePreprocess } from '@sveltejs/vite-plugin-svelte';

const dev = process.argv.includes('dev');

const config = {
	preprocess: vitePreprocess(),
	kit: { 
		adapter: adapter(),
		paths: {
			base: dev ? "" : '/m5myroomnostr',
		
		}}
};

export default config;
