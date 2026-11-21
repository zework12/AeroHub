// Shared helpers for every page.
async function api(path, body) {
  const opt = body === undefined ? {} : { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) };
  const r = await fetch(path, opt);
  let data = {};
  try { data = await r.json(); } catch { /* empty body */ }
  return { ok: r.ok, status: r.status, data };
}

function el(tag, attrs = {}, ...children) {
  const e = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) {
    if (k === 'class') e.className = v;
    else if (k.startsWith('on')) e.addEventListener(k.slice(2), v);
    else e.setAttribute(k, v);
  }
  for (const c of children) e.append(c instanceof Node ? c : document.createTextNode(String(c ?? '')));
  return e;
}

function fmtDate(ms) {
  return ms ? new Date(ms).toLocaleDateString('ru-RU', { day: '2-digit', month: '2-digit', year: 'numeric' }) : '—';
}

const STATUS_RU = { active: 'Активен', expired: 'Истёк', revoked: 'Отозван', free: 'Свободен' };

const ICON_COPY = '<svg class="i" viewBox="0 0 24 24"><rect x="9" y="9" width="12" height="12" rx="2"/><path d="M5 15V5a2 2 0 0 1 2-2h10"/></svg>';
const ICON_EYE = '<svg class="i" viewBox="0 0 24 24"><path d="M2 12s3.5-7 10-7 10 7 10 7-3.5 7-10 7S2 12 2 12z"/><circle cx="12" cy="12" r="3"/></svg>';

function toast(text) {
  let t = document.querySelector('.toast');
  if (!t) document.body.append(t = el('div', { class: 'toast', role: 'status', 'aria-live': 'polite' }));
  t.textContent = text;
  t.classList.add('show');
  clearTimeout(t._h);
  t._h = setTimeout(() => t.classList.remove('show'), 2400);
}

function copyButton(text) {
  const b = el('button', { class: 'copy', type: 'button', 'aria-label': 'Скопировать', title: 'Скопировать' });
  b.innerHTML = ICON_COPY;
  b.addEventListener('click', async () => {
    try { await navigator.clipboard.writeText(text); toast('Скопировано'); } catch { toast('Не удалось скопировать'); }
  });
  return b;
}

// Header: "Войти" or the account link, depending on the session.
async function initHeader() {
  const slot = document.getElementById('auth-slot');
  if (!slot) return null;
  const me = await api('/api/me');
  slot.replaceChildren(me.ok ? el('a', { class: 'btn small', href: '/account' }, me.data.username)
                             : el('a', { class: 'btn small primary', href: '/login' }, 'Войти'));
  return me.ok ? me.data : null;
}

function bindForm(form, handler) {
  const msg = form.querySelector('.msg');
  if (msg) { msg.setAttribute('role', 'alert'); msg.setAttribute('aria-live', 'polite'); }
  form.addEventListener('submit', async (ev) => {
    ev.preventDefault();
    const btn = form.querySelector('button[type=submit]');
    btn.disabled = true;
    btn.classList.add('loading');
    msg.className = 'msg';
    msg.textContent = '';
    try {
      const text = await handler(Object.fromEntries(new FormData(form)));
      if (text) { msg.className = 'msg ok'; msg.textContent = text; }
    } catch (e) {
      msg.className = 'msg err';
      msg.textContent = e.message;
    } finally {
      btn.disabled = false;
      btn.classList.remove('loading');
    }
  });
}

// ---- page polish: header state, scroll reveal, card spotlight, password toggles, soft page transitions
(function polish() {
  const reduce = matchMedia('(prefers-reduced-motion: reduce)').matches;
  const header = document.querySelector('header.top');
  const onScroll = () => header && header.classList.toggle('scrolled', scrollY > 8);
  addEventListener('scroll', onScroll, { passive: true });
  onScroll();

  const items = document.querySelectorAll('.reveal');
  if (reduce || !('IntersectionObserver' in window)) items.forEach(e => e.classList.add('in'));
  else {
    const io = new IntersectionObserver((entries) => {
      for (const en of entries) if (en.isIntersecting) { en.target.classList.add('in'); io.unobserve(en.target); }
    }, { threshold: 0.12, rootMargin: '0px 0px -40px 0px' });
    items.forEach(e => io.observe(e));
  }

  document.addEventListener('pointermove', (ev) => {
    const c = ev.target.closest && ev.target.closest('.card');
    if (!c) return;
    const r = c.getBoundingClientRect();
    c.style.setProperty('--mx', `${ev.clientX - r.left}px`);
    c.style.setProperty('--my', `${ev.clientY - r.top}px`);
  }, { passive: true });

  for (const input of document.querySelectorAll('input[type=password]')) {
    const box = el('div', { class: 'pw' });
    input.replaceWith(box);
    const b = el('button', { type: 'button', 'aria-label': 'Показать пароль' });
    b.innerHTML = ICON_EYE;
    b.addEventListener('click', () => {
      const show = input.type === 'password';
      input.type = show ? 'text' : 'password';
      b.setAttribute('aria-label', show ? 'Скрыть пароль' : 'Показать пароль');
    });
    box.append(input, b);
  }

  if (!reduce) document.addEventListener('click', (ev) => {
    const a = ev.target.closest && ev.target.closest('a[href^="/"]');
    if (!a || a.target || ev.metaKey || ev.ctrlKey || ev.shiftKey || a.getAttribute('href').startsWith('/download')) return;
    ev.preventDefault();
    document.body.classList.add('leaving');
    setTimeout(() => { location.href = a.href; }, 170);
  });
  addEventListener('pageshow', () => document.body.classList.remove('leaving'));
})();
