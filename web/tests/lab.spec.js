import { test, expect } from '@playwright/test';

async function ready(page) {
  await page.goto('/');
  await expect(page.locator('#connection')).toContainText('connected');
  await expect(page.locator('#sample')).toBeEnabled();
}
async function sample(page) {
  await page.locator('#sample').click();
  await expect(page.locator('#status')).toContainText('Ready.');
  await expect(page.locator('#step')).toBeEnabled();
}

test('hydrogen numerical results, WebGL rendering, keyboard camera and mobile layout', async ({ page }, info) => {
  const errors = [];
  page.on('pageerror', error => errors.push(error.message));
  await ready(page);
  await page.locator('#backend').selectOption('cpu');
  await sample(page);
  await expect(page.locator('#active-state')).toHaveText('H / 1s / m = 0');
  expect(Number(await page.locator('#energy').textContent())).toBeCloseTo(-13.6, 1);
  expect(Number(await page.locator('#radius').textContent())).toBeCloseTo(1.5, 1);
  await expect(page.locator('#preview')).toContainText('20,000 shown / 50,000 sampled');
  await expect(page.locator('#renderer-error')).toBeHidden();
  const pixels = await page.evaluate(() => {
    document.getElementById('reset-camera').click();
    const canvas = document.getElementById('cloud');
    const gl = canvas.getContext('webgl');
    const data = new Uint8Array(canvas.width * canvas.height * 4);
    gl.readPixels(0, 0, canvas.width, canvas.height, gl.RGBA, gl.UNSIGNED_BYTE, data);
    let colored = 0;
    for (let i = 0; i < data.length; i += 4) if (data[i] > 40 || data[i + 1] > 50) colored++;
    return { colored, error: gl.getError() };
  });
  expect(pixels.error).toBe(0);
  expect(pixels.colored).toBeGreaterThan(2000);
  await page.locator('#cloud').focus();
  await page.keyboard.press('ArrowRight');
  await page.keyboard.press('+');
  await page.screenshot({ path: info.outputPath('orbital-lab-desktop.png'), fullPage: true });
  await page.setViewportSize({ width: 390, height: 844 });
  expect(await page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth)).toBe(true);
  await page.screenshot({ path: info.outputPath('orbital-lab-mobile.png'), fullPage: true });
  expect(errors).toEqual([]);
});

test('gold screening and clear input/state separation', async ({ page }) => {
  await ready(page);
  await page.locator('#element').selectOption('79');
  await expect(page.locator('#atomic-number')).toHaveValue('79');
  await page.locator('#n').fill('6');
  await page.locator('#screening').selectOption('slater-neutral');
  await sample(page);
  await expect(page.locator('#active-state')).toHaveText('Au / 6s / m = 0');
  await expect(page.locator('#charge')).toHaveText('3.7');
  await expect(page.locator('#interpretation')).toContainText('Slater screening is approximate');
  await page.locator('#seed').fill('23');
  await expect(page.locator('#status')).toContainText('Inputs changed');
  await expect(page.locator('#step')).toBeDisabled();
  await expect(page.locator('#active-state')).toHaveText('Au / 6s / m = 0');
});

test('backend-driven current, play/pause, and stationary m=0 state', async ({ page }) => {
  await ready(page);
  await page.locator('#samples').fill('1000');
  await page.locator('#n').fill('2');
  await page.locator('#l').fill('1');
  await page.locator('#m').fill('-1');
  await sample(page);
  await page.locator('#step').click();
  await expect(page.locator('#time')).toHaveText('0.1');
  await page.locator('#play').click();
  await expect(page.locator('#play')).toHaveText('Pause');
  await expect.poll(async () => Number(await page.locator('#time').textContent())).toBeGreaterThan(0.1);
  await page.locator('#play').click();
  await expect(page.locator('#play')).toHaveText('Play');
  await expect(page.locator('#step')).toBeEnabled();
  await page.locator('#m').fill('0');
  await sample(page);
  await expect(page.locator('#flow-description')).toContainText('zero current');
  await page.locator('#step').click();
  await expect(page.locator('#time')).toHaveText('0.1');
});

test('screening errors retain prior results and mark the failing field', async ({ page }) => {
  await ready(page);
  await sample(page);
  await page.locator('#n').fill('2');
  await page.locator('#l').fill('1');
  await page.locator('#screening').selectOption('slater-neutral');
  await page.locator('#sample').click();
  await expect(page.locator('#error')).toContainText('unoccupied');
  await expect(page.locator('#screening')).toHaveAttribute('aria-invalid', 'true');
  await expect(page.locator('#active-state')).toHaveText('H / 1s / m = 0');
  await page.locator('#screening').selectOption('pure-z');
  await sample(page);
  await expect(page.locator('#error')).toBeHidden();
  await expect(page.locator('#active-state')).toHaveText('H / 2p / m = 0');
});

test('fractional inputs are blocked and unavailable models stay disabled', async ({ page }) => {
  await ready(page);
  let submissions = 0;
  page.on('request', request => { if (request.url().endsWith('/api/sample')) submissions++; });
  await page.locator('#samples').fill('1.5');
  await page.locator('#sample').click();
  expect(submissions).toBe(0);
  expect(await page.locator('#samples').evaluate(input => input.validity.stepMismatch)).toBe(true);
  await expect(page.locator('#model option:disabled')).toHaveCount(1);
});

test('offline recovery works without fabricated results', async ({ page }) => {
  await page.route('**/api/catalog', route => route.abort());
  await page.goto('/');
  await expect(page.locator('#error')).toContainText('Could not reach the local backend');
  await expect(page.locator('#sample')).toBeDisabled();
  await expect(page.locator('#energy')).toHaveText('—');
  await page.unroute('**/api/catalog');
  await page.locator('#reconnect').click();
  await expect(page.locator('#sample')).toBeEnabled();
  await sample(page);
});

test('another run invalidates the old browser handle', async ({ page, request }) => {
  await ready(page);
  await sample(page);
  const response = await request.post('/api/sample', { data: { contractVersion: 1, model: 'hydrogenic-orbital', element: { atomicNumber: 1 }, state: { n: 1, l: 0, m: 0 }, screening: 'pure-z', sampleCount: 100, seed: 42, backend: 'cpu' } });
  expect(response.ok()).toBe(true);
  await page.locator('#step').click();
  await expect(page.locator('#error')).toContainText('no longer active');
  await expect(page.locator('#step')).toBeDisabled();
});

test('coherent density changes with time and pure-Z constraints are explicit', async ({ page }) => {
  await ready(page);
  await page.locator('#try-interference').click();
  await expect(page.locator('#status')).toContainText('Coherent two-state density');
  await expect(page.locator('#active-state')).toHaveText('H / 1s + 2p / interference');
  await expect(page.locator('#screening')).toBeDisabled();
  await expect(page.locator('#dynamics-title')).toHaveText('Time-dependent interference');
  expect(Number(await page.locator('#centroid').textContent())).toBeGreaterThan(0.5);
  await page.locator('#dt').fill(String(Math.PI / 0.375));
  await page.locator('#step').click();
  await expect.poll(async () => Number(await page.locator('#centroid').textContent())).toBeLessThan(-0.5);
  await expect(page.locator('#interpretation')).toContainText('not tracked trajectories');
});
