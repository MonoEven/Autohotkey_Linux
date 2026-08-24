'use strict';

const path = require('path');

function stripAnsi(value) {
  return String(value || '').replace(/\x1b\[[0-9;]*m/g, '');
}

function expandVariables(value, context = {}) {
  if (typeof value !== 'string') return value;
  const replacements = {
    '${workspaceFolder}': context.workspaceFolder || '',
    '${file}': context.file || '',
    '${fileDirname}': context.file ? path.dirname(context.file) : '',
    '${fileBasename}': context.file ? path.basename(context.file) : '',
    '${fileBasenameNoExtension}': context.file
      ? path.basename(context.file, path.extname(context.file))
      : '',
  };
  return Object.entries(replacements).reduce(
    (result, [token, replacement]) => result.split(token).join(replacement),
    value,
  );
}

function buildSpawnSpec(configuration, scriptPath, context = {}) {
  const runtime = expandVariables(configuration.runtime || 'ahk_core', context);
  const runtimeArgs = Array.isArray(configuration.runtimeArgs)
    ? configuration.runtimeArgs.map((arg) => expandVariables(String(arg), context))
    : [];
  const backend = configuration.inputBackend || 'auto';
  const env = { ...(context.env || process.env) };
  if (backend && backend !== 'auto') env.AHK_INPUT_BACKEND = backend;
  else delete env.AHK_INPUT_BACKEND;
  if (configuration.inputdSocket) {
    env.AHK_INPUTD_SOCKET = expandVariables(configuration.inputdSocket, context);
  }
  const cwdValue = expandVariables(
    configuration.workingDirectory || '${workspaceFolder}',
    context,
  );
  const cwd = cwdValue || context.workspaceFolder || path.dirname(scriptPath);
  return {
    command: runtime,
    args: [...runtimeArgs, scriptPath],
    cwd,
    env,
  };
}

function normalizeDiagKey(key) {
  return key
    .trim()
    .toLowerCase()
    .replace(/\s+/g, '-')
    .replace(/[^a-z0-9_.-]/g, '');
}

function parseDiag(text) {
  const entries = [];
  for (const rawLine of stripAnsi(text).split(/\r?\n/)) {
    const line = rawLine.trim();
    if (!line || /^[-=]{3,}$/.test(line)) continue;
    const match = line.match(/^([^:=]{2,64})\s*[:=]\s*(.*)$/);
    if (!match) continue;
    const key = normalizeDiagKey(match[1]);
    if (!key) continue;
    entries.push({ key, label: match[1].trim(), value: match[2].trim() });
  }
  return entries;
}

function parseAhkDiagnostics(text, defaultFile = '') {
  const clean = stripAnsi(text);
  const diagnostics = [];
  const seen = new Set();
  const push = (file, line, column, severity, message) => {
    const entry = {
      file: file || defaultFile,
      line: Math.max(0, Number(line || 1) - 1),
      column: Math.max(0, Number(column || 1) - 1),
      severity,
      message: String(message || '').trim(),
    };
    const id = JSON.stringify(entry);
    if (entry.message && !seen.has(id)) {
      seen.add(id);
      diagnostics.push(entry);
    }
  };

  for (const lineText of clean.split(/\r?\n/)) {
    let match = lineText.match(/^(.+?\.(?:ahk|ah2))\s*\((\d+)(?:,\s*(\d+))?\)\s*:\s*(?:(warning|error)\s*:)?\s*(.+)$/i);
    if (match) {
      push(match[1], match[2], match[3], /warning/i.test(match[4] || '') ? 'warning' : 'error', match[5]);
      continue;
    }
    match = lineText.match(/^(.+?\.(?:ahk|ah2)):(\d+)(?::(\d+))?\s*:\s*(warning|error)\s*:\s*(.+)$/i);
    if (match) {
      push(match[1], match[2], match[3], match[4].toLowerCase(), match[5]);
    }
  }

  const errorMatch = clean.match(/(?:^|\n)Error:\s*(.+)/i);
  const markerMatch = clean.match(/(?:^|\n)\s*(?:▶|--->?)?\s*(\d+)\s*:\s*(.+)/);
  if (errorMatch && markerMatch) {
    push(defaultFile, markerMatch[1], 1, 'error', errorMatch[1]);
  }
  return diagnostics;
}

module.exports = {
  buildSpawnSpec,
  expandVariables,
  normalizeDiagKey,
  parseAhkDiagnostics,
  parseDiag,
  stripAnsi,
};
