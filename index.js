import { mount } from 'svelte';
import App from './App.svelte';
import './index.css';

// Svelte owns the interface state; this entry point only mounts the app.
mount(App, { target: document.getElementById('app') });
