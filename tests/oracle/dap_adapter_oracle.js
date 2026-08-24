#!/usr/bin/env node
'use strict';

const fs = require('fs');
const path = require('path');
const { AhkDebugAdapterCore } = require('../../extensions/vscode-ahk-linux/lib/debugAdapterCore');

function parseArgs(argv) {
  if (argv.length !== 3) throw new Error('usage: dap_adapter_oracle.js RUNTIME FIXTURE SUMMARY');
  return argv.map((item) => path.resolve(item));
}

async function main() {
  const [runtime, fixture, summaryPath] = parseArgs(process.argv.slice(2));
  const marker = '/tmp/ahk-dbgp-fixture.out';
  try { fs.unlinkSync(marker); } catch (_) { /* absent */ }
  const messages = [];
  const waiters = [];
  const adapter = new AhkDebugAdapterCore((message) => {
    messages.push(message);
    for (const waiter of [...waiters]) {
      if (waiter.predicate(message)) {
        waiters.splice(waiters.indexOf(waiter), 1);
        clearTimeout(waiter.timer);
        waiter.resolve(message);
      }
    }
  });
  let sequence = 1;
  const send = (command, args = {}) => {
    const request = { seq: sequence++, type: 'request', command, arguments: args };
    adapter.handleMessage(request);
    return request;
  };
  const wait = (predicate, label, timeoutMs = 15000) => {
    const found = messages.find(predicate);
    if (found) return Promise.resolve(found);
    return new Promise((resolve, reject) => {
      const waiter = { predicate, resolve, reject };
      waiter.timer = setTimeout(() => {
        const index = waiters.indexOf(waiter);
        if (index >= 0) waiters.splice(index, 1);
        reject(new Error(`timeout waiting for ${label}\n${JSON.stringify(messages, null, 2)}`));
      }, timeoutMs);
      waiters.push(waiter);
    });
  };
  const response = (request) => wait(
    (message) => message.type === 'response' && message.request_seq === request.seq,
    `${request.command} response`,
  );
  const nextEvent = (event, from) => wait(
    (message) => message.type === 'event' && message.event === event && messages.indexOf(message) >= from,
    `${event} event`,
  );

  try {
    let request = send('initialize', { clientID: 'ahk-linux-oracle', adapterID: 'ahk-linux' });
    let reply = await response(request);
    if (!reply.success || !reply.body.supportsConfigurationDoneRequest) throw new Error('initialize failed');

    const launchStart = messages.length;
    request = send('launch', {
      program: fixture,
      runtime,
      cwd: path.dirname(fixture),
      args: ['dap-oracle'],
      backend: 'auto',
    });
    reply = await response(request);
    if (!reply.success) throw new Error(reply.message || 'launch failed');
    await nextEvent('initialized', launchStart);

    request = send('setBreakpoints', {
      source: { name: path.basename(fixture), path: fixture },
      breakpoints: [{ line: 3 }],
    });
    reply = await response(request);
    const breakpoint = reply.body.breakpoints[0];
    if (!breakpoint.verified || breakpoint.line !== 3) throw new Error(`bad breakpoint: ${JSON.stringify(reply)}`);

    const runStart = messages.length;
    request = send('configurationDone');
    await response(request);
    const stopped1 = await nextEvent('stopped', runStart);
    if (stopped1.body.reason !== 'breakpoint') throw new Error('wrong first stop reason');

    request = send('threads');
    reply = await response(request);
    if (reply.body.threads.length !== 1) throw new Error('thread mapping failed');

    request = send('stackTrace', { threadId: 1 });
    reply = await response(request);
    const frame1 = reply.body.stackFrames[0];
    if (frame1.line !== 3 || path.resolve(frame1.source.path) !== fixture) {
      throw new Error(`wrong first frame: ${JSON.stringify(frame1)}`);
    }

    request = send('scopes', { frameId: frame1.id });
    reply = await response(request);
    const globalScope = reply.body.scopes.find((scope) => scope.name === 'Global');
    if (!globalScope) throw new Error('global scope absent');

    request = send('variables', { variablesReference: globalScope.variablesReference });
    reply = await response(request);
    const x = reply.body.variables.find((variable) => variable.name === 'x');
    const arr = reply.body.variables.find((variable) => variable.name === 'arr');
    const obj = reply.body.variables.find((variable) => variable.name === 'obj');
    const mapv = reply.body.variables.find((variable) => variable.name === 'mapv');
    if (!x || x.value !== '10') throw new Error(`x variable mismatch: ${JSON.stringify(x)}`);
    if (!arr || !arr.variablesReference) throw new Error(`arr is not expandable: ${JSON.stringify(arr)}`);
    if (!obj || !obj.variablesReference) throw new Error(`obj is not expandable: ${JSON.stringify(obj)}`);
    if (!mapv || !mapv.variablesReference) throw new Error(`mapv is not expandable: ${JSON.stringify(mapv)}`);

    request = send('variables', { variablesReference: arr.variablesReference, start: 0, count: 16 });
    reply = await response(request);
    const arrayPage0 = reply.body.variables;
    request = send('variables', { variablesReference: arr.variablesReference, start: 16, count: 16 });
    reply = await response(request);
    const arrayPage1 = reply.body.variables;
    const arrayValues = [...arrayPage0, ...arrayPage1]
      .map((variable) => Number(variable.value))
      .filter(Number.isFinite);
    if (JSON.stringify(arrayValues) !== JSON.stringify(Array.from({ length: 20 }, (_, i) => i + 1))) {
      throw new Error(`array paging mismatch: ${JSON.stringify({ arrayPage0, arrayPage1 })}`);
    }

    request = send('variables', { variablesReference: obj.variablesReference, start: 0, count: 16 });
    reply = await response(request);
    const alpha = reply.body.variables.find((variable) => variable.name === 'alpha');
    const nested = reply.body.variables.find((variable) => variable.name === 'nested');
    if (!alpha || alpha.value !== 'A') throw new Error(`alpha mismatch: ${JSON.stringify(alpha)}`);
    if (!nested || !nested.variablesReference) throw new Error(`nested object missing: ${JSON.stringify(nested)}`);

    request = send('variables', { variablesReference: nested.variablesReference, start: 0, count: 16 });
    reply = await response(request);
    const beta = reply.body.variables.find((variable) => variable.name === 'beta');
    if (!beta || beta.value !== '42') throw new Error(`beta mismatch: ${JSON.stringify(beta)}`);

    request = send('variables', { variablesReference: mapv.variablesReference, start: 0, count: 16 });
    reply = await response(request);
    const mapValues = Object.fromEntries(
      reply.body.variables
        .filter((variable) => variable.name !== '<base>')
        .map((variable) => [variable.name, variable.value]),
    );
    if (mapValues['["first"]'] !== '101' || mapValues['["second"]'] !== '202') {
      throw new Error(`map mismatch: ${JSON.stringify(mapValues)}`);
    }

    request = send('evaluate', { expression: 'x', frameId: frame1.id, context: 'watch' });
    reply = await response(request);
    if (reply.body.result !== '10') throw new Error(`evaluate mismatch: ${JSON.stringify(reply)}`);
    const evaluatedX = reply.body.result;

    const stepStart = messages.length;
    request = send('stepIn', { threadId: 1 });
    await response(request);
    const stopped2 = await nextEvent('stopped', stepStart);
    if (stopped2.body.reason !== 'step') throw new Error('wrong step stop reason');

    request = send('stackTrace', { threadId: 1 });
    reply = await response(request);
    const frame2 = reply.body.stackFrames[0];
    if (frame2.line !== 4) throw new Error(`wrong stepped frame: ${JSON.stringify(frame2)}`);

    request = send('scopes', { frameId: frame2.id });
    reply = await response(request);
    const globalScope2 = reply.body.scopes.find((scope) => scope.name === 'Global');
    request = send('variables', { variablesReference: globalScope2.variablesReference });
    reply = await response(request);
    const y = reply.body.variables.find((variable) => variable.name === 'y');
    if (!y || y.value !== '15') throw new Error(`y variable mismatch: ${JSON.stringify(y)}`);

    const continueStart = messages.length;
    request = send('continue', { threadId: 1 });
    await response(request);
    await nextEvent('terminated', continueStart);

    const deadline = Date.now() + 3000;
    while (!fs.existsSync(marker) && Date.now() < deadline) {
      await new Promise((resolve) => setTimeout(resolve, 20));
    }
    const scriptResult = fs.readFileSync(marker, 'utf8').trim();
    if (scriptResult !== 'value=30') throw new Error(`script result mismatch: ${scriptResult}`);

    const summary = {
      schema: 1,
      result: 'pass',
      adapter: 'ahk-linux-inline-dap',
      initialized: true,
      breakpointLine: frame1.line,
      stepLine: frame2.line,
      globalX: Number(x.value),
      evaluatedX: Number(evaluatedX),
      arrayCount: arrayValues.length,
      arrayPages: 2,
      arrayEdges: [arrayValues[0], arrayValues.at(-1)],
      objectAlpha: alpha.value,
      nestedBeta: Number(beta.value),
      mapValues: { first: Number(mapValues['["first"]']), second: Number(mapValues['["second"]']) },
      globalY: Number(y.value),
      terminated: true,
      scriptResult,
      dapMessages: messages.length,
    };
    fs.mkdirSync(path.dirname(summaryPath), { recursive: true });
    fs.writeFileSync(summaryPath, `${JSON.stringify(summary)}\n`, 'utf8');
    process.stdout.write(`${JSON.stringify(summary)}\n`);
  } finally {
    adapter.dispose();
  }
}

main().catch((error) => {
  console.error(error.stack || error.message);
  process.exitCode = 1;
});
