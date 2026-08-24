'use strict';

const cp = require('child_process');
const path = require('path');
const { fileURLToPath, pathToFileURL } = require('url');
const {
  DbgpSession,
  findElements,
  findTopLevelProperties,
} = require('./dbgp');

const VARIABLE_PAGE_SIZE = 16;

class AhkDebugAdapterCore {
  constructor(sendMessage, options = {}) {
    this.sendMessage = sendMessage;
    this.onDetached = options.onDetached || (() => {});
    this.sequence = 1;
    this.session = null;
    this.child = null;
    this.processId = 0;
    this.debugHost = '127.0.0.1';
    this.debugPort = 0;
    this.attachMode = false;
    this.init = null;
    this.frames = new Map();
    this.breakpoints = new Map();
    this.variableHandles = new Map();
    this.nextVariableHandle = 1;
    this.exceptionBreakpointId = 0;
    this.pendingPause = false;
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
      supportsVariablePaging: true,
      supportsRestartRequest: false,
      supportTerminateDebuggee: true,
      supportsExceptionInfoRequest: false,
      exceptionBreakpointFilters: [
        { filter: 'all', label: 'All caught and uncaught exceptions', default: false },
      ],
    });
  }

  async request_launch(request) {
    const config = request.arguments || {};
    if (!config.program) throw new Error('A debug program path is required');
    if (!config.runtime) throw new Error('An ahk_core runtime path is required');
    this.session = new DbgpSession();
    const port = await this.session.listen(this.debugHost);
    this.debugPort = port;
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
    this.processId = this.child.pid;
    this.child.stdout.on('data', (chunk) => this.output(chunk, 'stdout'));
    this.child.stderr.on('data', (chunk) => this.output(chunk, 'stderr'));
    this.child.on('error', (error) => this.output(`${error.message}\n`, 'stderr'));
    this.child.on('exit', (code, signal) => {
      this.event('exited', { exitCode: Number.isInteger(code) ? code : -1 });
      this.sendTerminated({ code, signal });
    });
    this.init = await this.session.waitForInit();
    await this.configureSession();
    this.respond(request, {
      processId: this.processId,
      reconnect: { host: this.debugHost, port: this.debugPort },
    });
    this.event('initialized');
  }

  async request_attach(request) {
    const config = request.arguments || {};
    this.processId = Number(config.processId);
    this.debugHost = String(config.host || '127.0.0.1');
    this.debugPort = Number(config.port);
    if (!Number.isInteger(this.processId) || this.processId <= 0) throw new Error('A valid processId is required');
    if (!Number.isInteger(this.debugPort) || this.debugPort <= 0 || this.debugPort > 65535) {
      throw new Error('A valid reconnect port is required');
    }
    this.attachMode = true;
    this.session = new DbgpSession();
    await this.session.listen(this.debugHost, this.debugPort);
    try {
      process.kill(this.processId, 'SIGUSR2');
    } catch (error) {
      this.session.close();
      throw new Error(`Cannot signal process ${this.processId}: ${error.message}`);
    }
    this.init = await this.session.waitForInit();
    await this.configureSession();
    this.respond(request, { processId: this.processId });
    this.event('initialized');
  }

  async configureSession() {
    await this.session.send(`feature_set -n max_children -v ${VARIABLE_PAGE_SIZE}`);
    await this.session.send('feature_set -n max_depth -v 1');
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
    if (this.attachMode) {
      this.variableHandles.clear();
      this.nextVariableHandle = 1;
      this.event('stopped', {
        reason: 'pause',
        description: 'Reconnected to existing AutoHotkey process',
        threadId: 1,
        allThreadsStopped: true,
      });
    } else {
      this.startContinuation('run', 'breakpoint');
    }
  }

  async request_setExceptionBreakpoints(request) {
    this.requireSession();
    const enable = (request.arguments?.filters || []).includes('all');
    if (this.exceptionBreakpointId) {
      try { await this.session.send(`breakpoint_remove -d ${this.exceptionBreakpointId}`); } catch (_) { /* stale */ }
      this.exceptionBreakpointId = 0;
    }
    if (enable) {
      const packet = await this.session.send('breakpoint_set -t exception -x Any -s enabled');
      this.exceptionBreakpointId = Number(packet.attributes.id || 0);
    }
    this.respond(request, { breakpoints: [{ verified: !enable || this.exceptionBreakpointId > 0 }] });
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
    if (!frames.length && this.init) {
      // A persistent script which finished auto-execute has no active DBGp
      // stack frame.  DAP needs a frame to expose scopes, so provide an honest
      // synthetic idle frame: depth 0 permits Global context/property queries,
      // but the label explicitly says there is no active script frame.
      let sourcePath = '';
      try { sourcePath = fileURLToPath(this.init.attributes.fileuri); } catch (_) { sourcePath = this.init.attributes.fileuri || ''; }
      this.frames.set(1, 0);
      frames.push({
        id: 1,
        name: 'Idle (no active script frame)',
        line: 1,
        column: 1,
        presentationHint: 'subtle',
        source: { name: path.basename(sourcePath), path: sourcePath },
      });
    }
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
    const reference = Number(request.arguments?.variablesReference);
    const scope = decodeScope(reference);
    let variables;
    if (scope) {
      const packet = await this.session.send(`context_get -c ${scope.context} -d ${scope.depth}`);
      const properties = findTopLevelProperties(packet.xml);
      const start = Math.max(0, Number(request.arguments?.start || 0));
      const count = Number(request.arguments?.count || properties.length);
      variables = properties.slice(start, start + count)
        .map((property) => this.toDapVariable(property, scope.context, scope.depth));
    } else {
      const handle = this.variableHandles.get(reference);
      if (!handle) throw new Error('Unknown variable reference');
      variables = await this.objectVariables(handle, request.arguments || {});
    }
    this.respond(request, { variables });
  }

  async objectVariables(handle, args) {
    const total = Math.max(0, Number(handle.numchildren || 0));
    const start = Math.max(0, Number(args.start || 0));
    const requested = Number(args.count || VARIABLE_PAGE_SIZE);
    const end = Math.min(total || start + requested, start + requested);
    const result = [];
    let position = start;
    while (position < end) {
      const page = Math.floor(position / VARIABLE_PAGE_SIZE);
      const packet = await this.session.send(
        `property_get -n ${handle.fullname} -c ${handle.context} -d ${handle.depth} -p ${page}`,
      );
      const root = findTopLevelProperties(packet.xml)[0];
      if (!root) break;
      const offset = position - page * VARIABLE_PAGE_SIZE;
      const available = root.childProperties.slice(offset);
      if (!available.length) break;
      const take = Math.min(available.length, end - position);
      result.push(...available.slice(0, take)
        .map((property) => this.toDapVariable(property, handle.context, handle.depth)));
      position += take;
    }
    return result;
  }

  toDapVariable(property, context, depth) {
    const numchildren = Math.max(0, Number(property.numchildren || 0));
    let variablesReference = 0;
    if (property.children === '1' || numchildren > 0) {
      variablesReference = this.nextVariableHandle++;
      this.variableHandles.set(variablesReference, {
        fullname: property.fullname,
        context,
        depth,
        numchildren,
      });
    }
    const objectLabel = property.classname
      ? `${property.classname}${numchildren ? ` (${numchildren})` : ''}`
      : property.value;
    return {
      name: property.name || property.fullname || '?',
      value: variablesReference ? objectLabel : property.value,
      type: property.classname || property.type || '',
      variablesReference,
      ...(variablesReference ? { namedVariables: numchildren } : {}),
    };
  }

  async request_evaluate(request) {
    this.requireSession();
    const expression = String(request.arguments?.expression || '');
    const frameId = Number(request.arguments?.frameId || 1);
    const depth = this.frames.get(frameId) || 0;
    const packet = await this.session.send(`property_get -n ${expression} -c 0 -d ${depth}`);
    const property = findTopLevelProperties(packet.xml)[0];
    const variable = property ? this.toDapVariable(property, 0, depth) : null;
    this.respond(request, {
      result: variable ? variable.value : 'undefined',
      type: variable ? variable.type : 'undefined',
      variablesReference: variable ? variable.variablesReference : 0,
      ...(variable && variable.namedVariables ? { namedVariables: variable.namedVariables } : {}),
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
    this.requireSession();
    this.pendingPause = true;
    this.respond(request);
    // DBGp emits the outstanding run continuation response first when break
    // enters the stopped state, followed by the break command response.  The
    // continuation handler consumes pendingPause to emit exactly one DAP stop.
    this.session.send('break').catch((error) => {
      this.pendingPause = false;
      this.output(`${error.message}\n`, 'stderr');
    });
  }

  request_disconnect(request) {
    this.respond(request);
    const terminate = request.arguments?.terminateDebuggee !== false;
    if (!this.session) {
      this.dispose(!terminate);
      return;
    }
    if (!terminate) {
      const info = {
        processId: this.processId,
        host: this.debugHost,
        port: this.debugPort,
        fileuri: this.init?.attributes?.fileuri || '',
      };
      this.session.send('detach').then(() => {
        this.onDetached(info);
        this.sendTerminated({ detached: true, processId: this.processId });
      }).catch((error) => this.output(`${error.message}\n`, 'stderr'))
        .finally(() => this.dispose(true));
      return;
    }
    this.session.send('stop').then(() => this.sendTerminated({ processId: this.processId }))
      .catch(() => {}).finally(() => this.dispose(false));
  }

  request_terminate(request) {
    this.respond(request);
    if (this.session) {
      this.session.send('stop').then(() => this.sendTerminated({ processId: this.processId }))
        .catch(() => {}).finally(() => this.dispose(false));
    }
  }

  startContinuation(command, reason) {
    this.requireSession();
    this.session.send(command).then((packet) => {
      if (packet.attributes.status === 'break') {
        this.variableHandles.clear();
        this.nextVariableHandle = 1;
        const stoppedReason = ['exception', 'error'].includes(packet.attributes.reason)
          ? 'exception'
          : this.pendingPause ? 'pause' : reason;
        this.pendingPause = false;
        this.event('stopped', {
          reason: stoppedReason,
          ...(stoppedReason === 'exception' ? { text: packet.attributes.reason } : {}),
          threadId: 1,
          allThreadsStopped: true,
        });
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

  dispose(keepDebuggee = false) {
    if (this.session) this.session.close();
    this.session = null;
    if (!keepDebuggee && this.child && this.child.exitCode === null && !this.child.killed) {
      this.child.kill('SIGTERM');
    }
    if (keepDebuggee && this.child) this.child.unref();
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
