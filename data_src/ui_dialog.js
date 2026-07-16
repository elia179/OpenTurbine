(function () {
  'use strict';

  let activeResolve = null;
  let previousFocus = null;

  function ensureDialog() {
    let overlay = document.getElementById('ot-app-dialog');
    if (overlay) return overlay;

    const style = document.createElement('style');
    style.textContent = `
      .ot-dialog-overlay{position:fixed;inset:0;z-index:10000;display:none;align-items:center;justify-content:center;padding:1rem;background:rgba(0,0,0,.76)}
      .ot-dialog-overlay.show{display:flex}
      .ot-dialog-card{width:min(620px,96vw);max-height:88vh;display:flex;flex-direction:column;background:var(--surface,#17171a);color:var(--text,#f5f5f7);border:1px solid var(--border-light,#42424a);border-radius:10px;box-shadow:0 20px 70px rgba(0,0,0,.55)}
      .ot-dialog-header{padding:1rem 1.1rem .7rem;font-size:.9rem;font-weight:800;letter-spacing:.04em}
      .ot-dialog-message{padding:.25rem 1.1rem 1rem;color:var(--text-2,#cecdd4);font-size:.82rem;line-height:1.55;white-space:pre-wrap;overflow:auto}
      .ot-dialog-input{margin:0 1.1rem 1rem;width:calc(100% - 2.2rem);min-height:44px;padding:.55rem .7rem;background:var(--bg,#101012);color:var(--text,#f5f5f7);border:1px solid var(--border-light,#42424a);border-radius:6px;font:inherit}
      .ot-dialog-actions{display:flex;justify-content:flex-end;gap:.6rem;padding:.85rem 1.1rem;border-top:1px solid var(--border,#313135)}
      .ot-dialog-actions button{min-height:44px}
      .ot-dialog-actions .danger{background:var(--red,#ff4d5f);border-color:var(--red,#ff4d5f);color:#fff}
      @media(max-width:520px){.ot-dialog-overlay{align-items:flex-end;padding:.6rem}.ot-dialog-card{width:100%;max-height:92vh}.ot-dialog-actions{flex-wrap:wrap}.ot-dialog-actions button{flex:1 1 120px}}
    `;
    document.head.appendChild(style);

    overlay = document.createElement('div');
    overlay.id = 'ot-app-dialog';
    overlay.className = 'ot-dialog-overlay';
    overlay.setAttribute('role', 'dialog');
    overlay.setAttribute('aria-modal', 'true');
    overlay.innerHTML = `
      <div class="ot-dialog-card">
        <div class="ot-dialog-header" id="ot-dialog-title"></div>
        <div class="ot-dialog-message" id="ot-dialog-message"></div>
        <input class="ot-dialog-input" id="ot-dialog-input" type="text" style="display:none">
        <div class="ot-dialog-actions">
          <button type="button" id="ot-dialog-cancel" style="display:none">Cancel</button>
          <button type="button" id="ot-dialog-confirm">OK</button>
        </div>
      </div>`;
    document.body.appendChild(overlay);
    overlay.addEventListener('keydown', event => {
      if (event.key === 'Escape' && document.getElementById('ot-dialog-cancel').style.display !== 'none') finish(false);
    });
    document.getElementById('ot-dialog-cancel').addEventListener('click', () => finish(false));
    document.getElementById('ot-dialog-confirm').addEventListener('click', () => finish(true));
    return overlay;
  }

  function finish(accepted) {
    const overlay = document.getElementById('ot-app-dialog');
    if (!overlay) return;
    const input = document.getElementById('ot-dialog-input');
    const value = accepted ? (input.style.display === 'none' ? true : input.value) : false;
    overlay.classList.remove('show');
    if (previousFocus && typeof previousFocus.focus === 'function') previousFocus.focus();
    const resolve = activeResolve;
    activeResolve = null;
    if (resolve) resolve(value);
  }

  function open(message, options) {
    const overlay = ensureDialog();
    if (activeResolve) activeResolve(false);
    previousFocus = document.activeElement;
    document.getElementById('ot-dialog-title').textContent = options.title || 'OpenTurbine';
    document.getElementById('ot-dialog-message').textContent = String(message || '');
    const input = document.getElementById('ot-dialog-input');
    input.style.display = options.prompt ? '' : 'none';
    input.value = options.value || '';
    input.placeholder = options.placeholder || '';
    const cancel = document.getElementById('ot-dialog-cancel');
    cancel.style.display = options.cancel ? '' : 'none';
    cancel.textContent = options.cancelLabel || 'Cancel';
    const confirm = document.getElementById('ot-dialog-confirm');
    confirm.textContent = options.confirmText || options.confirmLabel || 'OK';
    confirm.classList.toggle('danger', !!options.danger);
    overlay.classList.add('show');
    setTimeout(() => (options.prompt ? input : confirm).focus(), 0);
    return new Promise(resolve => { activeResolve = resolve; });
  }

  window.OTDialog = {
    alert(message, options = {}) {
      return open(message, {title:'Attention required', confirmLabel:'OK', ...options});
    },
    confirm(message, options = {}) {
      return open(message, {title:'Confirm action', cancel:true, confirmLabel:'Continue', ...options});
    },
    prompt(message, options = {}) {
      return open(message, {title:'Confirmation required', cancel:true, prompt:true, confirmLabel:'Confirm', ...options});
    }
  };

  const setupKey = 'openturbine_setup_progress_v1';
  window.OTSetup = {
    mark(step) {
      try {
        const state = JSON.parse(localStorage.getItem(setupKey) || '{}');
        state[step] = Date.now();
        localStorage.setItem(setupKey, JSON.stringify(state));
      } catch (_) {}
    },
    state() {
      try { return JSON.parse(localStorage.getItem(setupKey) || '{}'); }
      catch (_) { return {}; }
    }
  };

  // Existing alert call sites stop their operation immediately after showing the
  // message. Replacing the browser primitive here preserves that control flow
  // while making every message persistent and immune to browser suppression.
  window.alert = function (message) { window.OTDialog.alert(message); };
})();
