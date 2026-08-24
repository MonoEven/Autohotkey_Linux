'use strict';

const net = require('net');

function decodeXml(value) {
  return String(value || '')
    .replace(/&quot;/g, '"')
    .replace(/&apos;/g, "'")
    .replace(/&lt;/g, '<')
    .replace(/&gt;/g, '>')
    .replace(/&amp;/g, '&');
}

function parseAttributes(fragment) {
  const result = {};
  const re = /([A-Za-z_:][\w:.-]*)="([^"]*)"/g;
  let match;
  while ((match = re.exec(fragment))) result[match[1]] = decodeXml(match[2]);
  return result;
}

function rootInfo(xml) {
  const match = String(xml).match(/<([A-Za-z_:][\w:.-]*)\b([^>]*)>/);
  if (!match) throw new Error(`Invalid DBGp XML packet: ${xml}`);
  return { name: match[1].split(':').pop(), attributes: parseAttributes(match[2]) };
}

function findElements(xml, name) {
  const re = new RegExp(`<${name}\\b([^>]*)\\/>`, 'g');
  const values = [];
  let match;
  while ((match = re.exec(xml))) values.push(parseAttributes(match[1]));
  return values;
}

function findTopLevelProperties(xml) {
  const values = [];
  const token = /<property\b[^>]*>|<\/property\s*>/g;
  let depth = 0;
  let start = -1;
  let match;
  while ((match = token.exec(xml))) {
    if (match[0][1] !== '/') {
      if (depth === 0) start = match.index;
      depth += 1;
    } else {
      depth -= 1;
      if (depth === 0 && start >= 0) {
        const block = xml.slice(start, token.lastIndex);
        const open = block.match(/^<property\b([^>]*)>/);
        const attrs = parseAttributes(open ? open[1] : '');
        const bodyStart = open ? open[0].length : 0;
        const nested = block.indexOf('<property', bodyStart);
        const close = block.lastIndexOf('</property>');
        const bodyEnd = nested >= 0 ? nested : close;
        let encoded = bodyEnd >= bodyStart ? block.slice(bodyStart, bodyEnd).trim() : '';
        let value = encoded;
        if (attrs.encoding === 'base64' && encoded) {
          value = Buffer.from(encoded, 'base64').toString('utf8');
        } else if (attrs.type === 'undefined') {
          value = 'undefined';
        }
        values.push({ ...attrs, value });
        start = -1;
      }
    }
  }
  return values;
}

class DbgpPacketParser {
  constructor(onPacket) {
    this.onPacket = onPacket;
    this.buffer = Buffer.alloc(0);
    this.expected = null;
  }

  push(chunk) {
    this.buffer = Buffer.concat([this.buffer, Buffer.from(chunk)]);
    for (;;) {
      if (this.expected === null) {
        const zero = this.buffer.indexOf(0);
        if (zero < 0) return;
        const prefix = this.buffer.subarray(0, zero).toString('ascii');
        if (!/^\d+$/.test(prefix)) throw new Error(`Invalid DBGp length prefix: ${prefix}`);
        this.expected = Number(prefix);
        this.buffer = this.buffer.subarray(zero + 1);
      }
      if (this.buffer.length < this.expected + 1) return;
      if (this.buffer[this.expected] !== 0) throw new Error('DBGp packet missing NUL terminator');
      const packet = this.buffer.subarray(0, this.expected).toString('utf8');
      this.buffer = this.buffer.subarray(this.expected + 1);
      this.expected = null;
      this.onPacket(packet);
    }
  }
}

class DbgpSession {
  constructor() {
    this.server = null;
    this.socket = null;
    this.parser = null;
    this.transaction = 0;
    this.pending = new Map();
    this.initPromise = new Promise((resolve, reject) => {
      this.resolveInit = resolve;
      this.rejectInit = reject;
    });
  }

  async listen(host = '127.0.0.1') {
    this.server = net.createServer((socket) => this.accept(socket));
    await new Promise((resolve, reject) => {
      this.server.once('error', reject);
      this.server.listen(0, host, resolve);
    });
    return this.server.address().port;
  }

  accept(socket) {
    if (this.socket) {
      socket.destroy();
      return;
    }
    this.socket = socket;
    socket.setNoDelay(true);
    this.parser = new DbgpPacketParser((xml) => this.onPacket(xml));
    socket.on('data', (chunk) => {
      try { this.parser.push(chunk); } catch (error) { this.fail(error); }
    });
    socket.on('error', (error) => this.fail(error));
    socket.on('close', () => this.fail(new Error('DBGp connection closed')));
    if (this.server) this.server.close();
  }

  onPacket(xml) {
    const info = rootInfo(xml);
    const packet = { xml, ...info };
    if (info.name === 'init') {
      this.resolveInit(packet);
      return;
    }
    if (info.name !== 'response') return;
    const tx = Number(info.attributes.transaction_id);
    const pending = this.pending.get(tx);
    if (!pending) return;
    this.pending.delete(tx);
    const error = xml.match(/<error\b[^>]*code="(\d+)"/);
    if (error) {
      const failure = new Error(`DBGp command failed with code ${error[1]}`);
      failure.code = Number(error[1]);
      failure.packet = packet;
      pending.reject(failure);
    } else {
      pending.resolve(packet);
    }
  }

  waitForInit(timeoutMs = 10000) {
    return Promise.race([
      this.initPromise,
      new Promise((_, reject) => setTimeout(() => reject(new Error('DBGp init timeout')), timeoutMs)),
    ]);
  }

  send(command) {
    if (!this.socket || this.socket.destroyed) return Promise.reject(new Error('DBGp is not connected'));
    const tx = ++this.transaction;
    const promise = new Promise((resolve, reject) => this.pending.set(tx, { resolve, reject, command }));
    this.socket.write(`${command} -i ${tx}\0`, 'utf8');
    return promise;
  }

  fail(error) {
    if (this.rejectInit) this.rejectInit(error);
    for (const pending of this.pending.values()) pending.reject(error);
    this.pending.clear();
  }

  close() {
    if (this.socket) this.socket.destroy();
    if (this.server) this.server.close();
    this.socket = null;
    this.server = null;
  }
}

module.exports = {
  DbgpPacketParser,
  DbgpSession,
  decodeXml,
  findElements,
  findTopLevelProperties,
  parseAttributes,
  rootInfo,
};
