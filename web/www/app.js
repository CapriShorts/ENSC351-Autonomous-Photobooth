// AEPB Photo Booth website
// Author: Ambardeep Chahal, AEPB team (ENSC 351, Fall 2026)
//
// Talks to the C++ WebServer module:
//   GET  /api/status     live booth state (polled)
//   GET  /api/photos     photo list, newest first
//   DELETE /api/photos/<name>
//   GET/POST /api/settings
//   POST /api/capture, /api/cancel
"use strict";

(() => {
  const LED_COUNT = 24;            // LEDs on the ring light
  const POLL_IDLE_MS = 1000;
  const POLL_BUSY_MS = 250;        // faster while counting down
  const POLL_OFFLINE_MS = 2500;
  const BUSY_STATES = ["COUNTDOWN", "CAPTURING", "SAVING"];

  const $ = (id) => document.getElementById(id);

  const els = {
    ring: $("ring"), leds: $("leds"), ringNumber: $("ring-number"), ringState: $("ring-state"),
    hint: $("booth-hint"), capture: $("capture"), cancel: $("cancel"),
    readouts: $("readouts"), light: $("light-level"), flashState: $("flash-state"),
    openSettings: $("open-settings"), prints: $("prints"), empty: $("empty"),
    count: $("photo-count"), flash: document.querySelector(".flash"),
    viewer: $("viewer"), viewerImg: $("viewer-img"), viewerCaption: $("viewer-caption"),
    viewerPrev: $("viewer-prev"), viewerNext: $("viewer-next"),
    viewerDownload: $("viewer-download"), viewerDelete: $("viewer-delete"),
    viewerClose: $("viewer-close"),
    settings: $("settings"), form: $("settings-form"), settingsError: $("settings-error"),
    settingsClose: $("settings-close"), uptime: $("uptime"),
    countdownOut: $("countdown-out"), brightnessOut: $("brightness-out"),
    toast: $("toast"),
  };

  const app = {
    status: null,
    online: null,
    features: {},
    settings: null,
    photos: [],
    knownPhotos: null,        // Set of names seen so far (null until first load)
    latestPhoto: undefined,
    viewerIndex: -1,
    pollTimer: 0,
    chaseTimer: 0,
    loadingPhotos: false,
    previewBrightness: null,  // set while the settings dialog is previewing
  };

  // ------------------------------------------------------------------ API --

  async function api(path, options = {}) {
    const res = await fetch(path, { cache: "no-store", ...options });
    let data = null;
    try { data = await res.json(); } catch (_) { /* empty body */ }
    if (!res.ok) {
      const err = new Error((data && data.error) || `The booth answered ${res.status}.`);
      err.status = res.status;
      throw err;
    }
    return data;
  }

  // ----------------------------------------------------------------- Ring --

  const ledNodes = [];

  function buildRing() {
    const NS = "http://www.w3.org/2000/svg";
    const defs = document.createElementNS(NS, "defs");
    defs.innerHTML =
      '<filter id="glow" x="-100%" y="-100%" width="300%" height="300%">' +
      '<feGaussianBlur stdDeviation="4"/></filter>';
    els.ring.insertBefore(defs, els.leds);

    for (let i = 0; i < LED_COUNT; i++) {
      const angle = (-90 + (i * 360) / LED_COUNT) * (Math.PI / 180);
      const cx = 100 + 82 * Math.cos(angle);
      const cy = 100 + 82 * Math.sin(angle);
      const g = document.createElementNS(NS, "g");
      g.innerHTML =
        `<circle class="led-housing" cx="${cx}" cy="${cy}" r="7.5"/>` +
        `<circle class="led" cx="${cx}" cy="${cy}" r="9" filter="url(#glow)"/>` +
        `<circle class="led" cx="${cx}" cy="${cy}" r="4.8"/>`;
      els.leds.appendChild(g);
      const [, glow, core] = g.children;
      ledNodes.push({ glow, core });
    }
  }

  // level: 0 (off) .. 1 (fully on, at the configured brightness)
  function setLeds(levelFor) {
    const percent = app.previewBrightness ?? (app.settings ? app.settings.ringBrightness : 60);
    const brightness = percent / 100;
    const peak = 0.35 + 0.65 * brightness;
    ledNodes.forEach((led, i) => {
      const level = levelFor(i);
      led.core.style.opacity = String(0.1 + level * (peak - 0.1));
      led.glow.style.opacity = String(level * peak * 0.85);
    });
  }

  function stopChase() {
    clearInterval(app.chaseTimer);
    app.chaseTimer = 0;
  }

  function startChase() {
    if (app.chaseTimer) return;
    let head = 0;
    const draw = () => {
      setLeds((i) => {
        const d = (head - i + LED_COUNT) % LED_COUNT;
        return d === 0 ? 1 : d === 1 ? 0.55 : d === 2 ? 0.25 : 0;
      });
      head = (head + 1) % LED_COUNT;
    };
    draw();
    app.chaseTimer = setInterval(draw, 70);
  }

  function setRingColor(color) {
    document.documentElement.style.setProperty("--led", color || "#ffffff");
  }

  function fireFlash() {
    els.flash.classList.remove("is-firing");
    void els.flash.offsetWidth; // restart the animation
    els.flash.classList.add("is-firing");
  }

  // --------------------------------------------------------------- Status --

  function describeIdleHint() {
    return app.features.capture
      ? "Press the green button on the booth, or tap Take photo."
      : "Press the green button on the booth.";
  }

  function renderStatus() {
    const s = app.status;
    els.ring.classList.toggle("is-offline", !app.online || (s && s.state === "UNKNOWN"));
    els.ring.classList.toggle("is-error", !!(app.online && s && s.state === "ERROR"));

    let number = "";
    let label = "";
    let hint = "";

    if (!app.online) {
      stopChase();
      setLeds(() => 0.15);
      label = "Offline";
      hint = "Can't reach the booth. Check that you're on the same Wi-Fi network.";
    } else {
      if (s.state !== "SAVING") stopChase();
      switch (s.state) {
        case "IDLE":
          setLeds(() => 0.55);
          label = "Ready";
          hint = describeIdleHint();
          break;
        case "COUNTDOWN": {
          const total = Math.max(1, s.countdownTotal);
          const lit = Math.ceil((LED_COUNT * s.countdownRemaining) / total);
          setLeds((i) => (i < lit ? 1 : 0));
          number = String(s.countdownRemaining);
          label = "Get ready";
          hint = "Look at the camera.";
          break;
        }
        case "CAPTURING":
          setLeds(() => 1);
          label = "Taking photo";
          break;
        case "SAVING":
          startChase();
          label = "Saving";
          hint = "Your photo will show up below.";
          break;
        case "ERROR":
          setLeds(() => 0.8);
          label = "Problem";
          hint = s.message || "Something went wrong on the booth.";
          break;
        default: // UNKNOWN: no status hook
          setLeds(() => 0.15);
          label = "No status";
          hint = "The booth hasn't connected its status to the website yet.";
      }
    }

    els.ringNumber.textContent = number;
    els.ringState.textContent = label;
    els.hint.textContent = hint;

    const state = app.online && s ? s.state : "OFFLINE";
    els.capture.disabled = state !== "IDLE";
    els.cancel.disabled = state !== "COUNTDOWN";

    if (app.online && s && s.lightLevel >= 0) {
      els.readouts.hidden = false;
      els.light.textContent = `${s.lightLevel}%`;
      els.flashState.textContent = s.flashWillFire ? "Will fire" : "Won't fire";
    } else {
      els.readouts.hidden = true;
    }

    if (app.online && s) {
      els.uptime.textContent = `Website running for ${formatDuration(s.uptimeSeconds)}`;
    }
  }

  function applyFeatures(f) {
    app.features = f || {};
    els.capture.hidden = !app.features.capture;
    els.cancel.hidden = !app.features.cancel;
    els.openSettings.hidden = !app.features.settings;
    els.viewerDelete.hidden = !app.features.delete;
    if (app.features.settings && !app.settings) loadSettings();
  }

  async function poll() {
    clearTimeout(app.pollTimer);
    const before = app.status ? app.status.state : null;
    try {
      const s = await api("/api/status");
      app.online = true;
      app.status = s;
      applyFeatures(s.features);
      if (s.state === "CAPTURING" && before !== "CAPTURING") fireFlash();
      if (s.latestPhoto !== app.latestPhoto || s.photoCount !== app.photos.length) {
        app.latestPhoto = s.latestPhoto;
        loadPhotos();
      }
    } catch (_) {
      app.online = false;
    }
    renderStatus();

    if (document.hidden) return; // resume on visibilitychange
    const busy = app.online && BUSY_STATES.includes(app.status.state);
    app.pollTimer = setTimeout(poll, !app.online ? POLL_OFFLINE_MS : busy ? POLL_BUSY_MS : POLL_IDLE_MS);
  }

  // --------------------------------------------------------------- Photos --

  const timeFormat = new Intl.DateTimeFormat(undefined, { hour: "numeric", minute: "2-digit" });
  const dateTimeFormat = new Intl.DateTimeFormat(undefined, {
    month: "short", day: "numeric", hour: "numeric", minute: "2-digit",
  });
  const fullFormat = new Intl.DateTimeFormat(undefined, {
    weekday: "long", month: "long", day: "numeric", hour: "numeric", minute: "2-digit", second: "2-digit",
  });

  function shortTime(epochSec) {
    const d = new Date(epochSec * 1000);
    const today = new Date();
    return d.toDateString() === today.toDateString() ? timeFormat.format(d) : dateTimeFormat.format(d);
  }

  function formatDuration(totalSec) {
    const h = Math.floor(totalSec / 3600);
    const m = Math.floor((totalSec % 3600) / 60);
    if (h > 0) return `${h} h ${m} min`;
    if (m > 0) return `${m} min`;
    return `${totalSec} s`;
  }

  async function loadPhotos() {
    if (app.loadingPhotos) return;
    app.loadingPhotos = true;
    try {
      const data = await api("/api/photos");
      const firstLoad = app.knownPhotos === null;
      const known = app.knownPhotos || new Set();
      app.photos = data.photos;
      renderPhotos(firstLoad ? new Set() : new Set(data.photos.filter((p) => !known.has(p.name)).map((p) => p.name)));
      app.knownPhotos = new Set(data.photos.map((p) => p.name));
      if (app.viewerIndex >= 0) syncViewer();
    } catch (err) {
      showToast(err.message, true);
    } finally {
      app.loadingPhotos = false;
    }
  }

  // Keyed by file name so existing <img>s aren't reloaded on every refresh.
  function renderPhotos(newNames) {
    const existing = new Map();
    for (const li of els.prints.children) existing.set(li.dataset.name, li);

    const frag = document.createDocumentFragment();
    app.photos.forEach((p, index) => {
      let li = existing.get(p.name);
      if (!li) {
        li = document.createElement("li");
        li.dataset.name = p.name;
        const btn = document.createElement("button");
        btn.type = "button";
        btn.className = "print";
        const img = document.createElement("img");
        img.loading = "lazy";
        img.decoding = "async";
        img.src = p.url;
        img.alt = `Photo taken ${fullFormat.format(new Date(p.time * 1000))}`;
        const caption = document.createElement("span");
        btn.append(img, caption);
        li.appendChild(btn);
        if (newNames.has(p.name)) btn.classList.add("is-new");
      }
      li.querySelector("span").textContent = shortTime(p.time);
      li.firstChild.onclick = () => openViewer(index);
      frag.appendChild(li);
    });
    els.prints.replaceChildren(frag);

    const n = app.photos.length;
    els.count.textContent = n ? String(n) : "";
    els.empty.hidden = n > 0;
  }

  // --------------------------------------------------------------- Viewer --

  function openViewer(index) {
    app.viewerIndex = index;
    showViewerPhoto();
    if (!els.viewer.open) els.viewer.showModal();
  }

  function showViewerPhoto() {
    const p = app.photos[app.viewerIndex];
    if (!p) return;
    els.viewerImg.src = p.url;
    els.viewerImg.alt = `Photo taken ${fullFormat.format(new Date(p.time * 1000))}`;
    els.viewerCaption.textContent = fullFormat.format(new Date(p.time * 1000));
    els.viewerDownload.href = p.url;
    els.viewerDownload.setAttribute("download", p.name);
    els.viewerPrev.disabled = app.viewerIndex <= 0;
    els.viewerNext.disabled = app.viewerIndex >= app.photos.length - 1;
    resetDeleteButton();
  }

  // Keep the same photo open when the list changes underneath us.
  function syncViewer() {
    const name = els.viewerDownload.getAttribute("download");
    const idx = app.photos.findIndex((p) => p.name === name);
    if (idx >= 0) {
      app.viewerIndex = idx;
      showViewerPhoto();
    } else if (app.photos.length === 0) {
      els.viewer.close();
    } else {
      app.viewerIndex = Math.min(app.viewerIndex, app.photos.length - 1);
      showViewerPhoto();
    }
  }

  function stepViewer(delta) {
    const next = app.viewerIndex + delta;
    if (next >= 0 && next < app.photos.length) {
      app.viewerIndex = next;
      showViewerPhoto();
    }
  }

  let deleteArmedTimer = 0;
  function resetDeleteButton() {
    clearTimeout(deleteArmedTimer);
    els.viewerDelete.dataset.armed = "";
    els.viewerDelete.textContent = "Delete";
  }

  async function deleteCurrent() {
    // Two taps to delete, so a stray tap on a phone doesn't lose a photo.
    if (!els.viewerDelete.dataset.armed) {
      els.viewerDelete.dataset.armed = "1";
      els.viewerDelete.textContent = "Tap again to delete";
      deleteArmedTimer = setTimeout(resetDeleteButton, 3000);
      return;
    }
    resetDeleteButton();
    const p = app.photos[app.viewerIndex];
    if (!p) return;
    try {
      await api(`/api/photos/${encodeURIComponent(p.name)}`, { method: "DELETE" });
      app.photos.splice(app.viewerIndex, 1);
      if (app.photos.length === 0) {
        els.viewer.close();
      } else {
        app.viewerIndex = Math.min(app.viewerIndex, app.photos.length - 1);
        showViewerPhoto();
      }
      showToast("Photo deleted");
      app.latestPhoto = undefined; // force a refresh on the next poll
      loadPhotos();
    } catch (err) {
      showToast(err.message, true);
    }
  }

  // ------------------------------------------------------------- Settings --

  async function loadSettings() {
    try {
      app.settings = await api("/api/settings");
      setRingColor(app.settings.ringColor);
      renderStatus();
    } catch (_) { /* hook not wired yet */ }
  }

  function fillForm(s) {
    const f = els.form.elements;
    f.countdownSeconds.value = s.countdownSeconds;
    f.flashMode.value = s.flashMode;
    f.ringColor.value = s.ringColor;
    f.ringBrightness.value = s.ringBrightness;
    f.soundEnabled.checked = s.soundEnabled;
    f.gestureEnabled.checked = s.gestureEnabled;
    updateOutputs();
  }

  function updateOutputs() {
    const f = els.form.elements;
    els.countdownOut.textContent = `${f.countdownSeconds.value} s`;
    els.brightnessOut.textContent = `${f.ringBrightness.value}%`;
  }

  // Preview colour and brightness on the on-screen ring while editing.
  function previewRing() {
    const f = els.form.elements;
    setRingColor(f.ringColor.value);
    app.previewBrightness = Number(f.ringBrightness.value);
    renderStatus();
  }

  async function openSettings() {
    els.settingsError.hidden = true;
    await loadSettings();
    if (!app.settings) {
      showToast("Settings aren't available right now.", true);
      return;
    }
    fillForm(app.settings);
    els.settings.showModal();
  }

  function closeSettings() {
    els.settings.close();
  }

  async function saveSettings(event) {
    event.preventDefault();
    const f = els.form.elements;
    const body = new URLSearchParams({
      countdownSeconds: f.countdownSeconds.value,
      flashMode: f.flashMode.value,
      ringColor: f.ringColor.value,
      ringBrightness: f.ringBrightness.value,
      soundEnabled: String(f.soundEnabled.checked),
      gestureEnabled: String(f.gestureEnabled.checked),
    });
    try {
      app.settings = await api("/api/settings", { method: "POST", body });
      els.settings.close();
      showToast("Settings saved");
    } catch (err) {
      els.settingsError.textContent = err.message;
      els.settingsError.hidden = false;
    }
  }

  // ---------------------------------------------------------------- Toast --

  let toastTimer = 0;
  function showToast(message, isError = false) {
    els.toast.textContent = message;
    els.toast.classList.toggle("is-error", isError);
    els.toast.classList.add("is-visible");
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => els.toast.classList.remove("is-visible"), isError ? 4000 : 2200);
  }

  // --------------------------------------------------------------- Wiring --

  els.capture.addEventListener("click", async () => {
    els.capture.disabled = true;
    try {
      await api("/api/capture", { method: "POST" });
    } catch (err) {
      showToast(err.message, true);
    }
    poll();
  });

  els.cancel.addEventListener("click", async () => {
    try {
      await api("/api/cancel", { method: "POST" });
    } catch (err) {
      showToast(err.message, true);
    }
    poll();
  });

  els.viewerPrev.addEventListener("click", () => stepViewer(-1));
  els.viewerNext.addEventListener("click", () => stepViewer(1));
  els.viewerDelete.addEventListener("click", deleteCurrent);
  els.viewerClose.addEventListener("click", () => els.viewer.close());
  els.viewer.addEventListener("close", () => { app.viewerIndex = -1; resetDeleteButton(); });
  els.viewer.addEventListener("click", (e) => { if (e.target === els.viewer) els.viewer.close(); });
  els.viewer.addEventListener("keydown", (e) => {
    if (e.key === "ArrowLeft") stepViewer(-1);
    if (e.key === "ArrowRight") stepViewer(1);
  });

  let touchX = null;
  els.viewer.addEventListener("touchstart", (e) => { touchX = e.touches[0].clientX; }, { passive: true });
  els.viewer.addEventListener("touchend", (e) => {
    if (touchX === null) return;
    const dx = e.changedTouches[0].clientX - touchX;
    touchX = null;
    if (Math.abs(dx) > 50) stepViewer(dx > 0 ? -1 : 1);
  });

  els.openSettings.addEventListener("click", openSettings);
  els.settingsClose.addEventListener("click", closeSettings);
  els.settings.addEventListener("close", () => {
    // Drop any unsaved preview.
    app.previewBrightness = null;
    if (app.settings) setRingColor(app.settings.ringColor);
    renderStatus();
  });
  els.form.addEventListener("submit", saveSettings);
  els.form.addEventListener("input", (e) => {
    updateOutputs();
    if (e.target.name === "ringColor" || e.target.name === "ringBrightness") previewRing();
  });

  document.addEventListener("visibilitychange", () => {
    if (!document.hidden) poll();
    else clearTimeout(app.pollTimer);
  });

  buildRing();
  renderStatus();
  poll();
})();
