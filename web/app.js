const state = {
  templates: [],
  fixtures: [],
  groups: [],
  dmx: null,
  audio: null,
  midi: {
    supported: false,
    access: null,
    inputMode: 'all',
    inputs: [],
    mappings: {},
    targets: {},
    learningControlId: null,
    learningControlLabel: '',
  },
  performance: {
    fadeSeconds: 1.0,
    intensity: {
      allOn: 1.0,
      blackout: 1.0,
      rotate: 1.0,
      strobe: 1.0,
    },
    rafId: null,
    lastTickMs: 0,
    active: false,
    sendInFlight: false,
    pendingPatchPayload: '',
    lastPatchPayload: '',
    effectByName: {
      allOn: {
        name: 'allOn',
        kind: 'brightness',
        holdSources: { ui: false, midi: false },
        envelope: 0,
        snapshot: null,
      },
      blackout: {
        name: 'blackout',
        kind: 'brightness',
        holdSources: { ui: false, midi: false },
        envelope: 0,
        snapshot: null,
      },
      rotate: {
        name: 'rotate',
        kind: 'movement',
        holdSources: { ui: false, midi: false },
        envelope: 0,
        phase: 0,
        snapshot: null,
      },
      strobe: {
        name: 'strobe',
        kind: 'strobe',
        holdSources: { ui: false, midi: false },
        envelope: 0,
        snapshot: null,
      },
    },
  },
  activeView: 'live',
  controlMode: 'slider',
  autoRefreshPauseUntil: 0,
  refreshTimer: null,
  toastTimer: null,
  learnSendTimer: null,
  reactiveThresholdTimer: null,
};

const els = {
  deviceStatus: document.getElementById('device-status'),
  devicePort: document.getElementById('device-port'),
  deviceSerial: document.getElementById('device-serial'),
  deviceFw: document.getElementById('device-fw'),
  audioBackend: document.getElementById('audio-backend'),
  audioEnergy: document.getElementById('audio-energy'),
  audioBass: document.getElementById('audio-bass'),
  audioBpm: document.getElementById('audio-bpm'),
  audioThreshold: document.getElementById('audio-threshold'),
  midiInputSelect: document.getElementById('midi-input-select'),
  midiStatus: document.getElementById('midi-status'),
  midiLearnStatus: document.getElementById('midi-learn-status'),
  reactiveProfileSelect: document.getElementById('reactive-profile-select'),
  reactiveLiveEnergy: document.getElementById('reactive-live-energy'),
  reactiveLiveEnergyValue: document.getElementById('reactive-live-energy-value'),
  reactiveThreshold: document.getElementById('reactive-threshold'),
  reactiveThresholdValue: document.getElementById('reactive-threshold-value'),
  performanceFadeSeconds: document.getElementById('performance-fade-seconds'),
  performanceFadeMidiSlot: document.getElementById('performance-fade-midi-slot'),
  performanceAllOnIntensity: document.getElementById('performance-all-on-intensity'),
  performanceAllOnIntensityMidiSlot: document.getElementById('performance-all-on-intensity-midi-slot'),
  performanceAllOnBtn: document.getElementById('performance-all-on-btn'),
  performanceAllOnMidiSlot: document.getElementById('performance-all-on-midi-slot'),
  performanceBlackoutIntensity: document.getElementById('performance-blackout-intensity'),
  performanceBlackoutIntensityMidiSlot: document.getElementById('performance-blackout-intensity-midi-slot'),
  performanceBlackoutBtn: document.getElementById('performance-blackout-btn'),
  performanceBlackoutMidiSlot: document.getElementById('performance-blackout-midi-slot'),
  panicBlackoutBtn: document.getElementById('panic-blackout-btn'),
  performanceRotateIntensity: document.getElementById('performance-rotate-intensity'),
  performanceRotateIntensityMidiSlot: document.getElementById('performance-rotate-intensity-midi-slot'),
  performanceRotateBtn: document.getElementById('performance-rotate-btn'),
  performanceRotateMidiSlot: document.getElementById('performance-rotate-midi-slot'),
  performanceStrobeIntensity: document.getElementById('performance-strobe-intensity'),
  performanceStrobeIntensityMidiSlot: document.getElementById('performance-strobe-intensity-midi-slot'),
  performanceStrobeBtn: document.getElementById('performance-strobe-btn'),
  performanceStrobeMidiSlot: document.getElementById('performance-strobe-midi-slot'),
  audioInputSelect: document.getElementById('audio-input-select'),
  applyAudioInputBtn: document.getElementById('apply-audio-input-btn'),
  audioInputLabel: document.getElementById('audio-input-label'),
  outputUniverseSelect: document.getElementById('output-universe-select'),
  outputUniverseLabel: document.getElementById('output-universe-label'),
  newUniverseNumber: document.getElementById('new-universe-number'),
  createUniverseBtn: document.getElementById('create-universe-btn'),
  universeHelp: document.getElementById('universe-help'),
  applyUniverseBtn: document.getElementById('apply-universe-btn'),
  controlModeSlider: document.getElementById('control-mode-slider'),
  controlModeKnob: document.getElementById('control-mode-knob'),
  controlModeButtons: [...document.querySelectorAll('[data-control-mode]')],
  audioReactiveMidiLearn: document.getElementById('audio-reactive-midi-learn'),
  audioReactiveMidiClear: document.getElementById('audio-reactive-midi-clear'),
  audioReactiveMidiLabel: document.getElementById('audio-reactive-midi-label'),
  viewTabs: [...document.querySelectorAll('[data-view-tab]')],
  viewPages: [...document.querySelectorAll('[data-view-page]')],
  refreshBtn: document.getElementById('refresh-btn'),
  audioToggle: document.getElementById('audio-toggle'),
  templateForm: document.getElementById('template-form'),
  templateExportBtn: document.getElementById('template-export-btn'),
  templateImportBtn: document.getElementById('template-import-btn'),
  templateImportFile: document.getElementById('template-import-file'),
  fixtureForm: document.getElementById('fixture-form'),
  groupForm: document.getElementById('group-form'),
  learnCreateForm: document.getElementById('learn-create-form'),
  fixtureTemplateSelect: document.getElementById('fixture-template-select'),
  fixtureStartAddress: document.getElementById('fixture-start-address'),
  fixtureChannelCount: document.getElementById('fixture-channel-count'),
  patchRangeText: document.getElementById('patch-range-text'),
  addChannelBtn: document.getElementById('add-channel-btn'),
  quickChannelCount: document.getElementById('quick-channel-count'),
  generateChannelsBtn: document.getElementById('generate-channels-btn'),
  channelEditorList: document.getElementById('channel-editor-list'),
  templateList: document.getElementById('template-list'),
  fixtureBoard: document.getElementById('fixture-board'),
  groupBoard: document.getElementById('group-board'),
  learnFixtureSelect: document.getElementById('learn-fixture-select'),
  learnChannelSelect: document.getElementById('learn-channel-select'),
  learnValueSlider: document.getElementById('learn-value-slider'),
  learnValueInput: document.getElementById('learn-value-input'),
  learnCaptureRangeStart: document.getElementById('learn-capture-range-start'),
  learnCaptureRangeEnd: document.getElementById('learn-capture-range-end'),
  learnChannelName: document.getElementById('learn-channel-name'),
  learnChannelKind: document.getElementById('learn-channel-kind'),
  learnSaveChannelMeta: document.getElementById('learn-save-channel-meta'),
  learnClearRanges: document.getElementById('learn-clear-ranges'),
  learnRangeStart: document.getElementById('learn-range-start'),
  learnRangeEnd: document.getElementById('learn-range-end'),
  learnRangeLabel: document.getElementById('learn-range-label'),
  learnAddRange: document.getElementById('learn-add-range'),
  learnKindButtons: [...document.querySelectorAll('.learn-kind-btn')],
  toast: document.getElementById('toast'),
  channelEditorTemplate: document.getElementById('channel-editor-template'),
  rangeEditorTemplate: document.getElementById('range-editor-template'),
};

const ICONS = {
  generic: '<svg viewBox="0 0 24 24"><path d="M4 12h16M12 4v16"/></svg>',
  dimmer: '<svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="6"/><path d="M12 2v3M12 19v3M2 12h3M19 12h3"/></svg>',
  red: '<svg viewBox="0 0 24 24"><path d="M12 4c3 4 4 6 4 8a4 4 0 1 1-8 0c0-2 1-4 4-8z"/></svg>',
  green: '<svg viewBox="0 0 24 24"><path d="M5 14c0-4 3-7 7-7 0 4-3 7-7 7z"/><path d="M12 7c4 0 7 3 7 7-4 0-7-3-7-7z"/></svg>',
  blue: '<svg viewBox="0 0 24 24"><path d="M12 3c4 5 6 8 6 11a6 6 0 1 1-12 0c0-3 2-6 6-11z"/></svg>',
  white: '<svg viewBox="0 0 24 24"><path d="M12 2l2.2 5.8L20 10l-5.8 2.2L12 18l-2.2-5.8L4 10l5.8-2.2z"/></svg>',
  strobe: '<svg viewBox="0 0 24 24"><path d="M13 2L4 14h6l-1 8 9-12h-6z"/></svg>',
  mode: '<svg viewBox="0 0 24 24"><path d="M5 7h14M5 12h14M5 17h14"/></svg>',
  speed: '<svg viewBox="0 0 24 24"><path d="M4 14a8 8 0 1 1 16 0"/><path d="M12 12l4-3"/></svg>',
  pan: '<svg viewBox="0 0 24 24"><path d="M3 12h18"/><path d="M8 7l-5 5 5 5"/><path d="M16 7l5 5-5 5"/></svg>',
  tilt: '<svg viewBox="0 0 24 24"><path d="M12 3v18"/><path d="M7 8l5-5 5 5"/><path d="M7 16l5 5 5-5"/></svg>',
  pan_speed: '<svg viewBox="0 0 24 24"><path d="M4 16c2-4 6-6 10-6"/><path d="M14 10l6-1-3 5"/></svg>',
  strip_effect: '<svg viewBox="0 0 24 24"><rect x="3" y="8" width="18" height="8" rx="2"/><path d="M7 8v8M12 8v8M17 8v8"/></svg>',
  effect_mode: '<svg viewBox="0 0 24 24"><path d="M4 17c3-6 13-6 16 0"/><path d="M4 12c3-4 13-4 16 0"/><path d="M4 7c3-2 13-2 16 0"/></svg>',
  macro: '<svg viewBox="0 0 24 24"><path d="M4 4h16v16H4z"/><path d="M8 8h8M8 12h8M8 16h5"/></svg>',
  jump: '<svg viewBox="0 0 24 24"><path d="M5 18l6-8 4 4 4-8"/></svg>',
  gradient: '<svg viewBox="0 0 24 24"><path d="M4 16c3-6 13-6 16 0"/><path d="M4 10c3-4 13-4 16 0"/></svg>',
  pulse: '<svg viewBox="0 0 24 24"><path d="M3 12h4l2-4 4 8 2-4h6"/></svg>',
  voice: '<svg viewBox="0 0 24 24"><path d="M12 4a3 3 0 0 1 3 3v4a3 3 0 0 1-6 0V7a3 3 0 0 1 3-3z"/><path d="M7 11a5 5 0 0 0 10 0M12 16v4"/></svg>',
};

const AUTO_REFRESH_EDIT_PAUSE_MS = 5000;

async function api(path, options = {}) {
  const opts = { method: 'GET', ...options };
  if (opts.body && typeof opts.body === 'object' && !(opts.body instanceof URLSearchParams) && !(opts.body instanceof FormData)) {
    opts.body = new URLSearchParams(opts.body);
  }

  if (opts.body instanceof URLSearchParams) {
    opts.headers = {
      ...(opts.headers || {}),
      'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8',
    };
  }

  const res = await fetch(path, opts);
  const data = await res.json().catch(() => ({ ok: false, error: 'Invalid JSON response from server' }));

  if (!res.ok || data.ok === false) {
    throw new Error(data.error || `Request failed (${res.status})`);
  }

  return data;
}

function showToast(message, kind = 'info') {
  els.toast.textContent = message;
  els.toast.classList.add('visible');
  els.toast.style.background = kind === 'error' ? 'rgba(140, 32, 20, 0.95)' : 'rgba(31, 42, 40, 0.92)';

  window.clearTimeout(state.toastTimer);
  state.toastTimer = window.setTimeout(() => {
    els.toast.classList.remove('visible');
  }, 2600);
}

function pauseAutoRefreshForEditing(durationMs = AUTO_REFRESH_EDIT_PAUSE_MS) {
  const nextUntil = Date.now() + Math.max(400, Number(durationMs || AUTO_REFRESH_EDIT_PAUSE_MS));
  state.autoRefreshPauseUntil = Math.max(state.autoRefreshPauseUntil, nextUntil);
}

function shouldAutoRefreshNow() {
  return Date.now() >= state.autoRefreshPauseUntil;
}

function installAutoRefreshPauseGuards(root) {
  if (!root) return;

  const handler = (event) => {
    const target = event.target;
    if (!(target instanceof HTMLElement)) return;
    if (!target.closest('input, select, textarea, button')) return;
    pauseAutoRefreshForEditing();
  };

  ['focusin', 'input', 'change', 'keydown', 'pointerdown'].forEach((eventName) => {
    root.addEventListener(eventName, handler, true);
  });
}

function iconForKind(kind) {
  return ICONS[kind] || ICONS.generic;
}

function iconForRangeLabel(label) {
  const text = (label || '').toLowerCase();
  if (text.includes('jump')) return ICONS.jump;
  if (text.includes('gradient') || text.includes('fade')) return ICONS.gradient;
  if (text.includes('pulse')) return ICONS.pulse;
  if (text.includes('voice') || text.includes('sound') || text.includes('music')) return ICONS.voice;
  if (text.includes('strobe') || text.includes('flash')) return ICONS.strobe;
  if (text.includes('random') || text.includes('walk')) return ICONS.macro;
  if (text.includes('shift') || text.includes('aberration')) return ICONS.effect_mode;
  return ICONS.mode;
}

function iconBadge(kindOrLabel, byLabel = false) {
  const svg = byLabel ? iconForRangeLabel(kindOrLabel) : iconForKind(kindOrLabel);
  return `<span class="icon-badge" aria-hidden="true">${svg}</span>`;
}

const MIDI_STORAGE_KEY = 'tuxdmx.midi.v1';
const MIDI_REACTIVE_CONTROL_ID = 'audio:reactive';
const PERFORMANCE_STORAGE_KEY = 'tuxdmx.performance.v1';
const MIDI_PERFORMANCE_FADE_CONTROL_ID = 'perf:fadeSeconds';
const MIDI_PERFORMANCE_ALL_ON_INTENSITY_CONTROL_ID = 'perf:allOnIntensity';
const MIDI_PERFORMANCE_ALL_ON_CONTROL_ID = 'perf:allOn';
const MIDI_PERFORMANCE_BLACKOUT_INTENSITY_CONTROL_ID = 'perf:blackoutIntensity';
const MIDI_PERFORMANCE_BLACKOUT_CONTROL_ID = 'perf:blackout';
const MIDI_PERFORMANCE_ROTATE_INTENSITY_CONTROL_ID = 'perf:rotateIntensity';
const MIDI_PERFORMANCE_ROTATE_CONTROL_ID = 'perf:rotate';
const MIDI_PERFORMANCE_STROBE_INTENSITY_CONTROL_ID = 'perf:strobeIntensity';
const MIDI_PERFORMANCE_STROBE_CONTROL_ID = 'perf:strobe';

function midiInputNameById(inputId) {
  const input = (state.midi.inputs || []).find((item) => item.id === inputId);
  return input?.name || inputId || 'Unknown input';
}

function describeMidiMapping(mapping) {
  if (!mapping) return 'MIDI: not mapped';
  const source = mapping.source === 'specific' ? midiInputNameById(mapping.inputId) : 'Any input';
  const type = mapping.type === 'cc' ? `CC${mapping.number}` : `NOTE${mapping.number}`;
  return `MIDI: ${source} • CH${mapping.channel} ${type}`;
}

function saveMidiPreferences() {
  const payload = {
    inputMode: state.midi.inputMode || 'all',
    mappings: state.midi.mappings || {},
  };
  try {
    window.localStorage.setItem(MIDI_STORAGE_KEY, JSON.stringify(payload));
  } catch {
    // Ignore localStorage write failures.
  }
}

function loadMidiPreferences() {
  try {
    const raw = window.localStorage.getItem(MIDI_STORAGE_KEY);
    if (!raw) return;
    const parsed = JSON.parse(raw);
    if (typeof parsed !== 'object' || parsed === null) return;

    if (typeof parsed.inputMode === 'string' && parsed.inputMode.length) {
      state.midi.inputMode = parsed.inputMode;
    }

    if (typeof parsed.mappings === 'object' && parsed.mappings !== null) {
      const safeMappings = {};
      Object.entries(parsed.mappings).forEach(([controlId, mapping]) => {
        if (!mapping || typeof mapping !== 'object') return;
        const type = mapping.type === 'note' ? 'note' : 'cc';
        const source = mapping.source === 'specific' ? 'specific' : 'all';
        const channel = Math.max(1, Math.min(16, Number(mapping.channel || 1)));
        const number = Math.max(0, Math.min(127, Number(mapping.number || 0)));
        safeMappings[controlId] = {
          type,
          source,
          channel,
          number,
          inputId: source === 'specific' ? String(mapping.inputId || '') : '',
        };
      });
      state.midi.mappings = safeMappings;
    }
  } catch {
    // Ignore localStorage parse/read failures.
  }
}

function setMidiLearnStatus() {
  if (!state.midi.learningControlId) {
    els.midiLearnStatus.textContent = 'Learn: idle';
    return;
  }
  els.midiLearnStatus.textContent = `Learn: waiting for MIDI message to map "${state.midi.learningControlLabel}"`;
}

function refreshMidiStatusText() {
  if (!state.midi.supported) {
    els.midiStatus.textContent = 'MIDI unsupported in this browser (use Chrome/Edge).';
    return;
  }

  const connected = (state.midi.inputs || []).length;
  const sourceText = state.midi.inputMode === 'all'
    ? 'all inputs'
    : midiInputNameById(state.midi.inputMode);
  els.midiStatus.textContent = `Inputs: ${connected} • Listening: ${sourceText}`;
}

function refreshMidiInputSelectUi() {
  if (!els.midiInputSelect) return;

  els.midiInputSelect.innerHTML = '';

  if (!state.midi.supported) {
    const option = document.createElement('option');
    option.value = 'all';
    option.textContent = 'MIDI not supported';
    els.midiInputSelect.appendChild(option);
    els.midiInputSelect.disabled = true;
    refreshMidiStatusText();
    setMidiLearnStatus();
    return;
  }

  const allOption = document.createElement('option');
  allOption.value = 'all';
  allOption.textContent = 'All MIDI inputs';
  els.midiInputSelect.appendChild(allOption);

  (state.midi.inputs || []).forEach((input) => {
    const option = document.createElement('option');
    option.value = input.id;
    option.textContent = input.name;
    els.midiInputSelect.appendChild(option);
  });

  if (state.midi.inputMode !== 'all' && !(state.midi.inputs || []).some((input) => input.id === state.midi.inputMode)) {
    state.midi.inputMode = 'all';
  }

  els.midiInputSelect.value = state.midi.inputMode || 'all';
  els.midiInputSelect.disabled = false;

  refreshMidiStatusText();
  setMidiLearnStatus();
  updateAllMidiTargetUi();
}

function updateMidiTargetUi(controlId) {
  const target = state.midi.targets[controlId];
  if (!target) return;

  const mapping = state.midi.mappings[controlId];
  if (target.labelEl) {
    target.labelEl.textContent = describeMidiMapping(mapping);
  }

  if (target.clearBtn) {
    target.clearBtn.disabled = !mapping;
  }

  if (target.rowEl) {
    target.rowEl.classList.toggle('is-learning', state.midi.learningControlId === controlId);
  }
}

function updateAllMidiTargetUi() {
  Object.keys(state.midi.targets).forEach((controlId) => {
    updateMidiTargetUi(controlId);
  });
}

function registerMidiTarget(controlId, config) {
  state.midi.targets[controlId] = {
    ...config,
    lastAppliedValue: undefined,
  };
  updateMidiTargetUi(controlId);
}

function unregisterMidiTargetsByPrefix(prefix) {
  Object.keys(state.midi.targets).forEach((controlId) => {
    if (!controlId.startsWith(prefix)) return;
    delete state.midi.targets[controlId];
    if (state.midi.learningControlId === controlId) {
      state.midi.learningControlId = null;
      state.midi.learningControlLabel = '';
      setMidiLearnStatus();
    }
  });
}

function startMidiLearn(controlId, label) {
  state.midi.learningControlId = controlId;
  state.midi.learningControlLabel = label;
  setMidiLearnStatus();
  updateAllMidiTargetUi();
  showToast(`MIDI learn armed: ${label}`);
}

function stopMidiLearn() {
  state.midi.learningControlId = null;
  state.midi.learningControlLabel = '';
  setMidiLearnStatus();
  updateAllMidiTargetUi();
}

function clearMidiMapping(controlId, { notify = true } = {}) {
  if (!state.midi.mappings[controlId]) return;
  delete state.midi.mappings[controlId];
  if (controlId === MIDI_PERFORMANCE_ALL_ON_CONTROL_ID) {
    setEffectSource('allOn', 'midi', false);
  } else if (controlId === MIDI_PERFORMANCE_BLACKOUT_CONTROL_ID) {
    setEffectSource('blackout', 'midi', false);
  } else if (controlId === MIDI_PERFORMANCE_ROTATE_CONTROL_ID) {
    setEffectSource('rotate', 'midi', false);
  } else if (controlId === MIDI_PERFORMANCE_STROBE_CONTROL_ID) {
    setEffectSource('strobe', 'midi', false);
  }
  if (state.midi.learningControlId === controlId) {
    stopMidiLearn();
  }
  saveMidiPreferences();
  updateMidiTargetUi(controlId);
  if (notify) {
    showToast('MIDI mapping cleared');
  }
}

function createMidiBindRow(controlId, label, kind, onMidiValue) {
  const row = document.createElement('div');
  row.className = 'midi-bind-row';

  const mappingLabel = document.createElement('span');
  mappingLabel.className = 'midi-bind-label';

  const actions = document.createElement('div');
  actions.className = 'midi-bind-actions';

  const learnBtn = document.createElement('button');
  learnBtn.type = 'button';
  learnBtn.className = 'btn ghost';
  learnBtn.textContent = 'MIDI Learn';
  learnBtn.addEventListener('click', () => startMidiLearn(controlId, label));

  const clearBtn = document.createElement('button');
  clearBtn.type = 'button';
  clearBtn.className = 'btn ghost';
  clearBtn.textContent = 'Clear';
  clearBtn.addEventListener('click', () => clearMidiMapping(controlId));

  actions.append(learnBtn, clearBtn);
  row.append(mappingLabel, actions);

  registerMidiTarget(controlId, {
    label,
    kind,
    onMidiValue,
    rowEl: row,
    labelEl: mappingLabel,
    clearBtn,
  });

  return row;
}

function normalizeMidiMessage(event) {
  const data = event.data;
  if (!data || data.length < 2) return null;

  const status = Number(data[0]);
  const data1 = Number(data[1]);
  const data2 = Number(data[2] || 0);

  const command = status & 0xF0;
  const channel = (status & 0x0F) + 1;

  if (command === 0xB0) {
    return {
      type: 'cc',
      channel,
      number: data1,
      value: data2,
      on: data2 >= 64,
      mappedValue: Math.round((data2 / 127) * 255),
    };
  }

  if (command === 0x90) {
    const on = data2 > 0;
    return {
      type: 'note',
      channel,
      number: data1,
      value: data2,
      on,
      mappedValue: on ? 255 : 0,
    };
  }

  if (command === 0x80) {
    return {
      type: 'note',
      channel,
      number: data1,
      value: 0,
      on: false,
      mappedValue: 0,
    };
  }

  return null;
}

function mappingMatchesMessage(mapping, midiMessage, sourceInputId) {
  if (!mapping || !midiMessage) return false;
  if (mapping.type !== midiMessage.type) return false;
  if (Number(mapping.channel) !== Number(midiMessage.channel)) return false;
  if (Number(mapping.number) !== Number(midiMessage.number)) return false;
  if (mapping.source === 'specific' && mapping.inputId !== sourceInputId) return false;
  return true;
}

async function processMidiMessage(event) {
  const midiMessage = normalizeMidiMessage(event);
  if (!midiMessage) return;

  const sourceInputId = event.currentTarget?.id || event.target?.id || '';

  if (state.midi.inputMode !== 'all' && sourceInputId !== state.midi.inputMode) {
    return;
  }

  if (state.midi.learningControlId) {
    const controlId = state.midi.learningControlId;
    const mapping = {
      source: state.midi.inputMode === 'all' ? 'all' : 'specific',
      inputId: state.midi.inputMode === 'all' ? '' : sourceInputId,
      type: midiMessage.type,
      channel: midiMessage.channel,
      number: midiMessage.number,
    };

    state.midi.mappings[controlId] = mapping;
    saveMidiPreferences();
    stopMidiLearn();
    updateMidiTargetUi(controlId);
    showToast(`Mapped ${state.midi.targets[controlId]?.label || controlId} to ${describeMidiMapping(mapping)}`);
    return;
  }

  const mappingEntries = Object.entries(state.midi.mappings);
  for (const [controlId, mapping] of mappingEntries) {
    const target = state.midi.targets[controlId];
    if (!target) continue;
    if (!mappingMatchesMessage(mapping, midiMessage, sourceInputId)) continue;

    const nextValue = target.kind === 'toggle' ? midiMessage.on : midiMessage.mappedValue;
    if (target.lastAppliedValue === nextValue) continue;
    target.lastAppliedValue = nextValue;

    try {
      await target.onMidiValue(nextValue, midiMessage);
    } catch (err) {
      showToast(err.message || 'MIDI mapping action failed', 'error');
    }
  }
}

function attachMidiInputListeners() {
  if (!state.midi.access) return;
  for (const input of state.midi.access.inputs.values()) {
    input.onmidimessage = (event) => {
      processMidiMessage(event).catch((err) => {
        showToast(err.message || 'MIDI processing error', 'error');
      });
    };
  }
}

function refreshMidiInputsFromAccess() {
  if (!state.midi.access) return;

  const inputs = [];
  for (const input of state.midi.access.inputs.values()) {
    if (input.state !== 'connected') continue;
    inputs.push({
      id: input.id,
      name: input.name || `MIDI Input ${inputs.length + 1}`,
    });
  }
  state.midi.inputs = inputs;

  attachMidiInputListeners();
  refreshMidiInputSelectUi();
}

async function initializeMidi() {
  loadMidiPreferences();

  if (!navigator.requestMIDIAccess) {
    state.midi.supported = false;
    refreshMidiInputSelectUi();
    return;
  }

  try {
    const access = await navigator.requestMIDIAccess({ sysex: false });
    state.midi.supported = true;
    state.midi.access = access;
    access.onstatechange = () => {
      refreshMidiInputsFromAccess();
    };
    refreshMidiInputsFromAccess();
  } catch {
    state.midi.supported = false;
    refreshMidiInputSelectUi();
  }
}

function setActiveView(view, { persist = true } = {}) {
  const nextView = ['live', 'patch', 'templates', 'groups', 'learn'].includes(view) ? view : 'live';
  state.activeView = nextView;

  els.viewTabs.forEach((tab) => {
    const active = tab.dataset.viewTab === nextView;
    tab.classList.toggle('is-active', active);
    tab.setAttribute('aria-pressed', active ? 'true' : 'false');
  });

  els.viewPages.forEach((page) => {
    page.classList.toggle('is-active', page.dataset.viewPage === nextView);
  });

  if (persist) {
    try {
      window.localStorage.setItem('tuxdmx.activeView', nextView);
    } catch {
      // Ignore storage write failures.
    }
  }
}

function setLocalChannelValue(fixtureId, channelIndex, value) {
  const fixture = (state.fixtures || []).find((item) => Number(item.id) === Number(fixtureId));
  if (!fixture) return;
  const channel = (fixture.channels || []).find((item) => Number(item.channelIndex) === Number(channelIndex));
  if (!channel) return;
  channel.value = Math.max(0, Math.min(255, Math.round(Number(value || 0))));
}

function setControlMode(mode, { persist = true, rerender = true } = {}) {
  const nextMode = mode === 'knob' ? 'knob' : 'slider';
  state.controlMode = nextMode;

  els.controlModeButtons.forEach((button) => {
    const active = button.dataset.controlMode === nextMode;
    button.classList.toggle('is-active', active);
    button.setAttribute('aria-pressed', active ? 'true' : 'false');
  });

  if (persist) {
    try {
      window.localStorage.setItem('tuxdmx.controlMode', nextMode);
    } catch {
      // Ignore storage write failures.
    }
  }

  if (rerender) {
    renderFixtures(state.fixtures || []);
    renderGroups(state.groups || []);
  }
}

function initializeUiPreferences() {
  let savedView = 'live';
  let savedMode = 'slider';
  try {
    savedView = window.localStorage.getItem('tuxdmx.activeView') || 'live';
    savedMode = window.localStorage.getItem('tuxdmx.controlMode') || 'slider';
  } catch {
    // Ignore storage access issues; defaults are good fallback values.
  }
  setActiveView(savedView, { persist: false });
  setControlMode(savedMode, { persist: false, rerender: false });
}

function savePerformancePreferences() {
  try {
    window.localStorage.setItem(PERFORMANCE_STORAGE_KEY, JSON.stringify({
      fadeSeconds: state.performance.fadeSeconds,
      intensity: state.performance.intensity,
    }));
  } catch {
    // Ignore localStorage write failures.
  }
}

function loadPerformancePreferences() {
  try {
    const raw = window.localStorage.getItem(PERFORMANCE_STORAGE_KEY);
    if (!raw) return;
    const parsed = JSON.parse(raw);
    if (typeof parsed !== 'object' || parsed === null) return;
    if (Number.isFinite(Number(parsed.fadeSeconds))) {
      state.performance.fadeSeconds = Math.max(0.01, Math.min(10, Number(parsed.fadeSeconds)));
    }
    if (typeof parsed.intensity === 'object' && parsed.intensity !== null) {
      const intensity = parsed.intensity;
      if (Number.isFinite(Number(intensity.allOn))) {
        state.performance.intensity.allOn = Math.max(0, Math.min(1, Number(intensity.allOn)));
      }
      if (Number.isFinite(Number(intensity.blackout))) {
        state.performance.intensity.blackout = Math.max(0, Math.min(1, Number(intensity.blackout)));
      }
      if (Number.isFinite(Number(intensity.rotate))) {
        state.performance.intensity.rotate = Math.max(0, Math.min(1, Number(intensity.rotate)));
      }
      if (Number.isFinite(Number(intensity.strobe))) {
        state.performance.intensity.strobe = Math.max(0, Math.min(1, Number(intensity.strobe)));
      }
    }
  } catch {
    // Ignore localStorage read failures.
  }
}

function setPerformanceFadeSeconds(value, { persist = true } = {}) {
  const normalized = Math.max(0.01, Math.min(10, Number(value || 1)));
  state.performance.fadeSeconds = normalized;
  els.performanceFadeSeconds.value = normalized.toFixed(2);
  if (persist) {
    savePerformancePreferences();
  }
}

function setPerformanceIntensity(effectName, value, { persist = true } = {}) {
  if (!(effectName in state.performance.intensity)) return;
  const normalized = Math.max(0, Math.min(1, Number(value || 0)));
  state.performance.intensity[effectName] = normalized;

  if (effectName === 'allOn') {
    els.performanceAllOnIntensity.value = normalized.toFixed(2);
  } else if (effectName === 'blackout') {
    els.performanceBlackoutIntensity.value = normalized.toFixed(2);
  } else if (effectName === 'rotate') {
    els.performanceRotateIntensity.value = normalized.toFixed(2);
  } else if (effectName === 'strobe') {
    els.performanceStrobeIntensity.value = normalized.toFixed(2);
  }

  if (persist) {
    savePerformancePreferences();
  }
}

function effectByName(effectName) {
  return state.performance.effectByName[effectName] || null;
}

function isEffectHeld(effect) {
  if (!effect) return false;
  return Boolean(effect.holdSources.ui || effect.holdSources.midi);
}

function snapshotKindsForEffect(effectName) {
  if (effectName === 'allOn' || effectName === 'blackout') {
    return new Set(['dimmer', 'red', 'green', 'blue', 'white']);
  }
  if (effectName === 'rotate') {
    return new Set(['pan', 'tilt', 'pan_speed']);
  }
  if (effectName === 'strobe') {
    return new Set(['strobe']);
  }
  return new Set();
}

function capturePerformanceSnapshot(effectName) {
  const kinds = snapshotKindsForEffect(effectName);
  const snapshot = [];

  (state.fixtures || []).forEach((fixture) => {
    const channels = (fixture.channels || [])
      .filter((channel) => kinds.has(String(channel.kind || '').toLowerCase()))
      .map((channel) => ({
        channelIndex: Number(channel.channelIndex),
        kind: String(channel.kind || '').toLowerCase(),
        baseValue: Math.max(0, Math.min(255, Number(channel.value || 0))),
      }));

    if (!channels.length) return;
    snapshot.push({
      fixtureId: Number(fixture.id),
      universe: Number(fixture.universe || 1),
      startAddress: Number(fixture.startAddress || 1),
      channels,
    });
  });

  return snapshot;
}

function setEffectSource(effectName, source, active) {
  const effect = effectByName(effectName);
  if (!effect || !effect.holdSources || !(source in effect.holdSources)) return;

  const prevHeld = isEffectHeld(effect);
  effect.holdSources[source] = Boolean(active);
  const nextHeld = isEffectHeld(effect);

  // All On and Blackout are mutually exclusive hold actions.
  if (nextHeld && effectName === 'allOn') {
    const other = effectByName('blackout');
    if (other) {
      other.holdSources.ui = false;
      other.holdSources.midi = false;
    }
  }
  if (nextHeld && effectName === 'blackout') {
    const other = effectByName('allOn');
    if (other) {
      other.holdSources.ui = false;
      other.holdSources.midi = false;
    }
  }

  if (!prevHeld && nextHeld) {
    effect.snapshot = capturePerformanceSnapshot(effectName);
  }

  updatePerformanceButtonVisuals();
  ensurePerformanceLoop();
}

function updatePerformanceButtonVisuals() {
  const allOn = effectByName('allOn');
  const blackout = effectByName('blackout');
  const rotate = effectByName('rotate');
  const strobe = effectByName('strobe');

  els.performanceAllOnBtn.classList.toggle('is-active', Boolean(allOn && (isEffectHeld(allOn) || allOn.envelope > 0.001)));
  els.performanceBlackoutBtn.classList.toggle(
    'is-active',
    Boolean(blackout && (isEffectHeld(blackout) || blackout.envelope > 0.001)),
  );
  els.performanceRotateBtn.classList.toggle('is-active', Boolean(rotate && (isEffectHeld(rotate) || rotate.envelope > 0.001)));
  els.performanceStrobeBtn.classList.toggle('is-active', Boolean(strobe && (isEffectHeld(strobe) || strobe.envelope > 0.001)));
}

function buildPerformancePatchMap(dtSeconds) {
  const fadeSeconds = Math.max(0.01, state.performance.fadeSeconds);
  const blendStep = dtSeconds / fadeSeconds;

  const patches = new Map();
  const setPatch = (fixtureId, universe, absoluteAddress, channelIndex, value) => {
    if (absoluteAddress < 1 || absoluteAddress > 512) return;
    const key = `${fixtureId}:${channelIndex}`;
    patches.set(key, {
      fixtureId,
      channelIndex,
      universe,
      absoluteAddress,
      value: Math.max(0, Math.min(255, Math.round(value))),
    });
  };

  const effects = Object.values(state.performance.effectByName);
  effects.forEach((effect) => {
    const held = isEffectHeld(effect);
    const previousEnvelope = effect.envelope;
    if (held) {
      effect.envelope = Math.min(1, effect.envelope + blendStep);
    } else {
      effect.envelope = Math.max(0, effect.envelope - blendStep);
    }
    effect.justSettled = !held && previousEnvelope > 0 && effect.envelope <= 0.0001;
  });

  const allOn = effectByName('allOn');
  if (allOn && allOn.snapshot && (allOn.envelope > 0 || allOn.justSettled || isEffectHeld(allOn))) {
    const e = allOn.envelope;
    const intensity = state.performance.intensity.allOn;
    const blend = e * intensity;
    allOn.snapshot.forEach((fixture) => {
      fixture.channels.forEach((channel) => {
        const value = channel.baseValue + ((255 - channel.baseValue) * blend);
        const absoluteAddress = fixture.startAddress + channel.channelIndex - 1;
        setPatch(fixture.fixtureId, fixture.universe, absoluteAddress, channel.channelIndex, value);
      });
    });
  }

  const blackout = effectByName('blackout');
  if (blackout && blackout.snapshot && (blackout.envelope > 0 || blackout.justSettled || isEffectHeld(blackout))) {
    const e = blackout.envelope;
    const intensity = state.performance.intensity.blackout;
    const blend = e * intensity;
    blackout.snapshot.forEach((fixture) => {
      fixture.channels.forEach((channel) => {
        const value = channel.baseValue * (1 - blend);
        const absoluteAddress = fixture.startAddress + channel.channelIndex - 1;
        setPatch(fixture.fixtureId, fixture.universe, absoluteAddress, channel.channelIndex, value);
      });
    });
  }

  const rotate = effectByName('rotate');
  if (rotate) {
    const intensity = state.performance.intensity.rotate;
    const movementBlend = rotate.envelope * intensity;
    rotate.phase += dtSeconds * (1.3 + movementBlend * 1.9);
    if (rotate.phase > 10000) {
      rotate.phase = rotate.phase % (Math.PI * 2);
    }

    if (rotate.snapshot && (rotate.envelope > 0 || rotate.justSettled || isEffectHeld(rotate))) {
      rotate.snapshot.forEach((fixture) => {
        const fixturePhase = rotate.phase + (fixture.fixtureId * 0.47);
        fixture.channels.forEach((channel) => {
          let value = channel.baseValue;
          if (channel.kind === 'pan') {
            value = channel.baseValue + Math.sin(fixturePhase) * (92 * movementBlend);
          } else if (channel.kind === 'tilt') {
            value = channel.baseValue + Math.cos((fixturePhase * 0.86) + 0.7) * (52 * movementBlend);
          } else if (channel.kind === 'pan_speed') {
            value = channel.baseValue + ((24 - channel.baseValue) * movementBlend);
          }
          const absoluteAddress = fixture.startAddress + channel.channelIndex - 1;
          setPatch(fixture.fixtureId, fixture.universe, absoluteAddress, channel.channelIndex, value);
        });
      });
    }
  }

  const strobe = effectByName('strobe');
  if (strobe && strobe.snapshot && (strobe.envelope > 0 || strobe.justSettled || isEffectHeld(strobe))) {
    const e = strobe.envelope;
    const intensity = state.performance.intensity.strobe;
    const blend = e * intensity;
    strobe.snapshot.forEach((fixture) => {
      fixture.channels.forEach((channel) => {
        const target = 210;
        const value = channel.baseValue + ((target - channel.baseValue) * blend);
        const absoluteAddress = fixture.startAddress + channel.channelIndex - 1;
        setPatch(fixture.fixtureId, fixture.universe, absoluteAddress, channel.channelIndex, value);
      });
    });
  }

  effects.forEach((effect) => {
    if (!isEffectHeld(effect) && effect.envelope <= 0.0001) {
      effect.envelope = 0;
      effect.snapshot = null;
      effect.justSettled = false;
    }
  });

  return patches;
}

function queuePerformancePatchPayload(payload) {
  if (!payload) return;
  if (state.performance.pendingPatchPayload === payload && state.performance.sendInFlight) return;
  if (!state.performance.sendInFlight && state.performance.lastPatchPayload === payload) return;

  state.performance.pendingPatchPayload = payload;
  if (state.performance.sendInFlight) return;

  const flush = async () => {
    if (!state.performance.pendingPatchPayload || state.performance.sendInFlight) return;
    const nextPayload = state.performance.pendingPatchPayload;
    state.performance.pendingPatchPayload = '';
    state.performance.sendInFlight = true;

    try {
      await api('/api/dmx/patches', {
        method: 'POST',
        body: { patches: nextPayload },
      });
      state.performance.lastPatchPayload = nextPayload;
    } catch (err) {
      showToast(err.message, 'error');
    } finally {
      state.performance.sendInFlight = false;
      if (state.performance.pendingPatchPayload) {
        flush().catch((err) => showToast(err.message, 'error'));
      }
    }
  };

  flush().catch((err) => showToast(err.message, 'error'));
}

function performanceFrame(timestampMs) {
  if (!state.performance.active) return;

  const last = state.performance.lastTickMs || timestampMs;
  const dtSeconds = Math.max(0.008, Math.min(0.1, (timestampMs - last) / 1000));
  state.performance.lastTickMs = timestampMs;

  const patchMap = buildPerformancePatchMap(dtSeconds);
  if (patchMap.size > 0) {
    const patchParts = [];
    patchMap.forEach((patch) => {
      patchParts.push(`${patch.universe}:${patch.absoluteAddress}:${patch.value}`);
      setLocalChannelValue(patch.fixtureId, patch.channelIndex, patch.value);
    });
    queuePerformancePatchPayload(patchParts.join(','));
  }

  updatePerformanceButtonVisuals();

  const anyActive = Object.values(state.performance.effectByName).some(
    (effect) => isEffectHeld(effect) || effect.envelope > 0.0001,
  );

  if (!anyActive && !state.performance.pendingPatchPayload && !state.performance.sendInFlight) {
    state.performance.active = false;
    state.performance.lastTickMs = 0;
    state.performance.rafId = null;
    state.performance.lastPatchPayload = '';
    return;
  }

  state.performance.rafId = window.requestAnimationFrame(performanceFrame);
}

function ensurePerformanceLoop() {
  if (state.performance.active) return;
  state.performance.active = true;
  state.performance.lastTickMs = 0;
  state.performance.rafId = window.requestAnimationFrame(performanceFrame);
}

function bindMomentaryPerformanceButton(button, effectName) {
  if (!button) return;

  const releaseUi = () => setEffectSource(effectName, 'ui', false);

  button.addEventListener('pointerdown', (event) => {
    event.preventDefault();
    button.setPointerCapture(event.pointerId);
    setEffectSource(effectName, 'ui', true);
  });
  button.addEventListener('pointerup', releaseUi);
  button.addEventListener('pointercancel', releaseUi);
  button.addEventListener('lostpointercapture', releaseUi);

  button.addEventListener('keydown', (event) => {
    if ((event.key === ' ' || event.key === 'Enter') && !event.repeat) {
      event.preventDefault();
      setEffectSource(effectName, 'ui', true);
    }
  });
  button.addEventListener('keyup', (event) => {
    if (event.key === ' ' || event.key === 'Enter') {
      event.preventDefault();
      setEffectSource(effectName, 'ui', false);
    }
  });
  button.addEventListener('blur', releaseUi);
}

function initializePerformanceControls() {
  loadPerformancePreferences();
  setPerformanceFadeSeconds(state.performance.fadeSeconds, { persist: false });
  setPerformanceIntensity('allOn', state.performance.intensity.allOn, { persist: false });
  setPerformanceIntensity('blackout', state.performance.intensity.blackout, { persist: false });
  setPerformanceIntensity('rotate', state.performance.intensity.rotate, { persist: false });
  setPerformanceIntensity('strobe', state.performance.intensity.strobe, { persist: false });

  els.performanceFadeSeconds.addEventListener('input', () => {
    setPerformanceFadeSeconds(els.performanceFadeSeconds.value);
  });
  els.performanceAllOnIntensity.addEventListener('input', () => {
    setPerformanceIntensity('allOn', els.performanceAllOnIntensity.value);
  });
  els.performanceBlackoutIntensity.addEventListener('input', () => {
    setPerformanceIntensity('blackout', els.performanceBlackoutIntensity.value);
  });
  els.performanceRotateIntensity.addEventListener('input', () => {
    setPerformanceIntensity('rotate', els.performanceRotateIntensity.value);
  });
  els.performanceStrobeIntensity.addEventListener('input', () => {
    setPerformanceIntensity('strobe', els.performanceStrobeIntensity.value);
  });

  bindMomentaryPerformanceButton(els.performanceAllOnBtn, 'allOn');
  bindMomentaryPerformanceButton(els.performanceBlackoutBtn, 'blackout');
  bindMomentaryPerformanceButton(els.performanceRotateBtn, 'rotate');
  bindMomentaryPerformanceButton(els.performanceStrobeBtn, 'strobe');

  els.performanceFadeMidiSlot.appendChild(
    createMidiBindRow(
      MIDI_PERFORMANCE_FADE_CONTROL_ID,
      'Performance Fade Seconds',
      'continuous',
      async (value) => {
        const seconds = 0.05 + ((Math.max(0, Math.min(255, Number(value || 0))) / 255) * 4.95);
        setPerformanceFadeSeconds(seconds);
      },
    ),
  );

  els.performanceAllOnIntensityMidiSlot.appendChild(
    createMidiBindRow(
      MIDI_PERFORMANCE_ALL_ON_INTENSITY_CONTROL_ID,
      'Global Lift Strength',
      'continuous',
      async (value) => {
        setPerformanceIntensity('allOn', Math.max(0, Math.min(255, Number(value || 0))) / 255);
      },
    ),
  );

  els.performanceAllOnMidiSlot.appendChild(
    createMidiBindRow(
      MIDI_PERFORMANCE_ALL_ON_CONTROL_ID,
      'Global Lift Hold',
      'toggle',
      async (enabled) => {
        setEffectSource('allOn', 'midi', Boolean(enabled));
      },
    ),
  );

  els.performanceBlackoutIntensityMidiSlot.appendChild(
    createMidiBindRow(
      MIDI_PERFORMANCE_BLACKOUT_INTENSITY_CONTROL_ID,
      'Blackout Strength',
      'continuous',
      async (value) => {
        setPerformanceIntensity('blackout', Math.max(0, Math.min(255, Number(value || 0))) / 255);
      },
    ),
  );

  els.performanceBlackoutMidiSlot.appendChild(
    createMidiBindRow(
      MIDI_PERFORMANCE_BLACKOUT_CONTROL_ID,
      'Blackout Hold',
      'toggle',
      async (enabled) => {
        setEffectSource('blackout', 'midi', Boolean(enabled));
      },
    ),
  );

  els.performanceRotateIntensityMidiSlot.appendChild(
    createMidiBindRow(
      MIDI_PERFORMANCE_ROTATE_INTENSITY_CONTROL_ID,
      'Rotate Intensity',
      'continuous',
      async (value) => {
        setPerformanceIntensity('rotate', Math.max(0, Math.min(255, Number(value || 0))) / 255);
      },
    ),
  );

  els.performanceRotateMidiSlot.appendChild(
    createMidiBindRow(
      MIDI_PERFORMANCE_ROTATE_CONTROL_ID,
      'Rotate Hold',
      'toggle',
      async (enabled) => {
        setEffectSource('rotate', 'midi', Boolean(enabled));
      },
    ),
  );

  els.performanceStrobeIntensityMidiSlot.appendChild(
    createMidiBindRow(
      MIDI_PERFORMANCE_STROBE_INTENSITY_CONTROL_ID,
      'Strobe Strength',
      'continuous',
      async (value) => {
        setPerformanceIntensity('strobe', Math.max(0, Math.min(255, Number(value || 0))) / 255);
      },
    ),
  );

  els.performanceStrobeMidiSlot.appendChild(
    createMidiBindRow(
      MIDI_PERFORMANCE_STROBE_CONTROL_ID,
      'Strobe Hold',
      'toggle',
      async (enabled) => {
        setEffectSource('strobe', 'midi', Boolean(enabled));
      },
    ),
  );

  updatePerformanceButtonVisuals();
}

function makeChannelRow(prefill = {}) {
  const node = els.channelEditorTemplate.content.firstElementChild.cloneNode(true);

  node.querySelector('.channel-index').value = prefill.channelIndex ?? 1;
  node.querySelector('.channel-name').value = prefill.name ?? '';
  node.querySelector('.channel-kind').value = prefill.kind ?? 'generic';
  node.querySelector('.channel-default').value = prefill.defaultValue ?? 0;

  const rangeList = node.querySelector('.range-list');
  const addRangeButton = node.querySelector('.add-range-btn');

  function addRange(range = {}) {
    const rangeNode = els.rangeEditorTemplate.content.firstElementChild.cloneNode(true);
    rangeNode.querySelector('.range-start').value = range.startValue ?? 0;
    rangeNode.querySelector('.range-end').value = range.endValue ?? 255;
    rangeNode.querySelector('.range-label').value = range.label ?? '';
    rangeNode.querySelector('.remove-range-btn').addEventListener('click', () => {
      rangeNode.remove();
    });
    rangeList.appendChild(rangeNode);
  }

  addRangeButton.addEventListener('click', () => addRange());
  node.querySelector('.remove-channel-btn').addEventListener('click', () => {
    node.remove();
    if (!els.channelEditorList.children.length) {
      els.channelEditorList.appendChild(makeChannelRow());
    }
  });

  if (prefill.ranges?.length) {
    prefill.ranges.forEach((range) => addRange(range));
  }

  return node;
}

function collectTemplateDefinition() {
  return [...els.channelEditorList.children].map((row) => {
    const ranges = [...row.querySelectorAll('.range-row')].map((rangeRow) => ({
      start_value: Number(rangeRow.querySelector('.range-start').value || 0),
      end_value: Number(rangeRow.querySelector('.range-end').value || 255),
      label: rangeRow.querySelector('.range-label').value.trim(),
    })).filter((range) => range.label.length);

    return {
      channel_index: Number(row.querySelector('.channel-index').value),
      name: row.querySelector('.channel-name').value.trim(),
      kind: row.querySelector('.channel-kind').value,
      default_value: Number(row.querySelector('.channel-default').value || 0),
      ranges,
    };
  });
}

function generateBlankChannels() {
  const count = Math.max(1, Math.min(64, Number(els.quickChannelCount.value || 1)));
  els.quickChannelCount.value = String(count);

  els.channelEditorList.innerHTML = '';
  for (let i = 1; i <= count; i += 1) {
    els.channelEditorList.appendChild(makeChannelRow({
      channelIndex: i,
      name: `CH${i}`,
      kind: 'generic',
      defaultValue: 0,
    }));
  }
}

function selectedLearnFixture() {
  const fixtureId = Number(els.learnFixtureSelect.value || 0);
  return state.fixtures.find((fixture) => fixture.id === fixtureId) || null;
}

function selectedLearnChannel() {
  const fixture = selectedLearnFixture();
  if (!fixture) return { fixture: null, channel: null };

  const channelIndex = Number(els.learnChannelSelect.value || 0);
  const channel = (fixture.channels || []).find((ch) => ch.channelIndex === channelIndex) || null;
  return { fixture, channel };
}

function syncLearnValueFields(value) {
  const clamped = Math.max(0, Math.min(255, Number(value || 0)));
  els.learnValueSlider.value = String(clamped);
  els.learnValueInput.value = String(clamped);
}

function setLearnFormDefaults() {
  syncLearnValueFields(0);
  els.learnChannelName.value = '';
  els.learnChannelKind.value = 'generic';
  els.learnRangeStart.value = '0';
  els.learnRangeEnd.value = '255';
  els.learnKindButtons.forEach((button) => {
    button.classList.remove('is-active');
    button.setAttribute('aria-pressed', 'false');
  });
}

function highlightLearnKind(kind) {
  const selectedKind = kind || 'generic';
  els.learnKindButtons.forEach((button) => {
    const active = button.dataset.kind === selectedKind;
    button.classList.toggle('is-active', active);
    button.setAttribute('aria-pressed', active ? 'true' : 'false');
  });
}

function rebuildLearnChannelSelect(fixture, preferredChannelIndex = null) {
  els.learnChannelSelect.innerHTML = '';

  const channels = fixture?.channels || [];
  channels.forEach((channel) => {
    const option = document.createElement('option');
    option.value = String(channel.channelIndex);
    option.textContent = `CH${channel.channelIndex} - ${channel.name}`;
    els.learnChannelSelect.appendChild(option);
  });

  if (!channels.length) {
    setLearnFormDefaults();
    return;
  }

  const channelToSelect = channels.find((ch) => ch.channelIndex === preferredChannelIndex) || channels[0];
  els.learnChannelSelect.value = String(channelToSelect.channelIndex);
  syncLearnValueFields(channelToSelect.value || 0);
  els.learnChannelName.value = channelToSelect.name || '';
  els.learnChannelKind.value = channelToSelect.kind || 'generic';
  highlightLearnKind(channelToSelect.kind || 'generic');
}

function refreshLearnSelectors() {
  const previousFixture = Number(els.learnFixtureSelect.value || 0);
  const previousChannel = Number(els.learnChannelSelect.value || 0);

  const fixtures = state.fixtures || [];
  els.learnFixtureSelect.innerHTML = '';
  fixtures.forEach((fixture) => {
    const option = document.createElement('option');
    option.value = String(fixture.id);
    option.textContent = `${fixture.name} (U${fixture.universe})`;
    els.learnFixtureSelect.appendChild(option);
  });

  if (!fixtures.length) {
    els.learnChannelSelect.innerHTML = '';
    setLearnFormDefaults();
    return;
  }

  const fixtureToSelect = fixtures.find((f) => f.id === previousFixture) || fixtures[0];
  els.learnFixtureSelect.value = String(fixtureToSelect.id);
  rebuildLearnChannelSelect(fixtureToSelect, previousChannel);
}

function applyLearnChannelInfoToForm() {
  const { channel } = selectedLearnChannel();
  if (!channel) {
    setLearnFormDefaults();
    return;
  }
  syncLearnValueFields(channel.value || 0);
  els.learnChannelName.value = channel.name || '';
  els.learnChannelKind.value = channel.kind || 'generic';
  highlightLearnKind(channel.kind || 'generic');
}

function updatePatchRangePreview() {
  const start = Number(els.fixtureStartAddress.value || 1);
  const selectedTemplateId = Number(els.fixtureTemplateSelect.value || 0);
  const selectedTemplate = state.templates.find((t) => t.id === selectedTemplateId);

  const manualCount = Number(els.fixtureChannelCount.value || 0);
  const count = manualCount > 0 ? manualCount : (selectedTemplate?.footprintChannels || 1);
  const end = Math.min(512, Math.max(start, start + count - 1));

  els.patchRangeText.textContent = `${start} - ${end}`;
}

function renderTemplates(templates) {
  state.templates = templates;
  const previousTemplateValue = els.fixtureTemplateSelect.value;

  els.templateList.innerHTML = '';
  els.fixtureTemplateSelect.innerHTML = '';

  if (!templates.length) {
    const option = document.createElement('option');
    option.value = '';
    option.textContent = 'No templates yet';
    els.fixtureTemplateSelect.appendChild(option);
    updatePatchRangePreview();
    return;
  }

  templates.forEach((template) => {
    const option = document.createElement('option');
    option.value = String(template.id);
    option.textContent = `${template.name} (${template.footprintChannels}ch)`;
    els.fixtureTemplateSelect.appendChild(option);

    const pill = document.createElement('div');
    pill.className = 'template-pill';
    pill.innerHTML = `
      <strong>${escapeHtml(template.name)}</strong>
      <p>${template.footprintChannels} channels • ${escapeHtml(template.description || 'No description')}</p>
    `;
    els.templateList.appendChild(pill);
  });

  const preferredTemplateValue = templates.some((template) => String(template.id) === previousTemplateValue)
    ? previousTemplateValue
    : String(templates[0].id);
  els.fixtureTemplateSelect.value = preferredTemplateValue;

  updatePatchRangePreview();
}

function renderStatus(dmx, audio) {
  state.dmx = dmx;
  state.audio = audio;

  if (dmx.connected) {
    els.deviceStatus.textContent = 'ENTTEC DMX USB Pro detected';
    els.deviceStatus.style.color = '#0b7266';
  } else {
    els.deviceStatus.textContent = `Device not connected. ${dmx.lastError || ''}`.trim();
    els.deviceStatus.style.color = '#b7432b';
  }

  els.devicePort.textContent = `Port: ${dmx.port || '--'}`;
  els.deviceSerial.textContent = `Serial: ${dmx.serial || '--'}`;
  els.deviceFw.textContent = `Firmware: ${dmx.firmwareMajor}.${dmx.firmwareMinor}`;
  els.audioBackend.textContent = `Audio Backend: ${audio.backend || '--'}`;
  const liveEnergy = Math.max(0, Math.min(1, Number(audio.energy || 0)));
  els.audioEnergy.textContent = `Energy: ${liveEnergy.toFixed(2)}`;
  els.audioBass.textContent = `Bass: ${Number(audio.bass || 0).toFixed(2)} • Treble: ${Number(audio.treble || 0).toFixed(2)}`;
  els.audioBpm.textContent = `BPM: ${Number(audio.bpm || 0).toFixed(1)} • Beat: ${audio.beat ? 'Yes' : 'No'}`;
  const reactiveThreshold = Math.max(0, Math.min(1, Number(audio.reactiveVolumeThreshold ?? 0.12)));
  const reactiveProfile = String(audio.reactiveProfile || 'balanced');
  if (els.audioThreshold) {
    els.audioThreshold.textContent = `Reactive Threshold: ${reactiveThreshold.toFixed(2)}`;
  }
  if (els.reactiveProfileSelect && document.activeElement !== els.reactiveProfileSelect) {
    els.reactiveProfileSelect.value = reactiveProfile === 'volume_blackout' ? 'volume_blackout' : 'balanced';
  }

  if (els.reactiveLiveEnergy) {
    els.reactiveLiveEnergy.value = liveEnergy.toFixed(2);
  }
  if (els.reactiveLiveEnergyValue) {
    els.reactiveLiveEnergyValue.textContent = liveEnergy.toFixed(2);
  }

  if (els.reactiveThreshold && document.activeElement !== els.reactiveThreshold) {
    els.reactiveThreshold.value = reactiveThreshold.toFixed(2);
  }
  if (els.reactiveThresholdValue) {
    const currentThreshold = els.reactiveThreshold ? Number(els.reactiveThreshold.value || reactiveThreshold) : reactiveThreshold;
    els.reactiveThresholdValue.textContent = Math.max(0, Math.min(1, currentThreshold)).toFixed(2);
  }

  els.audioToggle.textContent = `Music Reactive: ${audio.reactiveMode ? 'On' : 'Off'}`;
  els.audioToggle.classList.toggle('primary', audio.reactiveMode);
  els.audioToggle.classList.toggle('accent', !audio.reactiveMode);

  const inputDevices = Array.isArray(audio.inputDevices) ? audio.inputDevices : [];
  const defaultInputDeviceId = Number(audio.defaultInputDeviceId ?? -1);
  const selectedInputDeviceId = Number(audio.selectedInputDeviceId ?? -1);
  const activeInputDeviceId = Number(audio.activeInputDeviceId ?? -2);
  const previousAudioInputChoice = Number(els.audioInputSelect.value || NaN);
  const deviceById = new Map(inputDevices.map((device) => [Number(device.id), device]));
  const defaultDeviceName = deviceById.get(defaultInputDeviceId)?.name || 'None';

  els.audioInputSelect.innerHTML = '';

  const defaultOption = document.createElement('option');
  defaultOption.value = '-1';
  defaultOption.textContent = `Default Input (${defaultDeviceName})`;
  els.audioInputSelect.appendChild(defaultOption);

  inputDevices.forEach((device) => {
    const option = document.createElement('option');
    option.value = String(device.id);
    option.textContent = `${device.name}${device.isDefault ? ' • default' : ''}`;
    els.audioInputSelect.appendChild(option);
  });

  const hasSelectedOption =
    selectedInputDeviceId === -1 || inputDevices.some((device) => Number(device.id) === selectedInputDeviceId);
  const hasPreviousChoice = Number.isFinite(previousAudioInputChoice)
    && (previousAudioInputChoice === -1
      || inputDevices.some((device) => Number(device.id) === previousAudioInputChoice));
  const uiChoice = hasPreviousChoice
    ? previousAudioInputChoice
    : (hasSelectedOption ? selectedInputDeviceId : -1);
  els.audioInputSelect.value = String(uiChoice);

  const selectedLabel = selectedInputDeviceId === -1
    ? `Default (${defaultDeviceName})`
    : (deviceById.get(selectedInputDeviceId)?.name || `Device ${selectedInputDeviceId}`);
  const activeLabel = activeInputDeviceId >= 0
    ? (deviceById.get(activeInputDeviceId)?.name || `Device ${activeInputDeviceId}`)
    : 'Simulated fallback';
  let pendingLabel = '';
  if (uiChoice !== selectedInputDeviceId) {
    const pendingName = uiChoice === -1
      ? `Default (${defaultDeviceName})`
      : (deviceById.get(uiChoice)?.name || `Device ${uiChoice}`);
    pendingLabel = ` • Pending: ${pendingName}`;
  }
  els.audioInputLabel.textContent = `Selected: ${selectedLabel} • Active: ${activeLabel}${pendingLabel}`;

  const canSelectInput = inputDevices.length > 0;
  els.audioInputSelect.disabled = !canSelectInput;
  els.applyAudioInputBtn.disabled = !canSelectInput;

  const known = [...new Set((dmx.knownUniverses || [1]).map((u) => Number(u)).filter((u) => u > 0))].sort((a, b) => a - b);
  const current = Number(dmx.outputUniverse || 1);
  if (!known.includes(current)) known.push(current);

  const before = Number(els.outputUniverseSelect.value || 0);
  els.outputUniverseSelect.innerHTML = '';
  known.forEach((universe) => {
    const option = document.createElement('option');
    option.value = String(universe);
    option.textContent = `Universe ${universe}`;
    els.outputUniverseSelect.appendChild(option);
  });
  els.outputUniverseSelect.value = String(before || current);
  els.outputUniverseLabel.textContent = `Current: U${current}`;
  els.universeHelp.textContent =
    'Each universe is a separate 512-channel DMX space. Create one, patch fixtures to it, then switch output.';

  const nextUniverseDefault = (known.length ? Math.max(...known) : 1) + 1;
  const currentInput = Number(els.newUniverseNumber.value || 0);
  if (currentInput < 1) {
    els.newUniverseNumber.value = String(nextUniverseDefault);
  }

  updateMidiTargetUi(MIDI_REACTIVE_CONTROL_ID);
}

function escapeHtml(text = '') {
  return text
    .replaceAll('&', '&amp;')
    .replaceAll('<', '&lt;')
    .replaceAll('>', '&gt;')
    .replaceAll('"', '&quot;')
    .replaceAll("'", '&#039;');
}

function createSlider(initialValue, onChange) {
  let value = Math.max(0, Math.min(255, Number(initialValue || 0)));

  const wrapper = document.createElement('div');
  wrapper.className = 'control-widget slider-widget';

  const slider = document.createElement('input');
  slider.type = 'range';
  slider.className = 'channel-slider';
  slider.min = '0';
  slider.max = '255';
  slider.step = '1';

  const valueEl = document.createElement('div');
  valueEl.className = 'slider-value';

  function applyValue(next, send = false) {
    value = Math.max(0, Math.min(255, Math.round(next)));
    slider.value = String(value);
    valueEl.textContent = String(value);
    if (send) {
      onChange(value);
    }
  }

  slider.addEventListener('input', () => {
    applyValue(slider.value, true);
  });

  slider.addEventListener('change', () => {
    onChange(value);
  });

  applyValue(value, false);
  wrapper.append(slider, valueEl);

  return {
    element: wrapper,
    setValue(next) {
      applyValue(next, false);
    },
    getValue() {
      return value;
    },
  };
}

function createKnob(initialValue, onChange) {
  let value = Math.max(0, Math.min(255, Number(initialValue || 0)));
  let raf = null;

  const wrapper = document.createElement('div');
  wrapper.className = 'control-widget knob-widget';
  const knob = document.createElement('div');
  knob.className = 'knob';
  knob.setAttribute('role', 'slider');
  knob.setAttribute('tabindex', '0');
  knob.setAttribute('aria-valuemin', '0');
  knob.setAttribute('aria-valuemax', '255');

  const valueEl = document.createElement('div');
  valueEl.className = 'knob-value';

  function applyValue(next, send = false) {
    value = Math.max(0, Math.min(255, Math.round(next)));
    const angle = -145 + (value / 255) * 290;
    knob.style.setProperty('--angle', `${angle.toFixed(1)}deg`);
    knob.setAttribute('aria-valuenow', String(value));
    valueEl.textContent = String(value);

    if (send) {
      onChange(value);
    }
  }

  function pointToValue(event) {
    const rect = knob.getBoundingClientRect();
    const cx = rect.left + rect.width / 2;
    const cy = rect.top + rect.height / 2;
    const dx = event.clientX - cx;
    const dy = event.clientY - cy;

    let angle = (Math.atan2(dy, dx) * 180) / Math.PI + 90;
    if (angle < -180) angle += 360;
    if (angle > 180) angle -= 360;

    angle = Math.max(-145, Math.min(145, angle));
    return ((angle + 145) / 290) * 255;
  }

  function scheduleSend() {
    if (raf) return;
    raf = requestAnimationFrame(() => {
      raf = null;
      onChange(value);
    });
  }

  knob.addEventListener('pointerdown', (event) => {
    knob.setPointerCapture(event.pointerId);
    applyValue(pointToValue(event), true);

    const onMove = (moveEvent) => {
      applyValue(pointToValue(moveEvent), false);
      scheduleSend();
    };

    const onUp = () => {
      knob.removeEventListener('pointermove', onMove);
      knob.removeEventListener('pointerup', onUp);
      knob.removeEventListener('pointercancel', onUp);
      onChange(value);
    };

    knob.addEventListener('pointermove', onMove);
    knob.addEventListener('pointerup', onUp);
    knob.addEventListener('pointercancel', onUp);
  });

  knob.addEventListener('keydown', (event) => {
    if (event.key === 'ArrowUp' || event.key === 'ArrowRight') {
      event.preventDefault();
      applyValue(value + 2, true);
    }
    if (event.key === 'ArrowDown' || event.key === 'ArrowLeft') {
      event.preventDefault();
      applyValue(value - 2, true);
    }
  });

  knob.addEventListener('wheel', (event) => {
    event.preventDefault();
    const delta = event.deltaY > 0 ? -3 : 3;
    applyValue(value + delta, true);
  }, { passive: false });

  applyValue(value, false);
  wrapper.append(knob, valueEl);
  return {
    element: wrapper,
    setValue(next) {
      applyValue(next, false);
    },
    getValue() {
      return value;
    },
  };
}

function createChannelControl(initialValue, onChange) {
  return state.controlMode === 'knob'
    ? createKnob(initialValue, onChange)
    : createSlider(initialValue, onChange);
}

function renderFixtures(fixtures) {
  state.fixtures = fixtures;
  els.fixtureBoard.innerHTML = '';
  unregisterMidiTargetsByPrefix('fixture:');

  if (!fixtures.length) {
    const empty = document.createElement('p');
    empty.textContent = 'No fixtures patched yet. Add one from the Patch Fixture panel.';
    empty.style.color = '#4f6660';
    els.fixtureBoard.appendChild(empty);
    refreshLearnSelectors();
    return;
  }

  fixtures.forEach((fixture) => {
    const card = document.createElement('article');
    card.className = 'fixture-card';

    const head = document.createElement('div');
    head.className = 'fixture-head';

    const titleWrap = document.createElement('div');
    titleWrap.innerHTML = `
      <h3>${escapeHtml(fixture.name)}</h3>
      <p class="fixture-meta">${escapeHtml(fixture.templateName)} • U${fixture.universe} • ${fixture.startAddress}-${fixture.startAddress + fixture.channelCount - 1}</p>
    `;

    const toggleLabel = document.createElement('label');
    toggleLabel.className = 'fixture-toggle';

    const toggle = document.createElement('input');
    toggle.type = 'checkbox';
    toggle.checked = Boolean(fixture.enabled);
    toggle.addEventListener('change', async () => {
      try {
        await api(`/api/fixtures/${fixture.id}/enabled`, {
          method: 'POST',
          body: { enabled: toggle.checked ? '1' : '0' },
        });
        showToast(`${fixture.name} ${toggle.checked ? 'enabled' : 'disabled'}`);
      } catch (err) {
        toggle.checked = !toggle.checked;
        showToast(err.message, 'error');
      }
    });

    toggleLabel.append(toggle, document.createTextNode(fixture.enabled ? 'Enabled' : 'Disabled'));
    toggle.addEventListener('change', () => {
      toggleLabel.lastChild.textContent = toggle.checked ? 'Enabled' : 'Disabled';
    });

    head.append(titleWrap, toggleLabel);

    const channelGrid = document.createElement('div');
    channelGrid.className = 'channel-grid';

    fixture.channels.forEach((channel) => {
      const channelCard = document.createElement('div');
      channelCard.className = 'channel-card';

      const titleWrapRow = document.createElement('div');
      titleWrapRow.className = 'channel-title';
      titleWrapRow.innerHTML = `${iconBadge(channel.kind)}<span>${escapeHtml(channel.name)}</span>`;

      const sub = document.createElement('p');
      sub.className = 'channel-sub';
      sub.textContent = `CH${channel.channelIndex}`;

      let sendTimer = null;
      const sendValue = (value) => {
        setLocalChannelValue(fixture.id, channel.channelIndex, value);
        window.clearTimeout(sendTimer);
        sendTimer = window.setTimeout(async () => {
          try {
            await api(`/api/fixtures/${fixture.id}/channels/${channel.channelIndex}`, {
              method: 'POST',
              body: { value: String(value) },
            });
          } catch (err) {
            showToast(err.message, 'error');
          }
        }, 35);
      };

      const control = createChannelControl(channel.value, sendValue);
      const midiControlId = `fixture:${fixture.id}:ch:${channel.channelIndex}`;
      const midiRow = createMidiBindRow(
        midiControlId,
        `${fixture.name} CH${channel.channelIndex}`,
        'continuous',
        async (value) => {
          control.setValue(value);
          sendValue(value);
        },
      );

      const rangeChipWrap = document.createElement('div');
      rangeChipWrap.className = 'range-chip-wrap';

      (channel.ranges || []).forEach((range) => {
        const chip = document.createElement('button');
        chip.type = 'button';
        chip.className = 'range-chip';
        chip.title = `${range.startValue}-${range.endValue}`;
        chip.innerHTML = `${iconBadge(range.label, true)}<span>${escapeHtml(range.label)}</span>`;
        chip.addEventListener('click', () => {
          const mid = Math.round((range.startValue + range.endValue) / 2);
          control.setValue(mid);
          sendValue(mid);
        });
        rangeChipWrap.appendChild(chip);
      });

      channelCard.append(titleWrapRow, sub, control.element);
      if ((channel.ranges || []).length) {
        channelCard.appendChild(rangeChipWrap);
      }
      channelCard.appendChild(midiRow);
      channelGrid.appendChild(channelCard);
    });

    card.append(head, channelGrid);
    els.fixtureBoard.appendChild(card);
  });

  refreshLearnSelectors();
}

function getGroupFixtures(group) {
  const memberSet = new Set((group.fixtureIds || []).map(Number));
  return state.fixtures.filter((fixture) => memberSet.has(Number(fixture.id)));
}

function collectKindsForGroup(groupFixtures) {
  const kinds = new Set();
  groupFixtures.forEach((fixture) => {
    fixture.channels.forEach((channel) => {
      if (channel.kind && channel.kind !== 'generic') kinds.add(channel.kind);
    });
  });
  return [...kinds].sort();
}

function collectModesForGroup(groupFixtures) {
  const labels = new Set();
  groupFixtures.forEach((fixture) => {
    fixture.channels.forEach((channel) => {
      if (channel.kind !== 'mode') return;
      (channel.ranges || []).forEach((range) => labels.add(range.label));
    });
  });
  return [...labels];
}

function renderGroups(groups) {
  state.groups = groups;
  els.groupBoard.innerHTML = '';
  unregisterMidiTargetsByPrefix('group:');

  if (!groups.length) {
    const empty = document.createElement('p');
    empty.textContent = 'No groups yet. Create one and assign fixtures.';
    empty.style.color = '#4f6660';
    els.groupBoard.appendChild(empty);
    return;
  }

  groups.forEach((group) => {
    const card = document.createElement('article');
    card.className = 'group-card';

    const head = document.createElement('div');
    head.className = 'group-head';

    const title = document.createElement('h3');
    title.textContent = group.name;

    const deleteBtn = document.createElement('button');
    deleteBtn.type = 'button';
    deleteBtn.className = 'btn danger';
    deleteBtn.textContent = 'Delete';
    deleteBtn.addEventListener('click', async () => {
      try {
        await api(`/api/groups/${group.id}/delete`, { method: 'POST' });
        await loadState({ silent: true });
        showToast(`Deleted group "${group.name}"`);
      } catch (err) {
        showToast(err.message, 'error');
      }
    });

    head.append(title, deleteBtn);

    const fixturesWrap = document.createElement('div');
    fixturesWrap.className = 'group-fixtures';

    state.fixtures.forEach((fixture) => {
      const item = document.createElement('label');
      item.className = 'group-fixture-item';

      const checkbox = document.createElement('input');
      checkbox.type = 'checkbox';
      checkbox.checked = (group.fixtureIds || []).includes(fixture.id);
      checkbox.value = String(fixture.id);

      const text = document.createElement('span');
      text.textContent = `${fixture.name} (U${fixture.universe})`;

      item.append(checkbox, text);
      fixturesWrap.appendChild(item);
    });

    const saveMembersBtn = document.createElement('button');
    saveMembersBtn.type = 'button';
    saveMembersBtn.className = 'btn ghost';
    saveMembersBtn.textContent = 'Save Group Members';
    saveMembersBtn.addEventListener('click', async () => {
      const ids = [...fixturesWrap.querySelectorAll('input[type="checkbox"]:checked')].map((i) => i.value);
      try {
        await api(`/api/groups/${group.id}/fixtures`, {
          method: 'POST',
          body: { fixture_ids: ids.join(',') },
        });
        await loadState({ silent: true });
        showToast(`Updated fixtures for ${group.name}`);
      } catch (err) {
        showToast(err.message, 'error');
      }
    });

    const groupFixtures = getGroupFixtures(group);
    const kinds = collectKindsForGroup(groupFixtures);
    const controls = document.createElement('div');
    controls.className = 'group-controls';

    if (kinds.length) {
      const kindGrid = document.createElement('div');
      kindGrid.className = 'group-control-grid';

      kinds.forEach((kind) => {
        const cell = document.createElement('div');
        cell.className = 'channel-card';

        const titleRow = document.createElement('div');
        titleRow.className = 'channel-title';
        titleRow.innerHTML = `${iconBadge(kind)}<span>${escapeHtml(kind)}</span>`;

        const values = [];
        groupFixtures.forEach((fixture) => {
          fixture.channels.forEach((channel) => {
            if (channel.kind === kind) values.push(Number(channel.value || 0));
          });
        });
        const initialValue = values.length
          ? Math.round(values.reduce((sum, v) => sum + v, 0) / values.length)
          : 128;

        let sendTimer = null;
        const sendValue = (value) => {
          window.clearTimeout(sendTimer);
          sendTimer = window.setTimeout(() => {
            api(`/api/groups/${group.id}/kinds/${kind}`, {
              method: 'POST',
              body: { value: String(value) },
            }).catch((err) => showToast(err.message, 'error'));
          }, 40);
        };

        const control = createChannelControl(initialValue, sendValue);
        const midiControlId = `group:${group.id}:kind:${kind}`;
        const midiRow = createMidiBindRow(
          midiControlId,
          `${group.name} ${kind}`,
          'continuous',
          async (value) => {
            control.setValue(value);
            sendValue(value);
          },
        );

        cell.append(titleRow, control.element, midiRow);
        kindGrid.appendChild(cell);
      });

      controls.appendChild(kindGrid);
    }

    const modeLabels = collectModesForGroup(groupFixtures);
    if (modeLabels.length) {
      const modeWrap = document.createElement('div');
      modeWrap.className = 'mode-chip-wrap';

      modeLabels.forEach((label) => {
        const chip = document.createElement('button');
        chip.type = 'button';
        chip.className = 'group-mode-chip';
        chip.innerHTML = `${iconBadge(label, true)}<span>${escapeHtml(label)}</span>`;
        chip.addEventListener('click', async () => {
          try {
            await api(`/api/groups/${group.id}/mode`, {
              method: 'POST',
              body: { label },
            });
            await loadState({ silent: true });
            showToast(`${group.name}: mode ${label}`);
          } catch (err) {
            showToast(err.message, 'error');
          }
        });
        modeWrap.appendChild(chip);
      });

      controls.appendChild(modeWrap);
    }

    card.append(head, fixturesWrap, saveMembersBtn, controls);
    els.groupBoard.appendChild(card);
  });
}

async function loadState({ silent = false } = {}) {
  try {
    const data = await api('/api/state');
    renderStatus(data.dmx, data.audio);
    renderTemplates(data.templates || []);
    renderFixtures(data.fixtures || []);
    renderGroups(data.groups || []);
  } catch (err) {
    if (!silent) {
      showToast(err.message, 'error');
    }
  }
}

async function onTemplateSubmit(event) {
  event.preventDefault();

  const formData = new FormData(els.templateForm);
  const name = (formData.get('name') || '').toString().trim();
  const description = (formData.get('description') || '').toString().trim();

  if (!name) {
    showToast('Template name is required', 'error');
    return;
  }

  const channels = collectTemplateDefinition();
  if (!channels.length) {
    showToast('Add at least one channel', 'error');
    return;
  }

  for (const channel of channels) {
    if (!channel.name || !Number.isFinite(channel.channel_index) || channel.channel_index < 1) {
      showToast('Each channel needs a valid index and name', 'error');
      return;
    }
  }

  try {
    const templateResp = await api('/api/templates', {
      method: 'POST',
      body: { name, description },
    });

    const templateId = templateResp.templateId;

    for (const channel of channels) {
      const channelResp = await api(`/api/templates/${templateId}/channels`, {
        method: 'POST',
        body: {
          channel_index: String(channel.channel_index),
          name: channel.name,
          kind: channel.kind,
          default_value: String(channel.default_value || 0),
        },
      });

      for (const range of channel.ranges) {
        await api(`/api/channels/${channelResp.channelId}/ranges`, {
          method: 'POST',
          body: {
            start_value: String(range.start_value),
            end_value: String(range.end_value),
            label: range.label,
          },
        });
      }
    }

    showToast(`Template "${name}" saved`);
    els.templateForm.reset();
    els.channelEditorList.innerHTML = '';
    els.channelEditorList.appendChild(makeChannelRow());
    await loadState({ silent: true });
  } catch (err) {
    showToast(err.message, 'error');
  }
}

async function onFixtureSubmit(event) {
  event.preventDefault();

  const formData = new FormData(els.fixtureForm);
  const payload = {
    name: String(formData.get('name') || '').trim(),
    template_id: String(formData.get('template_id') || ''),
    universe: String(formData.get('universe') || '1'),
    start_address: String(formData.get('start_address') || '1'),
    channel_count: String(formData.get('channel_count') || ''),
  };

  if (!payload.name || !payload.template_id || !payload.start_address) {
    showToast('Fixture name, template, and start address are required', 'error');
    return;
  }

  try {
    await api('/api/fixtures', {
      method: 'POST',
      body: payload,
    });

    showToast(`Fixture "${payload.name}" patched`);
    els.fixtureForm.reset();
    els.fixtureStartAddress.value = '1';
    await loadState({ silent: true });
  } catch (err) {
    showToast(err.message, 'error');
  }
}

async function onGroupSubmit(event) {
  event.preventDefault();
  const formData = new FormData(els.groupForm);
  const name = String(formData.get('name') || '').trim();
  if (!name) {
    showToast('Group name is required', 'error');
    return;
  }

  try {
    await api('/api/groups', {
      method: 'POST',
      body: { name },
    });
    els.groupForm.reset();
    await loadState({ silent: true });
    showToast(`Group "${name}" created`);
  } catch (err) {
    showToast(err.message, 'error');
  }
}

async function onLearnCreateSubmit(event) {
  event.preventDefault();

  const formData = new FormData(els.learnCreateForm);
  const rawTemplateName = String(formData.get('template_name') || '').trim();
  const templateName = rawTemplateName || `Learn Fixture ${new Date().toLocaleDateString()}`;
  const fixtureName = String(formData.get('fixture_name') || '').trim() || `${templateName} Test`;
  const channelCount = Math.max(1, Math.min(64, Number(formData.get('channel_count') || 1)));
  const universe = Math.max(1, Number(formData.get('universe') || 1));
  const startAddress = Math.max(1, Math.min(512, Number(formData.get('start_address') || 1)));

  let finalTemplateName = templateName;
  const existing = new Set(state.templates.map((t) => String(t.name || '').toLowerCase()));
  let suffix = 1;
  while (existing.has(finalTemplateName.toLowerCase())) {
    suffix += 1;
    finalTemplateName = `${templateName} (learn ${suffix})`;
  }

  try {
    const templateResp = await api('/api/templates', {
      method: 'POST',
      body: {
        name: finalTemplateName,
        description: `Learn-mode generated template (${channelCount} channels).`,
      },
    });

    for (let channelIndex = 1; channelIndex <= channelCount; channelIndex += 1) {
      await api(`/api/templates/${templateResp.templateId}/channels`, {
        method: 'POST',
        body: {
          channel_index: String(channelIndex),
          name: `CH${channelIndex}`,
          kind: 'generic',
          default_value: '0',
        },
      });
    }

    await api('/api/fixtures', {
      method: 'POST',
      body: {
        name: fixtureName,
        template_id: String(templateResp.templateId),
        universe: String(universe),
        start_address: String(startAddress),
        channel_count: String(channelCount),
      },
    });

    showToast(`Learn fixture "${fixtureName}" created`);
    await loadState({ silent: true });
  } catch (err) {
    showToast(err.message, 'error');
  }
}

function queueLearnChannelValueSend(value) {
  const { fixture, channel } = selectedLearnChannel();
  if (!fixture || !channel) return;

  const dmxValue = Math.max(0, Math.min(255, Number(value || 0)));
  syncLearnValueFields(dmxValue);
  channel.value = dmxValue;

  window.clearTimeout(state.learnSendTimer);
  state.learnSendTimer = window.setTimeout(async () => {
    try {
      await api(`/api/fixtures/${fixture.id}/channels/${channel.channelIndex}`, {
        method: 'POST',
        body: { value: String(dmxValue) },
      });
    } catch (err) {
      showToast(err.message, 'error');
    }
  }, 35);
}

async function saveLearnChannelMeta() {
  const { channel } = selectedLearnChannel();
  if (!channel || !channel.channelId) {
    showToast('Select a channel from a fixture with template mapping', 'error');
    return;
  }

  const name = String(els.learnChannelName.value || '').trim();
  const kind = String(els.learnChannelKind.value || 'generic').trim();
  const defaultValue = Number(els.learnValueInput.value || 0);

  if (!name) {
    showToast('Channel name is required', 'error');
    return;
  }

  try {
    await api(`/api/channels/${channel.channelId}/update`, {
      method: 'POST',
      body: {
        name,
        kind,
        default_value: String(defaultValue),
      },
    });
    await loadState({ silent: true });
    showToast(`Saved metadata for CH${channel.channelIndex}`);
  } catch (err) {
    showToast(err.message, 'error');
  }
}

async function clearLearnChannelRanges() {
  const { channel } = selectedLearnChannel();
  if (!channel || !channel.channelId) {
    showToast('Select a channel first', 'error');
    return;
  }

  try {
    await api(`/api/channels/${channel.channelId}/ranges/clear`, {
      method: 'POST',
    });
    await loadState({ silent: true });
    showToast(`Cleared ranges for CH${channel.channelIndex}`);
  } catch (err) {
    showToast(err.message, 'error');
  }
}

async function addLearnChannelRange() {
  const { channel } = selectedLearnChannel();
  if (!channel || !channel.channelId) {
    showToast('Select a channel first', 'error');
    return;
  }

  let start = Number(els.learnRangeStart.value || 0);
  let end = Number(els.learnRangeEnd.value || 0);
  const label = String(els.learnRangeLabel.value || '').trim();
  if (!label) {
    showToast('Range label is required', 'error');
    return;
  }
  if (end < start) [start, end] = [end, start];

  try {
    await api(`/api/channels/${channel.channelId}/ranges`, {
      method: 'POST',
      body: {
        start_value: String(start),
        end_value: String(end),
        label,
      },
    });
    await loadState({ silent: true });
    showToast(`Added range ${start}-${end} for CH${channel.channelIndex}`);
  } catch (err) {
    showToast(err.message, 'error');
  }
}

async function toggleReactiveMode() {
  try {
    const enabled = !Boolean(state.audio?.reactiveMode);
    await api('/api/audio/reactive', {
      method: 'POST',
      body: { enabled: enabled ? '1' : '0' },
    });
    await loadState({ silent: true });
    showToast(`Music reactive ${enabled ? 'enabled' : 'disabled'}`);
  } catch (err) {
    showToast(err.message, 'error');
  }
}

async function setReactiveModeFromMidi(enabled) {
  const desired = Boolean(enabled);
  const current = Boolean(state.audio?.reactiveMode);
  if (current === desired) return;

  if (state.midi.reactiveRequestInFlight) return;
  state.midi.reactiveRequestInFlight = true;

  try {
    await api('/api/audio/reactive', {
      method: 'POST',
      body: { enabled: desired ? '1' : '0' },
    });
    await loadState({ silent: true });
  } finally {
    state.midi.reactiveRequestInFlight = false;
  }
}

async function applyReactiveProfile({ notify = true } = {}) {
  if (!els.reactiveProfileSelect) return;
  const profile = els.reactiveProfileSelect.value === 'volume_blackout' ? 'volume_blackout' : 'balanced';

  try {
    await api('/api/audio/reactive-profile', {
      method: 'POST',
      body: { profile },
    });
    if (state.audio) {
      state.audio.reactiveProfile = profile;
    }
    if (notify) {
      showToast(`Reactive profile: ${profile === 'volume_blackout' ? 'Volume Blackout' : 'Balanced'}`);
    }
  } catch (err) {
    showToast(err.message, 'error');
  }
}

async function applyReactiveThreshold({ notify = false } = {}) {
  if (!els.reactiveThreshold) return;

  const threshold = Number(els.reactiveThreshold.value || 0);
  if (!Number.isFinite(threshold)) {
    showToast('Threshold must be a number between 0 and 1', 'error');
    return;
  }

  const normalized = Math.max(0, Math.min(1, threshold));
  if (Math.abs(normalized - threshold) > 0.0001) {
    els.reactiveThreshold.value = normalized.toFixed(2);
  }
  if (els.reactiveThresholdValue) {
    els.reactiveThresholdValue.textContent = normalized.toFixed(2);
  }

  try {
    await api('/api/audio/reactive-threshold', {
      method: 'POST',
      body: { threshold: String(normalized) },
    });
    if (state.audio) {
      state.audio.reactiveVolumeThreshold = normalized;
    }
    if (notify) {
      showToast(`Reactive threshold set to ${normalized.toFixed(2)}`);
    }
  } catch (err) {
    showToast(err.message, 'error');
  }
}

function queueReactiveThresholdApply() {
  window.clearTimeout(state.reactiveThresholdTimer);
  state.reactiveThresholdTimer = window.setTimeout(() => {
    applyReactiveThreshold().catch((err) => showToast(err.message, 'error'));
  }, 140);
}

async function applyAudioInputDevice() {
  const deviceId = Number(els.audioInputSelect.value || -1);
  try {
    await api('/api/audio/input-device', {
      method: 'POST',
      body: { device_id: String(deviceId) },
    });
    await loadState({ silent: true });
    showToast('Audio input selection updated');
  } catch (err) {
    showToast(err.message, 'error');
  }
}

async function applyOutputUniverse() {
  try {
    const universe = Number(els.outputUniverseSelect.value || 1);
    await api('/api/dmx/output-universe', {
      method: 'POST',
      body: { universe: String(universe) },
    });
    await loadState({ silent: true });
    showToast(`DMX output now routed to Universe ${universe}`);
  } catch (err) {
    showToast(err.message, 'error');
  }
}

function releasePerformanceEffectsImmediately() {
  if (state.performance.rafId) {
    window.cancelAnimationFrame(state.performance.rafId);
  }

  Object.values(state.performance.effectByName).forEach((effect) => {
    effect.holdSources.ui = false;
    effect.holdSources.midi = false;
    effect.envelope = 0;
    effect.snapshot = null;
    effect.justSettled = false;
  });

  state.performance.active = false;
  state.performance.rafId = null;
  state.performance.lastTickMs = 0;
  state.performance.pendingPatchPayload = '';
  state.performance.lastPatchPayload = '';
  updatePerformanceButtonVisuals();
}

async function panicBlackout() {
  try {
    releasePerformanceEffectsImmediately();

    // Send blackout twice to override any in-flight non-zero patch from a previous frame.
    await api('/api/dmx/blackout', { method: 'POST' });
    await new Promise((resolve) => window.setTimeout(resolve, 120));
    await api('/api/dmx/blackout', { method: 'POST' });

    (state.fixtures || []).forEach((fixture) => {
      (fixture.channels || []).forEach((channel) => {
        channel.value = 0;
      });
    });
    renderFixtures(state.fixtures || []);

    await loadState({ silent: true });
    showToast('Panic blackout applied');
  } catch (err) {
    showToast(err.message, 'error');
  }
}

async function createUniverse() {
  const universe = Number(els.newUniverseNumber.value || 0);
  if (!Number.isFinite(universe) || universe < 1) {
    showToast('Universe must be a number >= 1', 'error');
    return;
  }

  try {
    await api('/api/dmx/universes', {
      method: 'POST',
      body: { universe: String(universe) },
    });
    await loadState({ silent: true });
    showToast(`Universe ${universe} created`);
  } catch (err) {
    showToast(err.message, 'error');
  }
}

async function exportTemplates() {
  try {
    const data = await api('/api/templates/export');
    const blob = new Blob([JSON.stringify({ templates: data.templates || [] }, null, 2)], {
      type: 'application/json',
    });
    const url = URL.createObjectURL(blob);
    const link = document.createElement('a');
    link.href = url;
    link.download = `tuxdmx-templates-${new Date().toISOString().slice(0, 10)}.json`;
    document.body.appendChild(link);
    link.click();
    link.remove();
    URL.revokeObjectURL(url);
    showToast('Templates exported');
  } catch (err) {
    showToast(err.message, 'error');
  }
}

async function importTemplatesFromFile(file) {
  if (!file) return;

  let data;
  try {
    data = JSON.parse(await file.text());
  } catch {
    showToast('Invalid JSON file', 'error');
    return;
  }

  const templates = Array.isArray(data) ? data : (Array.isArray(data.templates) ? data.templates : []);
  if (!templates.length) {
    showToast('No templates found in file', 'error');
    return;
  }

  let imported = 0;

  for (const template of templates) {
    const channels = Array.isArray(template.channels) ? template.channels : [];
    if (!template.name || !channels.length) continue;

    let name = String(template.name).trim();
    const existingNames = new Set(state.templates.map((t) => t.name.toLowerCase()));
    let suffix = 1;
    while (existingNames.has(name.toLowerCase())) {
      suffix += 1;
      name = `${template.name} (import ${suffix})`;
    }

    try {
      const created = await api('/api/templates', {
        method: 'POST',
        body: {
          name,
          description: String(template.description || ''),
        },
      });

      for (const channel of channels) {
        const createdChannel = await api(`/api/templates/${created.templateId}/channels`, {
          method: 'POST',
          body: {
            channel_index: String(channel.channelIndex || channel.channel_index || 1),
            name: String(channel.name || 'Channel'),
            kind: String(channel.kind || 'generic'),
            default_value: String(channel.defaultValue ?? channel.default_value ?? 0),
          },
        });

        const ranges = Array.isArray(channel.ranges) ? channel.ranges : [];
        for (const range of ranges) {
          await api(`/api/channels/${createdChannel.channelId}/ranges`, {
            method: 'POST',
            body: {
              start_value: String(range.startValue ?? range.start_value ?? 0),
              end_value: String(range.endValue ?? range.end_value ?? 255),
              label: String(range.label || ''),
            },
          });
        }
      }

      imported += 1;
    } catch (err) {
      showToast(`Import failed for ${template.name}: ${err.message}`, 'error');
    }
  }

  await loadState({ silent: true });
  showToast(`Imported ${imported} templates`);
}

function installEventListeners() {
  els.templateForm.addEventListener('submit', onTemplateSubmit);
  els.fixtureForm.addEventListener('submit', onFixtureSubmit);
  els.groupForm.addEventListener('submit', onGroupSubmit);
  els.learnCreateForm.addEventListener('submit', onLearnCreateSubmit);

  els.addChannelBtn.addEventListener('click', () => {
    els.channelEditorList.appendChild(makeChannelRow());
  });
  els.generateChannelsBtn.addEventListener('click', generateBlankChannels);

  els.fixtureTemplateSelect.addEventListener('change', updatePatchRangePreview);
  els.fixtureStartAddress.addEventListener('input', updatePatchRangePreview);
  els.fixtureChannelCount.addEventListener('input', updatePatchRangePreview);

  els.refreshBtn.addEventListener('click', () => loadState());
  els.audioToggle.addEventListener('click', toggleReactiveMode);
  if (els.reactiveProfileSelect) {
    els.reactiveProfileSelect.addEventListener('change', () => {
      applyReactiveProfile({ notify: true }).catch((err) => showToast(err.message, 'error'));
    });
  }
  if (els.reactiveThreshold) {
    els.reactiveThreshold.addEventListener('input', () => {
      const liveValue = Math.max(0, Math.min(1, Number(els.reactiveThreshold.value || 0)));
      if (els.reactiveThresholdValue) {
        els.reactiveThresholdValue.textContent = liveValue.toFixed(2);
      }
      queueReactiveThresholdApply();
    });
    els.reactiveThreshold.addEventListener('change', () => {
      window.clearTimeout(state.reactiveThresholdTimer);
      applyReactiveThreshold({ notify: true }).catch((err) => showToast(err.message, 'error'));
    });
  }
  els.applyAudioInputBtn.addEventListener('click', applyAudioInputDevice);
  els.applyUniverseBtn.addEventListener('click', applyOutputUniverse);
  els.createUniverseBtn.addEventListener('click', createUniverse);
  if (els.panicBlackoutBtn) {
    els.panicBlackoutBtn.addEventListener('click', () => {
      panicBlackout().catch((err) => showToast(err.message, 'error'));
    });
  }
  els.audioReactiveMidiLearn.addEventListener('click', () => {
    startMidiLearn(MIDI_REACTIVE_CONTROL_ID, 'Music Reactive');
  });
  els.audioReactiveMidiClear.addEventListener('click', () => {
    clearMidiMapping(MIDI_REACTIVE_CONTROL_ID);
  });

  els.midiInputSelect.addEventListener('change', () => {
    state.midi.inputMode = els.midiInputSelect.value || 'all';
    saveMidiPreferences();
    refreshMidiStatusText();
  });

  els.newUniverseNumber.addEventListener('keydown', (event) => {
    if (event.key === 'Enter') {
      event.preventDefault();
      createUniverse().catch((err) => showToast(err.message, 'error'));
    }
  });

  els.viewTabs.forEach((tab) => {
    tab.addEventListener('click', () => {
      setActiveView(tab.dataset.viewTab || 'live');
    });
  });

  els.controlModeButtons.forEach((button) => {
    button.addEventListener('click', () => {
      setControlMode(button.dataset.controlMode || 'slider');
    });
  });

  els.templateExportBtn.addEventListener('click', exportTemplates);
  els.templateImportBtn.addEventListener('click', () => els.templateImportFile.click());
  els.templateImportFile.addEventListener('change', () => {
    importTemplatesFromFile(els.templateImportFile.files?.[0]).catch((err) => showToast(err.message, 'error'));
    els.templateImportFile.value = '';
  });

  els.learnFixtureSelect.addEventListener('change', () => {
    const fixture = selectedLearnFixture();
    rebuildLearnChannelSelect(fixture);
  });

  els.learnChannelSelect.addEventListener('change', applyLearnChannelInfoToForm);

  els.learnValueSlider.addEventListener('input', () => {
    queueLearnChannelValueSend(els.learnValueSlider.value);
  });

  els.learnValueInput.addEventListener('input', () => {
    queueLearnChannelValueSend(els.learnValueInput.value);
  });

  els.learnCaptureRangeStart.addEventListener('click', () => {
    els.learnRangeStart.value = String(Math.round(Number(els.learnValueInput.value || 0)));
  });

  els.learnCaptureRangeEnd.addEventListener('click', () => {
    els.learnRangeEnd.value = String(Math.round(Number(els.learnValueInput.value || 0)));
  });

  els.learnSaveChannelMeta.addEventListener('click', () => {
    saveLearnChannelMeta().catch((err) => showToast(err.message, 'error'));
  });

  els.learnClearRanges.addEventListener('click', () => {
    clearLearnChannelRanges().catch((err) => showToast(err.message, 'error'));
  });

  els.learnAddRange.addEventListener('click', () => {
    addLearnChannelRange().catch((err) => showToast(err.message, 'error'));
  });

  els.learnRangeLabel.addEventListener('keydown', (event) => {
    if (event.key === 'Enter') {
      event.preventDefault();
      addLearnChannelRange().catch((err) => showToast(err.message, 'error'));
    }
  });

  els.learnChannelKind.addEventListener('change', () => {
    highlightLearnKind(els.learnChannelKind.value);
  });

  els.learnKindButtons.forEach((button) => {
    button.addEventListener('click', () => {
      const kind = button.dataset.kind || 'generic';
      els.learnChannelKind.value = kind;
      highlightLearnKind(kind);
    });
  });

  const learnEditorRoot = els.learnFixtureSelect?.closest('.stack-form');
  const autoPauseRoots = [
    els.templateForm,
    els.fixtureForm,
    els.groupForm,
    els.learnCreateForm,
    learnEditorRoot,
  ];
  autoPauseRoots.forEach((root) => installAutoRefreshPauseGuards(root));

  window.addEventListener('resize', updatePatchRangePreview);
}

async function boot() {
  initializeUiPreferences();
  installEventListeners();
  initializePerformanceControls();

  registerMidiTarget(MIDI_REACTIVE_CONTROL_ID, {
    label: 'Music Reactive',
    kind: 'toggle',
    onMidiValue: async (enabled) => {
      await setReactiveModeFromMidi(Boolean(enabled));
    },
    labelEl: els.audioReactiveMidiLabel,
    clearBtn: els.audioReactiveMidiClear,
  });
  updateMidiTargetUi(MIDI_REACTIVE_CONTROL_ID);

  await initializeMidi();

  els.channelEditorList.appendChild(makeChannelRow({
    channelIndex: 1,
    name: 'Master Dimmer',
    kind: 'dimmer',
    defaultValue: 255,
    ranges: [{ startValue: 0, endValue: 255, label: 'Master dimmer' }],
  }));

  await loadState();

  state.refreshTimer = window.setInterval(() => {
    if (!shouldAutoRefreshNow()) return;
    loadState({ silent: true });
  }, 2500);
}

boot().catch((err) => {
  showToast(err.message, 'error');
});
