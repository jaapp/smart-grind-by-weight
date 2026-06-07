#!/usr/bin/env node

import fs from 'node:fs/promises';

const managerPath = new URL('../src/connectivity/manager.cpp', import.meta.url);
const source = await fs.readFile(managerPath, 'utf8');
const htmlMatch = source.match(/static const char body\[\] = R"HTML\(([\s\S]*?)\)HTML";/);
if (!htmlMatch) {
  throw new Error('Embedded web UI HTML was not found');
}

const html = htmlMatch[1];
const scriptMatch = html.match(/<script>([\s\S]*?)<\/script>/);
if (!scriptMatch) {
  throw new Error('Embedded web UI script was not found');
}

class FakeElement {
  constructor(id) {
    this.id = id;
    this.textContent = '';
    this.className = '';
    this.value = '';
    this.checked = false;
    this.listeners = {};
    this.dataset = {};
    this.children = [];
    this._innerHTML = '';
  }

  addEventListener(type, callback) {
    (this.listeners[type] ||= []).push(callback);
  }

  async dispatch(type) {
    const event = {
      preventDefault() {},
      target: this,
    };
    for (const callback of this.listeners[type] || []) {
      await callback(event);
    }
  }

  dispatchEvent(event) {
    const type = typeof event === 'string' ? event : event.type;
    for (const callback of this.listeners[type] || []) {
      callback(event);
    }
    return true;
  }

  set innerHTML(value) {
    this._innerHTML = String(value);
    this.children = [];
    const buttonRe = /<button\b([^>]*)>([\s\S]*?)<\/button>/g;
    let match;
    while ((match = buttonRe.exec(this._innerHTML))) {
      const child = new FakeElement('button');
      child.textContent = match[2].replace(/<[^>]+>/g, '').trim();
      const dataRe = /data-([a-zA-Z0-9_-]+)="([^"]*)"/g;
      let dataMatch;
      while ((dataMatch = dataRe.exec(match[1]))) {
        const key = dataMatch[1].replace(/-([a-z])/g, (_, c) => c.toUpperCase());
        child.dataset[key] = dataMatch[2];
      }
      this.children.push(child);
    }
  }

  get innerHTML() {
    return this._innerHTML;
  }

  querySelectorAll(selector) {
    return selector === 'button' ? this.children : [];
  }
}

const ids = [...html.matchAll(/id="([^"]+)"/g)].map((match) => match[1]);
const elements = new Map(ids.map((id) => [id, new FakeElement(id)]));
const el = (id) => {
  if (!elements.has(id)) {
    elements.set(id, new FakeElement(id));
  }
  return elements.get(id);
};

const state = {
  settings: {
    wifi_startup: true,
    advanced_ui: true,
    logging_enabled: false,
    swipe_enabled: true,
    auto_start: false,
    auto_return: true,
    basket_detect: false,
    screensaver_startup: false,
    screensaver_sleep: false,
    basket_single_g: 14.2,
    basket_double_g: 17.8,
    basket_tolerance_g: 1.2,
    grind_mode_index: 0,
    purge_mode_index: 1,
    purge_amount_g: 1.0,
    freshness_hours: 72,
    coast_ratio: 0.23,
    brightness_normal: 85,
    brightness_screensaver: 25,
    screensaver_sleep_delay_min: 10,
    screensaver_startup_duration_s: 20,
    basket_configured: true,
    screensaver_image: false,
    screensaver_image_bytes: 0,
  },
  beans: [
    {
      id: 1,
      name: 'Monte Alegre',
      roaster: 'Fjord',
      bag_size_g: 250,
      mahlgrad: 26.5,
      dose_used_g: 56,
      purge_used_g: 2,
      total_used_g: 58,
      active: true,
    },
    {
      id: 2,
      name: 'House Blend',
      roaster: 'Five Elephant',
      bag_size_g: 250,
      mahlgrad: 24,
      dose_used_g: 12.4,
      purge_used_g: 0,
      total_used_g: 12.4,
      active: false,
    },
  ],
  nextBeanId: 3,
  posts: [],
};

function response(body, ok = true, statusText = 'OK') {
  return {
    ok,
    statusText,
    async text() {
      return JSON.stringify(body);
    },
  };
}

function beanPayload() {
  const active = state.beans.find((bean) => bean.active);
  return {
    active_bean_id: active ? active.id : 0,
    capacity: 8,
    beans: state.beans,
  };
}

async function fetchMock(path, options = {}) {
  if (path === '/api/status' || path === '/status') {
    return response({
      status: 'connected',
      mode: 'STA',
      ssid: 'TestNet',
      ip: '127.0.0.1',
      host_url: 'http://grindbyweight.local',
      mac: '00:11:22:33:44:55',
      rssi_dbm: -42,
      connected: true,
      ota_active: false,
      ota_url: '',
      build: 17,
      version: 'test',
    });
  }

  if (path === '/api/settings' && (!options.method || options.method === 'GET')) {
    return response({ settings: state.settings });
  }

  if (path === '/api/settings' && options.method === 'POST') {
    const form = new URLSearchParams(options.body);
    state.posts.push({ path, body: Object.fromEntries(form) });
    for (const [key, value] of form) {
      if ([
        'wifi_startup',
        'advanced_ui',
        'logging_enabled',
        'swipe_enabled',
        'auto_start',
        'auto_return',
        'basket_detect',
        'screensaver_startup',
        'screensaver_sleep',
      ].includes(key)) {
        state.settings[key] = value === '1';
      } else if (value !== '') {
        state.settings[key] = Number.isFinite(Number(value)) ? Number(value) : value;
      }
    }
    return response({ settings: state.settings });
  }

  if (path === '/api/beans' && (!options.method || options.method === 'GET')) {
    return response(beanPayload());
  }

  if (path === '/api/beans' && options.method === 'POST') {
    const form = new URLSearchParams(options.body);
    const action = form.get('action');
    state.posts.push({ path, body: Object.fromEntries(form) });

    if (action === 'create') {
      const bean = {
        id: state.nextBeanId++,
        active: false,
        name: form.get('name') || '',
        roaster: form.get('roaster') || '',
        bag_size_g: Number(form.get('bag_size_g') || 0),
        mahlgrad: Number(form.get('mahlgrad') || 25),
        dose_used_g: 0,
        purge_used_g: 0,
        total_used_g: 0,
      };
      if (!state.beans.some((existing) => existing.active)) {
        bean.active = true;
      }
      state.beans.push(bean);
    } else if (action === 'update') {
      const id = Number(form.get('id'));
      const bean = state.beans.find((existing) => existing.id === id);
      if (bean) {
        Object.assign(bean, {
          name: form.get('name') || '',
          roaster: form.get('roaster') || '',
          bag_size_g: Number(form.get('bag_size_g') || 0),
          mahlgrad: Number(form.get('mahlgrad') || 25),
        });
      }
    } else if (action === 'set_active') {
      const id = Number(form.get('id'));
      state.beans.forEach((bean) => {
        bean.active = bean.id === id;
      });
    } else if (action === 'delete') {
      const id = Number(form.get('id'));
      state.beans = state.beans.filter((bean) => bean.id !== id);
      if (!state.beans.some((bean) => bean.active) && state.beans[0]) {
        state.beans[0].active = true;
      }
    }

    return response(beanPayload());
  }

  if (String(path).startsWith('/api/basket/capture/')) {
    return response({ settings: state.settings });
  }

  return response({ error: `not mocked: ${path}` }, false, 'Not Found');
}

const errors = [];
const document = {
  getElementById: el,
};
const consoleMock = {
  log() {},
  warn(...args) {
    errors.push(['warn', args.join(' ')]);
  },
  error(...args) {
    errors.push(['error', args.join(' ')]);
  },
};

Function('document', 'fetch', 'setInterval', 'console', 'URLSearchParams', scriptMatch[1])(
  document,
  fetchMock,
  () => 0,
  consoleMock,
  URLSearchParams,
);

await new Promise((resolve) => setTimeout(resolve, 10));

const checks = [];
function check(condition, name) {
  checks.push({ name, ok: Boolean(condition) });
}

check(el('statusList').innerHTML.includes('TestNet'), 'status renders WiFi data');
check(el('advanced_ui').checked === true, 'advanced UI checkbox populated');
check(
  el('beanList').innerHTML.includes('Monte Alegre') &&
    el('beanList').innerHTML.includes('House Blend'),
  'bean list renders records',
);

el('bean_name').value = 'New Bean';
el('bean_roaster').value = 'April';
el('bean_bag_size_g').value = '200';
el('bean_mahlgrad').value = '27.5';
await el('beanForm').dispatch('submit');
check(
  state.beans.some((bean) => bean.name === 'New Bean' && bean.mahlgrad === 27.5),
  'bean create posts and refreshes',
);

const setActive = el('beanList').children.find(
  (button) => button.dataset.act === 'active' && button.dataset.id === '2',
);
check(Boolean(setActive), 'set-active button exists');
await setActive.dispatch('click');
check(state.beans.find((bean) => bean.id === 2)?.active === true, 'set active action works');

const deleteBean = el('beanList').children.find(
  (button) => button.dataset.act === 'delete' && button.dataset.id === '3',
);
check(Boolean(deleteBean), 'delete button for new bean exists');
await deleteBean.dispatch('click');
check(!state.beans.some((bean) => bean.id === 3), 'delete action works');

el('advanced_ui').checked = false;
await el('settingsForm').dispatch('submit');
const latestSettingsPost = state.posts.filter((post) => post.path === '/api/settings').at(-1);
check(latestSettingsPost?.body?.advanced_ui === '0', 'settings form posts advanced_ui toggle');
check(errors.length === 0, 'no web UI console errors in harness');

const failed = checks.filter((result) => !result.ok);
for (const result of checks) {
  console.log(`${result.ok ? 'OK' : 'FAIL'} ${result.name}`);
}

if (failed.length > 0) {
  console.error(JSON.stringify({ failed, errors, posts: state.posts }, null, 2));
  process.exit(1);
}
