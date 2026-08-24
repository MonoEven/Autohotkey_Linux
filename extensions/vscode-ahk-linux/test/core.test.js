'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');
const {
  buildSpawnSpec,
  expandVariables,
  parseAhkDiagnostics,
  parseDiag,
} = require('../lib/core');

const ROOT = path.resolve(__dirname, '..');

test('manifest, grammar and language configuration parse as JSON', () => {
  for (const relative of [
    'package.json',
    'language-configuration.json',
    'syntaxes/ahk2.tmLanguage.json',
  ]) {
    assert.doesNotThrow(() => JSON.parse(fs.readFileSync(path.join(ROOT, relative), 'utf8')));
  }
});

test('manifest contributes all command handlers implemented by extension', () => {
  const manifest = JSON.parse(fs.readFileSync(path.join(ROOT, 'package.json'), 'utf8'));
  const extension = fs.readFileSync(path.join(ROOT, 'extension.js'), 'utf8');
  for (const command of manifest.contributes.commands) {
    assert.match(extension, new RegExp(command.command.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')));
  }
  assert.equal(manifest.contributes.languages[0].id, 'ahk2');
  assert.equal(manifest.contributes.taskDefinitions[0].type, 'ahk-linux');
});

test('variable expansion covers workspace and file tokens', () => {
  const result = expandVariables('${workspaceFolder}/${fileBasenameNoExtension}:${fileDirname}', {
    workspaceFolder: '/work/repo',
    file: '/work/repo/scripts/demo.ahk',
  });
  assert.equal(result, '/work/repo/demo:/work/repo/scripts');
});

test('spawn spec preserves direct argv and backend environment', () => {
  const spec = buildSpawnSpec({
    runtime: '${workspaceFolder}/build core/ahk_core',
    runtimeArgs: ['--trace', '${fileBasename}'],
    workingDirectory: '${fileDirname}',
    inputBackend: 'evdev',
    inputdSocket: '/run/user/1000/ahk-inputd.sock',
  }, '/work/repo/test script.ahk', {
    workspaceFolder: '/work/repo',
    file: '/work/repo/test script.ahk',
    env: { PATH: '/usr/bin' },
  });
  assert.equal(spec.command, '/work/repo/build core/ahk_core');
  assert.deepEqual(spec.args, ['--trace', 'test script.ahk', '/work/repo/test script.ahk']);
  assert.equal(spec.cwd, '/work/repo');
  assert.equal(spec.env.AHK_INPUT_BACKEND, 'evdev');
  assert.equal(spec.env.AHK_INPUTD_SOCKET, '/run/user/1000/ahk-inputd.sock');
});

test('diagnostic parser handles backend and mux diagnostics', () => {
  const entries = parseDiag([
    'input-backend: x11',
    'input-mux    : x11+evdev',
    'caps-version=2',
    'input-event-version: 1',
  ].join('\n'));
  assert.deepEqual(entries.map(({ key, value }) => [key, value]), [
    ['input-backend', 'x11'],
    ['input-mux', 'x11+evdev'],
    ['caps-version', '2'],
    ['input-event-version', '1'],
  ]);
});

test('AHK error parser recognizes parenthesized and colon formats', () => {
  const errors = parseAhkDiagnostics([
    '/tmp/demo.ahk (12, 3) : Error: Unknown function.',
    '/tmp/other.ah2:7:2: warning: Deprecated option.',
  ].join('\n'));
  assert.equal(errors.length, 2);
  assert.deepEqual(errors[0], {
    file: '/tmp/demo.ahk', line: 11, column: 2, severity: 'error', message: 'Unknown function.',
  });
  assert.equal(errors[1].severity, 'warning');
  assert.equal(errors[1].line, 6);
});

test('AHK block error parser anchors Error message to arrow line', () => {
  const errors = parseAhkDiagnostics('Error: Invalid value.\nSpecifically: x\n\n▶ 004: BadCall()', '/tmp/block.ahk');
  assert.equal(errors.length, 1);
  assert.equal(errors[0].line, 3);
  assert.equal(errors[0].message, 'Invalid value.');
});
