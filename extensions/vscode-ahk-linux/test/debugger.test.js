'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');
const {
  DbgpPacketParser,
  findElements,
  findTopLevelProperties,
  rootInfo,
} = require('../lib/dbgp');
const { AhkDebugAdapterCore, decodeScope, encodeScope } = require('../lib/debugAdapterCore');

const ROOT = path.resolve(__dirname, '..');

test('DBGp parser handles split and coalesced length-framed packets', () => {
  const packets = [];
  const parser = new DbgpPacketParser((packet) => packets.push(packet));
  const one = '<init protocol_version="1.0"/>';
  const two = '<response transaction_id="2" status="break"/>';
  const wire = Buffer.concat([
    Buffer.from(`${Buffer.byteLength(one)}\0${one}\0`),
    Buffer.from(`${Buffer.byteLength(two)}\0${two}\0`),
  ]);
  parser.push(wire.subarray(0, 7));
  parser.push(wire.subarray(7, 31));
  parser.push(wire.subarray(31));
  assert.deepEqual(packets, [one, two]);
  assert.equal(rootInfo(packets[1]).attributes.status, 'break');
});

test('DBGp XML helpers decode stack and top-level scalar properties', () => {
  const xml = '<response transaction_id="5"><stack level="0" lineno="3"/>'
    + '<property name="x" type="integer" encoding="base64">MTA=</property>'
    + '<property name="obj" type="object"><property name="nested" type="string" encoding="base64">eQ==</property></property>'
    + '</response>';
  assert.equal(findElements(xml, 'stack')[0].lineno, '3');
  const properties = findTopLevelProperties(xml);
  assert.deepEqual(properties.map((p) => [p.name, p.value]), [['x', '10'], ['obj', 'object']]);
  assert.deepEqual(properties[1].childProperties.map((p) => [p.name, p.value]), [['nested', 'y']]);
});

test('DAP scope references round-trip depth and context', () => {
  for (const depth of [0, 1, 42]) {
    for (const context of [0, 1]) {
      assert.deepEqual(decodeScope(encodeScope(depth, context)), { depth, context });
    }
  }
  assert.equal(decodeScope(9), null);
});

test('DAP initialize advertises real debugger capabilities', async () => {
  const messages = [];
  const adapter = new AhkDebugAdapterCore((message) => messages.push(message));
  adapter.handleMessage({ seq: 1, type: 'request', command: 'initialize', arguments: {} });
  await new Promise((resolve) => setImmediate(resolve));
  const response = messages.find((message) => message.type === 'response');
  assert.equal(response.success, true);
  assert.equal(response.body.supportsConfigurationDoneRequest, true);
  assert.equal(response.body.supportsTerminateRequest, true);
});

test('manifest contributes ahk-linux debugger and activation', () => {
  const manifest = JSON.parse(fs.readFileSync(path.join(ROOT, 'package.json'), 'utf8'));
  const debuggerContribution = manifest.contributes.debuggers.find((item) => item.type === 'ahk-linux');
  assert.ok(debuggerContribution);
  assert.deepEqual(debuggerContribution.languages, ['ahk2']);
  assert.ok(manifest.activationEvents.includes('onDebug:ahk-linux'));
  const extension = fs.readFileSync(path.join(ROOT, 'extension.js'), 'utf8');
  assert.match(extension, /registerDebugAdapterDescriptorFactory\('ahk-linux'/);
});
