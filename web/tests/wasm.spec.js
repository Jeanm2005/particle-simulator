import { test, expect } from '@playwright/test';
async function ready(page) {
  await page.goto('/');
  await expect(page.locator('#connection')).toContainText('browser engine connected', { timeout: 30000 });
}
async function sample(page) {
  await page.locator('#sample').click();
  await expect(page.locator('#status')).toContainText('Ready.');
}
test('public demo executes real C++ physics without an HTTP backend', async ({ page, request }, info) => {
  const apiRequests = [], errors = [];
  page.on('request', request => { if (request.url().includes('/api/')) apiRequests.push(request.url()); });
  page.on('pageerror', error => errors.push(error.message));
  await ready(page);
  await expect(page.getByText('Provisional demo · in development', { exact: true })).toBeVisible();
  await sample(page);
  expect(Number(await page.locator('#radius').textContent())).toBeCloseTo(1.5, 1);
  expect(Number(await page.locator('#energy').textContent())).toBeCloseTo(-13.6057, 4);
  await expect(page.locator('#renderer-error')).toBeHidden();
  await page.locator('#element').selectOption('79');
  await page.locator('#n').fill('6');
  await page.locator('#screening').selectOption('slater-neutral');
  await sample(page);
  await expect(page.locator('#charge')).toHaveText('3.7');
  const native = await request.post('http://127.0.0.1:8765/api/sample', { data: { contractVersion: 1, model: 'hydrogenic-orbital', element: { atomicNumber: 79 }, state: { n: 6, l: 0, m: 0 }, screening: 'slater-neutral', sampleCount: 50000, seed: 42, backend: 'cpu' } });
  const reference = await native.json();
  expect(Math.abs(Number(await page.locator('#radius').textContent()) - reference.meanRadiusA0)).toBeLessThan(0.3);
  await page.locator('#try-interference').click();
  await expect(page.locator('#status')).toContainText('Coherent two-state density');
  expect(Number(await page.locator('#centroid').textContent())).toBeGreaterThan(0.5);
  await page.screenshot({ path: info.outputPath('recruiter-interference-demo.png'), fullPage: true });
  await page.locator('#dt').fill(String(Math.PI / 0.375));
  await page.locator('#step').click();
  await expect.poll(async () => Number(await page.locator('#centroid').textContent())).toBeLessThan(-0.5);
  await page.locator('#play').click();
  await expect(page.locator('#play')).toHaveText('Pause');
  await page.locator('#play').click();
  expect(apiRequests).toEqual([]);
  expect(errors).toEqual([]);
});

test('visitors have independent state and invalid combinations show errors', async ({ browser, page }) => {
  await ready(page);
  await sample(page);
  const other = await browser.newContext();
  const second = await other.newPage();
  await second.goto('http://127.0.0.1:8766');
  await expect(second.locator('#connection')).toContainText('browser engine connected', { timeout: 30000 });
  await second.locator('#try-interference').click();
  await expect(second.locator('#active-state')).toHaveText('H / 1s + 2p / interference');
  await page.locator('#step').click();
  await expect(page.locator('#time')).toHaveText('0.1');
  await expect(page.locator('#error')).toBeHidden();
  await second.locator('#second-n').fill('1');
  await second.locator('#second-l').fill('0');
  await second.locator('#sample').click();
  await expect(second.locator('#error')).toContainText('distinct orthogonal');
  await expect(second.locator('#second-n')).toHaveAttribute('aria-invalid', 'true');
  await other.close();
});

test('browser current preserves native model semantics and mobile layout', async ({ page }) => {
  await ready(page);
  await page.locator('#n').fill('2');
  await page.locator('#l').fill('1');
  await page.locator('#m').fill('1');
  await page.locator('#samples').fill('1000');
  await sample(page);
  const radius = await page.locator('#radius').textContent();
  await page.locator('#step').click();
  await expect(page.locator('#time')).toHaveText('0.1');
  await expect(page.locator('#radius')).toHaveText(radius);
  await page.setViewportSize({ width: 390, height: 844 });
  expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth)).toBe(true);
});
