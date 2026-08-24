'use strict';

const cp = require('child_process');
const path = require('path');
const { fileURLToPath, pathToFileURL } = require('url');
const {
  DbgpSession,
  findElements,
  findTopLevelProperties,
} = require('./dbgp');

class AhkDebugAdapterCore {
  constructor(sendMessage) {
    this.sendMessage = sendMessage;
    this.sequence = 1;
    this.session = null;
    this.child = null;
    this.init = null;
    this.frames = new Map();
    this.breakpoints = new Map();
    this.terminated = false;
  }

  handleMessage(message) {
    if (!message || message.type !== 'request') return;
    const handler = this[`request_${message.command}`];
    if (!handler) {
      this.respond(message, undefined, false, `Unsupported request: ${message.command}`);
      return;
    }
    Promise.resolve(handler.call(this, message)).catch((error) => {
      this.output(`Debug adapter error: ${error.stack || error.message}\n`, 'stderr');
      this.respond(message, undefined, false, error.message);
    });
  }

  respond(request, body, success = true, message) {
    this.sendMessage({
      seq: this.sequence++,
      type: 'response',
      request_seq: request.seq,
      success,
      command: request.command,
      ...(message ? { message } : {}),
      ...(body === undefined ? {} : { body }),
    });
  }

  event(event, body) {
    this.sendMessage({ seq: this.sequence++, type: 'event', event, ...(body ? { body } : {}) });
  }

  output(output, category = 'console') {
    if (output) this.event('output', { category, output: String(output) });
  }

  request_initialize(request) {
    this.respond(request, {
      supportsConfigurationDoneRequest: true,
      supportsTerminateRequest: true,
      supportsEvaluateForHovers: true,
      supportsSetVariable: false,
      supportsRestartRequest: false,
      supportsExceptionInfoRequest: false,
    });
  }

  async request_launch(request) {
    const config = request.arguments || {};
    if (!config.program) throw new Error('A debug program path is required');
    if (!config.runtime) throw new Error('An ahk_core runtime path is required');
    this.session = new DbgpSession();
    const port = await this.session.listen();
    const env = { ...process.env, ...(config.env || {}) };
    if (config.backend && config.backend !== 'auto') env.AHK_INPUT_BACKEND = config.backend;
    if (config.inputdSocket) env.AHK_INPUTD_SOCKET = config.inputdSocket;
    const runtimeArgs = Array.isArray(config.runtimeArgs) ? config.runtimeArgs.map(String) : [];
    const scriptArgs = Array.isArray(config.args) ? config.args.map(String) : [];
    const argv = [...runtimeArgs, '--debug', `127.0.0.1:${port}`, config.program, ...scriptArgs];
    this.output(`$ ${config.runtime} ${argv.map(displayArg).join(' ')}\n`);
    this.child = cp.spawn(config.runtime, argv, {
      cwd: config.cwd || path.dirname(config.program),
      env,
      windowsHide: true,
    });
    this.child.stdout.on('data', (chunk) => this.output(chunk, 'stdout'));
    this.child.stderr.on('data', (chunk) => this.output(chunk, 'stderr'));
    this.child.on('error', (error) => this.output(`${error.message}\n`, 'stderr'));
    this.child.on('exit', (code, signal) => {
      this.event('exited', { exitCode: Number.isInteger(code) ? code : -1 });
      this.sendTerminated({ code, signal });
    });
    this.init = await this.session.waitForInit();
    this.respond(request);
    this.event('initialized');
  }

  async request_setBreakpoints(request) {
    this.requireSession();
    const args = request.arguments || {};
    const sourcePath = args.source && args.source.path;
    if (!sourcePath) throw new Error('Breakpoint source path is required');
    const old = this.breakpoints.get(sourcePath) || [];
    for (const id of old) {
      try { await this.session.send(`breakpoint_remove -d ${id}`); } catch (_) { /* stale */ }
    }
    const ids = [];
    const results = [];
    const uri = pathToFileURL(sourcePath).href;
    for (const requested of args.breakpoints || []) {
      const line = Number(requested.line);
      try {
        const packet = await this.session.send(`breakpoint_set -t line -f ${uri} -n ${line} -s enabled`);
        const id = Number(packet.attributes.id);
        ids.push(id);
        results.push({ id, verified: packet.attributes.state === 'enabled', line, source: args.source });
      } catch (error) {
        results.push({ verified: false, line, source: args.source, message: error.message });
      }
    }
    this.breakpoints.set(sourcePath, ids);
    this.respond(request, { breakpoints: results });
  }

  request_configurationDone(request) {
    this.respond(request);
    this.startContinuation('run', 'breakpoint');
  }

  request_threads(request) {
    this.respond(request, { threads: [{ id: 1, name: 'AutoHotkey main thread' }] });
  }

  async request_stackTrace(request) {
    this.requireSession();
    const packet = await this.session.send('stack_get');
    const stack = findElements(packet.xml, 'stack');
    this.frames.clear();
    const frames = stack.map((item, index) => {
      const depth = Number(item.level || index);
      const id = depth + 1;
      this.frames.set(id, depth);
      let sourcePath;
      try { sourcePath = fileURLToPath(item.filename); } catch (_) { sourcePath = item.filename; }
      return {
        id,
        name: item.where || `Frame ${depth}`,
        line: Number(item.lineno || 1),
        column: 1,
        source: { name: path.basename(sourcePath), path: sourcePath },
      };
    });
    this.respond(request, { stackFrames: frames, totalFrames: frames.length });
  }

  request_scopes(request) {
    const depth = this.frames.get(Number(request.arguments?.frameId));
    if (depth === undefined) throw new Error('Unknown stack frame');
    this.respond(request, {
      scopes: [
        { name: 'Local', presentationHint: 'locals', variablesReference: encodeScope(depth, 0), expensive: false },
        { name: 'Global', presentationHint: 'globals', variablesReference: encodeScope(depth, 1), expensive: true },
      ],
    });
  }

  async request_variables(request) {
    this.requireSession();
    const scope = decodeScope(Number(request.arguments?.variablesReference));
    if (!scope) throw new Error('Unknown variable scope');
    const packet = await this.session.send(`context_get -c ${scope.context} -d ${scope.depth}`);
    const variables = findTopLevelProperties(packet.xml).map((property) => ({
      name: property.name || property.fullname || '?',
      value: property.value,
      type: property.type || '',
      variablesReference: 0,
    }));
    this.respond(request, { variables });
  }

  async request_evaluate(request) {
    this.requireSession();
    const expression = String(request.arguments?.expression || '');
    const frameId = Number(request.arguments?.frameId || 1);
    const depth = this.frames.get(frameId) || 0;
    const packet = await this.session.send(`property_get -n ${expression} -c 0 -d ${depth}`);
    const property = findTopLevelProperties(packet.xml)[0];
    this.respond(request, {
      result: property ? property.value : 'undefined',
      type: property ? property.type : 'undefined',
      variablesReference: 0,
    });
  }

  request_continue(request) {
    this.respond(request, { allThreadsContinued: true });
    this.startContinuation('run', 'breakpoint');
  }

  request_next(request) {
    this.respond(request);
    this.startContinuation('step_over', 'step');
  }

  request_stepIn(request) {
    this.respond(request);
    this.startContinuation('step_into', 'step');
  }

  request_stepOut(request) {
    this.respond(request);
    this.startContinuation('step_out', 'step');
  }

  request_pause(request) {
    this.respond(request);
    this.startContinuation('break', 'pause');
  }

  request_disconnect(request) {
    this.respond(request);
    const terminate = request.arguments?.terminateDebuggee !== false;
    if (this.session) {
      this.session.send(terminate ? 'stop' : 'detach').catch(() => {}).finally(() => this.dispose());
    } else {
      this.dispose();
    }
  }

  request_terminate(request) {
    this.respond(request);
    if (this.session) this.session.send('stop').catch(() => {}).finally(() => this.dispose());
  }

  startContinuation(command, reason) {
    this.requireSession();
    this.session.send(command).then((packet) => {
      if (packet.attributes.status === 'break') {
        this.event('stopped', { reason, threadId: 1, allThreadsStopped: true });
      } else if (packet.attributes.status === 'stopped') {
        this.sendTerminated({ reason: packet.attributes.reason });
      }
    }).catch((error) => {
      if (!this.terminated) this.output(`${error.message}\n`, 'stderr');
    });
  }

  requireSession() {
    if (!this.session || !this.init) throw new Error('Debug session is not initialized');
  }

  sendTerminated(body) {
    if (this.terminated) return;
    this.terminated = true;
    this.event('terminated', body || {});
  }

  dispose() {
    if (this.session) this.session.close();
    this.session = null;
    if (this.child && this.child.exitCode === null && !this.child.killed) this.child.kill('SIGTERM');
    this.child = null;
  }
}

function encodeScope(depth, context) {
  return 100000 + depth * 2 + context;
}

function decodeScope(reference) {
  if (!Number.isInteger(reference) || reference < 100000) return null;
  const value = reference - 100000;
  return { depth: Math.floor(value / 2), context: value % 2 };
}

function displayArg(value) {
  const text = String(value);
  return /\s/.test(text) ? JSON.stringify(text) : text;
}

module.exports = { AhkDebugAdapterCore, decodeScope, encodeScope };
