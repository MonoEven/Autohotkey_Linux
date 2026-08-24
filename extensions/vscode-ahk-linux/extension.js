'use strict';

const vscode = require('vscode');
const cp = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');
const {
  buildSpawnSpec,
  expandVariables,
  parseAhkDiagnostics,
  parseDiag,
} = require('./lib/core');
const { AhkDebugAdapterCore } = require('./lib/debugAdapterCore');

class RuntimeManager {
  constructor(output, diagnostics) {
    this.output = output;
    this.diagnostics = diagnostics;
    this.children = new Set();
  }

  configuration(uri) {
    const cfg = vscode.workspace.getConfiguration('ahkLinux', uri);
    return {
      runtime: cfg.get('runtime', 'ahk_core'),
      runtimeArgs: cfg.get('runtimeArgs', []),
      workingDirectory: cfg.get('workingDirectory', '${workspaceFolder}'),
      inputBackend: cfg.get('inputBackend', 'auto'),
      inputdSocket: cfg.get('inputdSocket', ''),
      clearOutputBeforeRun: cfg.get('clearOutputBeforeRun', true),
      saveBeforeRun: cfg.get('saveBeforeRun', true),
    };
  }

  contextFor(uri) {
    const workspace = vscode.workspace.getWorkspaceFolder(uri);
    return {
      workspaceFolder: workspace ? workspace.uri.fsPath : path.dirname(uri.fsPath),
      file: uri.fsPath,
      env: process.env,
    };
  }

  async runScript(uri, options = {}) {
    const config = this.configuration(uri);
    if (config.clearOutputBeforeRun) this.output.clear();
    this.diagnostics.delete(uri);
    const context = this.contextFor(uri);
    const spec = buildSpawnSpec(config, uri.fsPath, context);
    if (options.cwd) spec.cwd = options.cwd;
    this.output.appendLine(`$ ${spec.command} ${spec.args.map(quoteForDisplay).join(' ')}`);
    this.output.appendLine(`[cwd] ${spec.cwd}`);
    this.output.show(true);
    return new Promise((resolve) => {
      let child;
      try {
        child = cp.spawn(spec.command, spec.args, {
          cwd: spec.cwd,
          env: spec.env,
          windowsHide: true,
        });
      } catch (error) {
        this.output.appendLine(`[spawn error] ${error.message}`);
        vscode.window.showErrorMessage(`AutoHotkey Linux: ${error.message}`);
        resolve({ code: -1, output: error.message });
        return;
      }
      this.children.add(child);
      let combined = '';
      const append = (chunk, prefix = '') => {
        const value = String(chunk);
        combined += value;
        for (const line of value.split(/\r?\n/)) {
          if (line) this.output.appendLine(`${prefix}${line}`);
        }
      };
      child.stdout.on('data', (chunk) => append(chunk));
      child.stderr.on('data', (chunk) => append(chunk, '[stderr] '));
      child.on('error', (error) => append(error.message, '[process error] '));
      child.on('close', (code) => {
        this.children.delete(child);
        this.output.appendLine(`[exit] ${code}`);
        this.publishDiagnostics(combined, uri);
        if (options.cleanup) options.cleanup();
        resolve({ code, output: combined });
      });
    });
  }

  publishDiagnostics(text, defaultUri) {
    const grouped = new Map();
    for (const item of parseAhkDiagnostics(text, defaultUri.fsPath)) {
      const uri = item.file ? vscode.Uri.file(path.resolve(item.file)) : defaultUri;
      const range = new vscode.Range(item.line, item.column, item.line, item.column + 1);
      const severity = item.severity === 'warning'
        ? vscode.DiagnosticSeverity.Warning
        : vscode.DiagnosticSeverity.Error;
      const diagnostic = new vscode.Diagnostic(range, item.message, severity);
      diagnostic.source = 'ahk-linux';
      const key = uri.toString();
      if (!grouped.has(key)) grouped.set(key, { uri, values: [] });
      grouped.get(key).values.push(diagnostic);
    }
    for (const { uri, values } of grouped.values()) this.diagnostics.set(uri, values);
  }

  stopAll() {
    for (const child of this.children) {
      try { child.kill('SIGTERM'); } catch (_) { /* already gone */ }
      setTimeout(() => {
        if (!child.killed) {
          try { child.kill('SIGKILL'); } catch (_) { /* already gone */ }
        }
      }, 1500).unref();
    }
    this.output.appendLine('[stop] termination requested');
  }

  async diagnosticsText(uri) {
    const config = this.configuration(uri);
    const context = this.contextFor(uri);
    const runtime = expandVariables(config.runtime || 'ahk_core', context);
    return new Promise((resolve) => {
      cp.execFile(runtime, ['--diag'], {
        cwd: context.workspaceFolder,
        env: buildSpawnSpec(config, uri.fsPath, context).env,
        timeout: 10000,
        maxBuffer: 4 * 1024 * 1024,
      }, (error, stdout, stderr) => {
        const text = `${stdout || ''}${stderr || ''}`;
        resolve({ error, text, runtime });
      });
    });
  }
}

class CapabilityProvider {
  constructor(runtime, statusBar) {
    this.runtime = runtime;
    this.statusBar = statusBar;
    this.entries = [];
    this.error = '';
    this.emitter = new vscode.EventEmitter();
    this.onDidChangeTreeData = this.emitter.event;
  }

  getTreeItem(element) { return element; }
  getChildren() {
    if (this.error) {
      const item = new vscode.TreeItem('Runtime unavailable');
      item.description = this.error;
      item.iconPath = new vscode.ThemeIcon('error');
      return [item];
    }
    if (!this.entries.length) {
      const item = new vscode.TreeItem('Refresh to inspect capabilities');
      item.iconPath = new vscode.ThemeIcon('info');
      return [item];
    }
    return this.entries.map((entry) => {
      const item = new vscode.TreeItem(entry.label);
      item.description = entry.value;
      item.tooltip = `${entry.label}: ${entry.value}`;
      item.iconPath = new vscode.ThemeIcon(iconForCapability(entry.key, entry.value));
      return item;
    });
  }

  async refresh(uri) {
    const result = await this.runtime.diagnosticsText(uri);
    this.entries = parseDiag(result.text);
    this.error = result.error && !result.text
      ? result.error.message
      : '';
    const backend = this.entries.find((entry) => entry.key === 'input-backend');
    const mux = this.entries.find((entry) => entry.key === 'input-mux');
    this.statusBar.text = backend
      ? `$(keyboard) AHK ${backend.value}${mux && mux.value !== '(none)' ? ` · ${mux.value}` : ''}`
      : '$(keyboard) AHK Linux';
    this.statusBar.tooltip = this.error || `Runtime: ${result.runtime}`;
    this.statusBar.show();
    this.emitter.fire(undefined);
    return result;
  }
}

class AhkTaskProvider {
  constructor(runtime) { this.runtime = runtime; }
  provideTasks() {
    const editor = vscode.window.activeTextEditor;
    if (!editor || editor.document.languageId !== 'ahk2') return [];
    return [this.taskFor(editor.document.uri, { type: 'ahk-linux', script: '${file}' })];
  }
  resolveTask(task) {
    const editor = vscode.window.activeTextEditor;
    if (!editor) return undefined;
    return this.taskFor(editor.document.uri, task.definition);
  }
  taskFor(uri, definition) {
    const config = this.runtime.configuration(uri);
    const context = this.runtime.contextFor(uri);
    if (definition.backend) config.inputBackend = definition.backend;
    const script = expandVariables(definition.script || '${file}', context);
    const spec = buildSpawnSpec(config, script, context);
    const execution = new vscode.ProcessExecution(spec.command, spec.args, {
      cwd: spec.cwd,
      env: spec.env,
    });
    const task = new vscode.Task(
      definition,
      vscode.TaskScope.Workspace,
      'Run AutoHotkey Linux script',
      'ahk-linux',
      execution,
      ['$ahk-linux'],
    );
    task.presentationOptions = {
      reveal: vscode.TaskRevealKind.Always,
      panel: vscode.TaskPanelKind.Dedicated,
      clear: true,
    };
    return task;
  }
}

class AhkDebugConfigurationProvider {
  constructor(runtime) { this.runtime = runtime; }

  resolveDebugConfiguration(folder, config) {
    if (config.request === 'attach') {
      const processId = Number(config.processId);
      const port = Number(config.port);
      if (!Number.isInteger(processId) || processId <= 0
        || !Number.isInteger(port) || port <= 0 || port > 65535) {
        vscode.window.showErrorMessage('Reconnect requires a valid processId and DBGp port.');
        return undefined;
      }
      return {
        ...config,
        type: 'ahk-linux',
        request: 'attach',
        name: config.name || `Reconnect AutoHotkey Linux (${processId})`,
        processId,
        host: config.host || '127.0.0.1',
        port,
      };
    }
    const editor = vscode.window.activeTextEditor;
    const fallbackUri = editor && editor.document.languageId === 'ahk2'
      ? editor.document.uri
      : folder && folder.uri;
    if (!fallbackUri) {
      vscode.window.showErrorMessage('Open an AutoHotkey v2 file or workspace before debugging.');
      return undefined;
    }
    const runtimeConfig = this.runtime.configuration(fallbackUri);
    const context = this.runtime.contextFor(fallbackUri);
    const program = config.program
      ? expandVariables(config.program, context)
      : editor && editor.document.languageId === 'ahk2'
        ? editor.document.uri.fsPath
        : '';
    if (!program) {
      vscode.window.showErrorMessage('Set "program" in the AutoHotkey Linux debug configuration.');
      return undefined;
    }
    return {
      ...config,
      type: 'ahk-linux',
      request: 'launch',
      name: config.name || 'Debug AutoHotkey Linux',
      program,
      runtime: expandVariables(config.runtime || runtimeConfig.runtime, { ...context, file: program }),
      runtimeArgs: config.runtimeArgs || runtimeConfig.runtimeArgs,
      cwd: expandVariables(config.cwd || runtimeConfig.workingDirectory, { ...context, file: program }),
      backend: config.backend || runtimeConfig.inputBackend,
      inputdSocket: config.inputdSocket || runtimeConfig.inputdSocket,
      args: config.args || [],
    };
  }
}

class InlineAhkDebugAdapter {
  constructor(onDetached) {
    this.emitter = new vscode.EventEmitter();
    this.onDidSendMessage = this.emitter.event;
    this.core = new AhkDebugAdapterCore(
      (message) => this.emitter.fire(message),
      { onDetached },
    );
  }
  handleMessage(message) { this.core.handleMessage(message); }
  dispose() {
    this.core.dispose();
    this.emitter.dispose();
  }
}

class AhkDebugAdapterFactory {
  constructor(onDetached) { this.onDetached = onDetached; }
  createDebugAdapterDescriptor() {
    return new vscode.DebugAdapterInlineImplementation(
      new InlineAhkDebugAdapter(this.onDetached),
    );
  }
}

function quoteForDisplay(value) {
  const text = String(value);
  return /\s/.test(text) ? JSON.stringify(text) : text;
}

function iconForCapability(key, value) {
  if (/error|unavailable|false|no$/i.test(value)) return 'warning';
  if (key.includes('backend') || key.includes('mux')) return 'server-environment';
  if (key.includes('version')) return 'versions';
  return 'symbol-property';
}

function activeAhkUri() {
  const editor = vscode.window.activeTextEditor;
  return editor && editor.document.languageId === 'ahk2' ? editor.document.uri : undefined;
}

async function runDebugSelfTest(context, script, runtimePath) {
  const uri = vscode.Uri.file(script);
  const breakpoint = new vscode.SourceBreakpoint(
    new vscode.Location(uri, new vscode.Position(2, 0)),
    true,
  );
  const evidence = {
    started: false,
    breakpointLine: 0,
    stepLine: 0,
    x: null,
    y: null,
    arrayValues: [],
    alpha: null,
    beta: null,
    mapValues: {},
    dbusProxy: {},
    typedScalar: {},
    exceptionLine: 0,
    exceptionMessage: null,
    idlePauseMs: null,
    idleFrame: null,
    idleValue: null,
    detached: false,
    reconnected: false,
    reconnectMs: null,
    reconnectIdleValue: null,
    terminated: false,
  };
  let debugSession;
  let phase = 0;
  let idlePauseStarted = 0;
  let reconnectStarted = 0;
  let chain = Promise.resolve();
  let resolveTerminated;
  const terminated = new Promise((resolve) => { resolveTerminated = resolve; });

  const inspect = async (session) => {
    const stack = await session.customRequest('stackTrace', { threadId: 1, startFrame: 0, levels: 20 });
    const frame = stack.stackFrames[0];
    const scopes = await session.customRequest('scopes', { frameId: frame.id });
    const globalScope = scopes.scopes.find((scope) => scope.name === 'Global');
    const variables = await session.customRequest('variables', {
      variablesReference: globalScope.variablesReference,
    });
    return { frame, variables: variables.variables };
  };

  const tracker = vscode.debug.registerDebugAdapterTrackerFactory('ahk-linux', {
    createDebugAdapterTracker(session) {
      debugSession = session;
      return {
        onDidSendMessage(message) {
          if (message.type !== 'event' || message.event !== 'stopped') return;
          chain = chain.then(async () => {
            const snapshot = await inspect(debugSession);
            if (phase === 0) {
              evidence.breakpointLine = snapshot.frame.line;
              evidence.x = snapshot.variables.find((item) => item.name === 'x')?.value ?? null;
              const arr = snapshot.variables.find((item) => item.name === 'arr');
              const obj = snapshot.variables.find((item) => item.name === 'obj');
              const mapv = snapshot.variables.find((item) => item.name === 'mapv');
              const comProxy = snapshot.variables.find((item) => item.name === 'comProxy');
              const typedScalar = snapshot.variables.find((item) => item.name === 'typedScalar');
              const arrPage0 = await debugSession.customRequest('variables', {
                variablesReference: arr.variablesReference, start: 0, count: 16,
              });
              const arrPage1 = await debugSession.customRequest('variables', {
                variablesReference: arr.variablesReference, start: 16, count: 16,
              });
              evidence.arrayValues = [...arrPage0.variables, ...arrPage1.variables]
                .map((item) => Number(item.value))
                .filter(Number.isFinite);
              const objChildren = await debugSession.customRequest('variables', {
                variablesReference: obj.variablesReference, start: 0, count: 16,
              });
              evidence.alpha = objChildren.variables.find((item) => item.name === 'alpha')?.value ?? null;
              const nested = objChildren.variables.find((item) => item.name === 'nested');
              const nestedChildren = await debugSession.customRequest('variables', {
                variablesReference: nested.variablesReference, start: 0, count: 16,
              });
              evidence.beta = nestedChildren.variables.find((item) => item.name === 'beta')?.value ?? null;
              const mapChildren = await debugSession.customRequest('variables', {
                variablesReference: mapv.variablesReference, start: 0, count: 16,
              });
              evidence.mapValues = Object.fromEntries(
                mapChildren.variables
                  .filter((item) => item.name !== '<base>')
                  .map((item) => [item.name, item.value]),
              );
              const proxyChildren = await debugSession.customRequest('variables', {
                variablesReference: comProxy.variablesReference, start: 0, count: 16,
              });
              evidence.dbusProxy = Object.fromEntries(
                proxyChildren.variables.map((item) => [item.name, item.value]),
              );
              const scalarChildren = await debugSession.customRequest('variables', {
                variablesReference: typedScalar.variablesReference, start: 0, count: 16,
              });
              evidence.typedScalar = Object.fromEntries(
                scalarChildren.variables.map((item) => [item.name, item.value]),
              );
              await debugSession.customRequest('setExceptionBreakpoints', { filters: ['all'] });
              phase = 1;
              await debugSession.customRequest('stepIn', { threadId: 1 });
            } else if (phase === 1) {
              evidence.stepLine = snapshot.frame.line;
              evidence.y = snapshot.variables.find((item) => item.name === 'y')?.value ?? null;
              phase = 2;
              await debugSession.customRequest('continue', { threadId: 1 });
            } else if (phase === 2) {
              evidence.exceptionLine = snapshot.frame.line;
              const evaluated = await debugSession.customRequest('evaluate', {
                expression: '<exception>.Message', frameId: snapshot.frame.id, context: 'watch',
              });
              evidence.exceptionMessage = evaluated.result;
              phase = 3;
              await debugSession.customRequest('continue', { threadId: 1 });
              const idleDeadline = Date.now() + 3000;
              while (!fs.existsSync('/tmp/ahk-dbgp-fixture.out') && Date.now() < idleDeadline) {
                await new Promise((resolve) => setTimeout(resolve, 20));
              }
              idlePauseStarted = Date.now();
              await debugSession.customRequest('pause', { threadId: 1 });
            } else if (phase === 3) {
              evidence.idlePauseMs = Date.now() - idlePauseStarted;
              evidence.idleFrame = snapshot.frame.name;
              evidence.idleValue = snapshot.variables.find((item) => item.name === 'idleValue')?.value ?? null;
              phase = 4;
              await vscode.commands.executeCommand('ahkLinux.detachDebugger');
            } else if (phase === 5) {
              evidence.reconnectMs = Date.now() - reconnectStarted;
              evidence.reconnected = true;
              evidence.reconnectIdleValue = snapshot.variables.find((item) => item.name === 'idleValue')?.value ?? null;
              phase = 6;
              await debugSession.customRequest('terminate');
            }
          });
        },
      };
    },
  });
  const terminateListener = vscode.debug.onDidTerminateDebugSession((session) => {
    if (session.type !== 'ahk-linux') return;
    if (phase === 4) {
      evidence.detached = true;
      phase = 5;
      reconnectStarted = Date.now();
      chain = chain.then(async () => {
        const started = await vscode.commands.executeCommand('ahkLinux.reconnectDebugger');
        if (!started) throw new Error('VS Code rejected the reconnect session');
      });
      return;
    }
    if (phase === 6) {
      evidence.terminated = true;
      resolveTerminated();
    }
  });

  vscode.debug.addBreakpoints([breakpoint]);
  try {
    evidence.started = await vscode.debug.startDebugging(
      vscode.workspace.getWorkspaceFolder(uri),
      {
        type: 'ahk-linux',
        request: 'launch',
        name: 'AHK Linux VSC-2 oracle',
        program: script,
        runtime: runtimePath,
        cwd: path.dirname(script),
        backend: 'auto',
      },
    );
    if (!evidence.started) throw new Error('VS Code rejected the debug session');
    await Promise.race([
      terminated,
      new Promise((_, reject) => setTimeout(() => reject(new Error('VS Code debug self-test timeout')), 30000)),
    ]);
    await chain;
    return evidence;
  } finally {
    vscode.debug.removeBreakpoints([breakpoint]);
    tracker.dispose();
    terminateListener.dispose();
    if (debugSession && !evidence.terminated) vscode.debug.stopDebugging(debugSession);
  }
}

async function activate(context) {
  const output = vscode.window.createOutputChannel('AutoHotkey Linux');
  const diagnostics = vscode.languages.createDiagnosticCollection('ahk-linux');
  const runtime = new RuntimeManager(output, diagnostics);
  const statusBar = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Left, 50);
  statusBar.command = 'ahkLinux.refreshCapabilities';
  const capabilities = new CapabilityProvider(runtime, statusBar);
  const taskProvider = new AhkTaskProvider(runtime);
  const debugConfigurationProvider = new AhkDebugConfigurationProvider(runtime);
  let lastDetachedDebuggee = null;
  const debugAdapterFactory = new AhkDebugAdapterFactory((info) => {
    lastDetachedDebuggee = info;
  });

  context.subscriptions.push(
    output,
    diagnostics,
    statusBar,
    capabilities.emitter,
    vscode.window.registerTreeDataProvider('ahkLinux.capabilities', capabilities),
    vscode.tasks.registerTaskProvider('ahk-linux', taskProvider),
    vscode.debug.registerDebugConfigurationProvider('ahk-linux', debugConfigurationProvider),
    vscode.debug.registerDebugAdapterDescriptorFactory('ahk-linux', debugAdapterFactory),
    vscode.commands.registerCommand('ahkLinux.runFile', async () => {
      const editor = vscode.window.activeTextEditor;
      if (!editor || editor.document.languageId !== 'ahk2') {
        vscode.window.showWarningMessage('Open an AutoHotkey v2 file first.');
        return;
      }
      const config = runtime.configuration(editor.document.uri);
      if (config.saveBeforeRun && editor.document.isDirty) await editor.document.save();
      await runtime.runScript(editor.document.uri);
    }),
    vscode.commands.registerCommand('ahkLinux.runSelection', async () => {
      const editor = vscode.window.activeTextEditor;
      if (!editor || editor.selection.isEmpty) {
        vscode.window.showWarningMessage('Select AutoHotkey code to run.');
        return;
      }
      let text = editor.document.getText(editor.selection);
      if (!/^\s*#Requires\b/im.test(text)) text = `#Requires AutoHotkey v2.0\n${text}\n`;
      const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'ahk-linux-vscode-'));
      const file = path.join(dir, 'selection.ahk');
      fs.writeFileSync(file, text, 'utf8');
      await runtime.runScript(vscode.Uri.file(file), {
        cwd: path.dirname(editor.document.uri.fsPath),
        cleanup: () => fs.rmSync(dir, { recursive: true, force: true }),
      });
    }),
    vscode.commands.registerCommand('ahkLinux.stop', () => runtime.stopAll()),
    vscode.commands.registerCommand('ahkLinux.detachDebugger', async () => {
      const session = vscode.debug.activeDebugSession;
      if (!session || session.type !== 'ahk-linux') {
        vscode.window.showWarningMessage('No active AutoHotkey Linux debug session.');
        return;
      }
      await session.customRequest('disconnect', { terminateDebuggee: false });
    }),
    vscode.commands.registerCommand('ahkLinux.reconnectDebugger', async () => {
      if (!lastDetachedDebuggee) {
        vscode.window.showWarningMessage('No detached AutoHotkey Linux process is available.');
        return false;
      }
      return vscode.debug.startDebugging(undefined, {
        type: 'ahk-linux',
        request: 'attach',
        name: `Reconnect AutoHotkey Linux (${lastDetachedDebuggee.processId})`,
        ...lastDetachedDebuggee,
      });
    }),
    vscode.commands.registerCommand('ahkLinux.showDiagnostics', async () => {
      const uri = activeAhkUri()
        || (vscode.workspace.workspaceFolders && vscode.workspace.workspaceFolders[0].uri);
      if (!uri) return;
      const result = await capabilities.refresh(uri);
      output.clear();
      output.appendLine(`$ ${result.runtime} --diag`);
      output.append(result.text || result.error?.message || 'No diagnostic output.');
      output.show(true);
    }),
    vscode.commands.registerCommand('ahkLinux.refreshCapabilities', async () => {
      const uri = activeAhkUri()
        || (vscode.workspace.workspaceFolders && vscode.workspace.workspaceFolders[0].uri);
      if (uri) await capabilities.refresh(uri);
    }),
    vscode.commands.registerCommand('ahkLinux.openLinuxDocs', async () => {
      const folders = vscode.workspace.workspaceFolders || [];
      const local = folders
        .map((folder) => path.join(folder.uri.fsPath, 'docs-v2', 'docs', 'linux-port.htm'))
        .find((candidate) => fs.existsSync(candidate));
      const uri = local
        ? vscode.Uri.file(local)
        : vscode.Uri.parse('https://github.com/MonoEven/Autohotkey_Linux');
      await vscode.env.openExternal(uri);
    }),
  );

  const uri = activeAhkUri()
    || (vscode.workspace.workspaceFolders && vscode.workspace.workspaceFolders[0].uri);
  const selfTestMarker = process.env.AHK_LINUX_VSCODE_SELFTEST;
  if (selfTestMarker) {
    const script = process.env.AHK_LINUX_VSCODE_SELFTEST_SCRIPT;
    const testUri = script ? vscode.Uri.file(script) : uri;
    const diagResult = testUri ? await capabilities.refresh(testUri) : { text: '', error: new Error('no URI') };
    const runResult = script
      ? await runtime.runScript(vscode.Uri.file(script), { cwd: path.dirname(script) })
      : { code: null, output: '' };
    const debugScript = process.env.AHK_LINUX_VSCODE_DEBUG_SCRIPT;
    const debugEvidence = debugScript
      ? await runDebugSelfTest(
          context,
          debugScript,
          runtime.configuration(vscode.Uri.file(debugScript)).runtime,
        )
      : null;
    const allCommands = await vscode.commands.getCommands(true);
    const languages = await vscode.languages.getLanguages();
    const evidence = {
      schema: 1,
      activated: true,
      extensionVersion: context.extension.packageJSON.version,
      commands: [
        'ahkLinux.runFile',
        'ahkLinux.runSelection',
        'ahkLinux.stop',
        'ahkLinux.detachDebugger',
        'ahkLinux.reconnectDebugger',
        'ahkLinux.showDiagnostics',
        'ahkLinux.refreshCapabilities',
      ].filter((command) => allCommands.includes(command)),
      languageRegistered: languages.includes('ahk2'),
      diagnosticsEntries: parseDiag(diagResult.text).length,
      runExitCode: runResult.code,
      runOutput: runResult.output,
      debug: debugEvidence,
    };
    fs.writeFileSync(selfTestMarker, `${JSON.stringify(evidence)}\n`, 'utf8');
  } else if (uri) {
    capabilities.refresh(uri);
  }
}

function deactivate() {}

module.exports = { activate, deactivate };
