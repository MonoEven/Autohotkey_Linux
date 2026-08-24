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

async function activate(context) {
  const output = vscode.window.createOutputChannel('AutoHotkey Linux');
  const diagnostics = vscode.languages.createDiagnosticCollection('ahk-linux');
  const runtime = new RuntimeManager(output, diagnostics);
  const statusBar = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Left, 50);
  statusBar.command = 'ahkLinux.refreshCapabilities';
  const capabilities = new CapabilityProvider(runtime, statusBar);
  const taskProvider = new AhkTaskProvider(runtime);

  context.subscriptions.push(
    output,
    diagnostics,
    statusBar,
    capabilities.emitter,
    vscode.window.registerTreeDataProvider('ahkLinux.capabilities', capabilities),
    vscode.tasks.registerTaskProvider('ahk-linux', taskProvider),
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
        'ahkLinux.showDiagnostics',
        'ahkLinux.refreshCapabilities',
      ].filter((command) => allCommands.includes(command)),
      languageRegistered: languages.includes('ahk2'),
      diagnosticsEntries: parseDiag(diagResult.text).length,
      runExitCode: runResult.code,
      runOutput: runResult.output,
    };
    fs.writeFileSync(selfTestMarker, `${JSON.stringify(evidence)}\n`, 'utf8');
  } else if (uri) {
    capabilities.refresh(uri);
  }
}

function deactivate() {}

module.exports = { activate, deactivate };
