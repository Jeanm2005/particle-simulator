import { defineConfig } from '@playwright/test';

export default defineConfig({
  testDir: './tests',
  workers: 1, // The local backend intentionally owns one active run.
  timeout: 30000,
  use: {
    baseURL: 'http://127.0.0.1:8765',
    viewport: { width: 1440, height: 1080 },
    trace: 'retain-on-failure',
    launchOptions: { args: ['--use-angle=swiftshader', '--enable-unsafe-swiftshader'] },
  },
  webServer: {
    command: 'python3 ../tools/serve.py --port 8765',
    url: 'http://127.0.0.1:8765',
    reuseExistingServer: false,
    timeout: 15000,
  },
});
