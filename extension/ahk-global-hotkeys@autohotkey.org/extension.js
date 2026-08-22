// AutoHotkey Global Hotkeys - GNOME 49 Wayland exclusive-hotkey backend.
//
// This is intentionally a THIN broker.  It does NOT parse AHK scripts, does
// NOT execute commands and does NOT fork AHK.  It only:
//   1. receives Register/Unregister/ClearOwner requests from AHK runtimes
//      over the session bus (io.github.autohotkey.GlobalHotkeys1),
//   2. registers accelerators with global.display.grab_accelerator() followed
//      by Main.wm.allowKeybinding() - the same whitelist step the
//      xdg-desktop-portal-gnome backend performs via shellDBus
//      (GrabAccelerators + allowKeybinding), which is what makes the grabbed
//      binding actually activate instead of being silently filtered by
//      WindowManager._filterKeybinding(),
//   3. emits Activated/Deactivated (action -> hotkey id) back to the owner.
//
// Registration model: each AHK process is an independent D-Bus peer (owner =
// its unique bus name).  ClearOwner removes every registration of that owner
// (used at script exit); the extension also cleans up after a vanished peer
// (fail-open).  On disable/unload every grab is released.
//
// Capability scope (v1, verified on GNOME 49): exclusive hotkeys 1:: / ^1::
// / F12:: etc. with zero confirmation.  Deactivated is relayed so future
// 1 up:: support can be evaluated, but v1 does not claim it.  ~1:: passthrough,
// remaps, a & b:: and full wildcards are NOT in scope (evdev/ahk-inputd lane).
//
// §4 (check_detail0821 §4): clipboard-change notifications.  Mutter does NOT
// implement the Wayland ext-data-control protocol, so the extension listens
// to Meta.Selection's `owner-changed` signal (the same mechanism Clipman
// uses) and broadcasts a ClipboardChanged(type) signal on the session bus.
// The AHK runtime subscribes and fires its OnClipboardChange callback; the
// runtime reads the clipboard text itself (it already has the Wayland data
// device path), so the extension never marshals clipboard contents -- a thin
// broker here too.  type: 1 = new owner (non-empty), 0 = cleared (owner
// null / selection without content).
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import Meta from 'gi://Meta';
import Shell from 'gi://Shell';
import * as Main from 'resource:///org/gnome/shell/ui/main.js';
import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';
import * as Config from 'resource:///org/gnome/shell/misc/config.js';

const BUS_NAME = 'io.github.autohotkey.GlobalHotkeys1';
const OBJ_PATH = '/io/github/autohotkey/GlobalHotkeys1';
const IFACE = 'io.github.autohotkey.GlobalHotkeys1';

// Version coupling (check0820 P1): the extension is written against a
// stable API surface (Meta.Display.grab_accelerator + Main.wm.allowKeybinding
// are stable across the GNOME 45-50 series, and the code is ESM so it only
// loads on 45+), and metadata.json declares that range so GNOME 45-50 will
// accept it.  But only GNOME 49 was machine-verified; loading on any other
// major must NOT fail silently.  Log a clear one-time warning.
const VERIFIED_GNOME_MAJOR = 49;
function _gnomeMajor() {
    try {
        const v = String(Config.PACKAGE_VERSION || '');
        const m = parseInt(v.split('.')[0], 10);
        return Number.isNaN(m) ? 0 : m;
    } catch (e) {
        return 0; // Version module unavailable: cannot check.
    }
}

// Interface (protocol root): string ids, accelerators, booleans.
// Register(s id, s accelerator, u flags) -> b   (flags reserved; 0 = exclusive)
// RegisterMany(as ids, as accelerators, au flags) -> ab
// Unregister(s id) -> b
// UnregisterMany(as ids) -> b
// ClearOwner() -> b   (owner = the caller's unique bus name)
// signals: Activated(s id, u timestamp), Deactivated(s id, u timestamp)
//   emitted DIRECTED to the registering owner (never broadcast).
//
// Security model (2026-08 check0819 review):
//   - ids are owner-scoped: "<unique-bus-name>/<registration>" and Register
//     rejects ids outside the caller's namespace;
//   - ClearOwner acts on the caller's own unique name (no owner argument);
//   - Unregister/UnregisterMany only touch the caller's own registrations;
//   - Activated/Deactivated are delivered to the owning peer only, so no
//     other session peer can receive or forge another process's hotkeys.
const ifaceXml = `
<node>
  <interface name="${IFACE}">
    <method name="Register">
      <arg type="s" name="id" direction="in"/>
      <arg type="s" name="accelerator" direction="in"/>
      <arg type="u" name="flags" direction="in"/>
      <arg type="b" name="success" direction="out"/>
    </method>
    <method name="RegisterMany">
      <arg type="as" name="ids" direction="in"/>
      <arg type="as" name="accelerators" direction="in"/>
      <arg type="au" name="flags" direction="in"/>
      <arg type="ab" name="results" direction="out"/>
    </method>
    <method name="Unregister">
      <arg type="s" name="id" direction="in"/>
      <arg type="b" name="success" direction="out"/>
    </method>
    <method name="UnregisterMany">
      <arg type="as" name="ids" direction="in"/>
      <arg type="b" name="success" direction="out"/>
    </method>
    <method name="ClearOwner">
      <arg type="b" name="success" direction="out"/>
    </method>
    <signal name="Activated">
      <arg type="s" name="id"/>
      <arg type="u" name="timestamp"/>
    </signal>
    <signal name="Deactivated">
      <arg type="s" name="id"/>
      <arg type="u" name="timestamp"/>
    </signal>
    <signal name="ClipboardChanged">
      <arg type="u" name="type"/>
    </signal>
  </interface>
</node>`;

export default class AhkGlobalHotkeysExtension extends Extension {
    enable() {
        // Version-coupling guard (check0820 P1): unverified GNOME majors
        // must not fail silently.  The API surface is stable 45-50, but only
        // 49 was tested in the VM/CI matrix; make the risk visible.
        const major = _gnomeMajor();
        if (major !== VERIFIED_GNOME_MAJOR) {
            log(`[AHK-GS] running on GNOME Shell ${String(Config.PACKAGE_VERSION)} `
                + `(major ${major}); verified only on ${VERIFIED_GNOME_MAJOR}. `
                + `If global hotkeys do not respond, report the shell version.`);
        }

        this._grabs = new Map();   // id -> {action, owner}
        this._byAction = new Map(); // action -> id
        this._owners = new Map();  // owner -> Set(id)

        this._info = Gio.DBusNodeInfo.new_for_xml(ifaceXml);
        this._iface = this._info.interfaces[0];
        this._dbus = Gio.DBusExportedObject.wrapJSObject(this._iface, this);
        this._dbus.export(Gio.DBus.session, OBJ_PATH);

        this._ownerId = Gio.bus_own_name(Gio.BusType.SESSION, BUS_NAME,
            Gio.BusNameOwnerFlags.NONE,
            this._onBusAcquired.bind(this),
            null /* name acquired */,
            () => log('[AHK-GS] bus name lost during enable'));

        // Watch every peer we register from; when it vanishes, drop its grabs
        // so a crashed/killed AHK can never leave hotkeys behind.
        this._watches = new Map(); // owner(bus name) -> watch id

        this._activatedSig = global.display.connect('accelerator-activated',
            this._onActivated.bind(this));
        this._deactivatedSig = global.display.connect('accelerator-deactivated',
            this._onDeactivated.bind(this));

        // §4: clipboard-change notifications (Meta.Selection owner-changed).
        // The API exists on GNOME 42+ (our declared 45-50 range); guard so an
        // older/newer shell fails open (no notifications) instead of breaking
        // the extension.
        this._selectionSig = null;
        try {
            this._selection = global.display.get_selection();
            this._selectionSig = this._selection.connect('owner-changed',
                this._onSelectionChanged.bind(this));
            log('[AHK-GS] clipboard watch armed');
        } catch (e) {
            log(`[AHK-GS] clipboard watch unavailable: ${e}`);
        }

        // auto-reload-safe: if the shell reloaded while we were enabled the
        // runtime will re-sync anyway (state truth lives in the AHK runtime).
    }

    disable() {
        if (this._selectionSig !== null && this._selection) {
            this._selection.disconnect(this._selectionSig);
            this._selectionSig = null;
            this._selection = null;
        }
        if (this._activatedSig) {
            global.display.disconnect(this._activatedSig);
            this._activatedSig = null;
        }
        if (this._deactivatedSig) {
            global.display.disconnect(this._deactivatedSig);
            this._deactivatedSig = null;
        }
        if (this._ownerId) {
            Gio.bus_unown_name(this._ownerId);
            this._ownerId = null;
        }
        if (this._dbus) {
            this._dbus.unexport();
            this._dbus = null;
        }
        // Release every grab (fail-open on extension unload/reload).
        for (const [id, g] of this._grabs)
            this._ungrab(id, g);
        this._grabs.clear();
        this._byAction.clear();
        this._owners.clear();
        for (const [, w] of this._watches)
            Gio.bus_unwatch_name(w);
        this._watches.clear();
    }

    // --- D-Bus method handlers ----------------------------------------------

    _onBusAcquired() {
        log('[AHK-GS] bus name acquired');
    }

    RegisterAsync([id, accelerator, flags], invocation) {
        const owner = invocation.get_sender();
        if (!owner) {
            invocation.return_value(new GLib.Variant('(b)', [false]));
            return;
        }
        invocation.return_value(new GLib.Variant('(b)',
            [this._registerOne(owner, String(id), String(accelerator), flags)]));
    }

    RegisterManyAsync([ids, accels, flags], invocation) {
        const owner = invocation.get_sender();
        if (!owner || !Array.isArray(ids) || !Array.isArray(accels)
                || ids.length !== accels.length) {
            invocation.return_value(new GLib.Variant('(ab)', [[]]));
            return;
        }
        const results = [];
        for (let i = 0; i < ids.length; i++) {
            const fl = Array.isArray(flags) ? Number(flags[i] || 0) : 0;
            results.push(this._registerOne(owner, String(ids[i]),
                String(accels[i]), fl));
        }
        invocation.return_value(new GLib.Variant('(ab)', [results]));
    }

    UnregisterAsync([id], invocation) {
        const owner = invocation.get_sender();
        const g = this._grabs.get(String(id));
        if (!g || g.owner !== owner) {
            invocation.return_value(new GLib.Variant('(b)', [false]));
            return;
        }
        this._ungrab(String(id), g);
        invocation.return_value(new GLib.Variant('(b)', [true]));
    }

    UnregisterManyAsync([ids], invocation) {
        const owner = invocation.get_sender();
        if (!Array.isArray(ids)) {
            invocation.return_value(new GLib.Variant('(b)', [false]));
            return;
        }
        for (const id of ids) {
            const g = this._grabs.get(String(id));
            if (g && g.owner === owner)
                this._ungrab(String(id), g);
        }
        invocation.return_value(new GLib.Variant('(b)', [true]));
    }

    ClearOwnerAsync([], invocation) {
        // The caller's own unique bus name; no owner argument is accepted,
        // so one peer can never clear another peer's registrations.
        const owner = invocation.get_sender();
        const set = owner ? this._owners.get(owner) : null;
        if (set) {
            for (const id of [...set])
                this._ungrab(id, this._grabs.get(id));
        }
        invocation.return_value(new GLib.Variant('(b)', [true]));
    }

    // One registration attempt (shared by Register and RegisterMany).
    _registerOne(owner, id, accelerator, flags) {
        if (this._grabs.has(id))
            return false;
        // Owner-scoped ids ("<unique-name>/<registration>"): reject foreign
        // ids so no peer can register into another peer's namespace.
        if (!id.startsWith(owner + '/'))
            return false;
        // v1: only exclusive (suppress) registrations, flags=0.
        if (flags !== 0)
            return false;
        const action = global.display.grab_accelerator(
            accelerator, Meta.KeyBindingFlags.TRIGGER_RELEASE);
        if (action === Meta.KeyBindingAction.NONE)
            return false;
        // Whitelist the binding in gnome-shell, exactly like the portal path.
        const name = Meta.external_binding_name_for_action(action);
        Main.wm.allowKeybinding(name, Shell.ActionMode.NORMAL);

        this._grabs.set(id, {action, owner, name});
        this._byAction.set(action, id);
        if (!this._owners.has(owner))
            this._owners.set(owner, new Set());
        this._owners.get(owner).add(id);
        this._watchOwner(owner);
        return true;
    }

    // --- signal handlers -----------------------------------------------------

    _onActivated(display, action, device, timestamp) {
        const id = this._byAction.get(action);
        if (id === undefined)
            return;
        const g = this._grabs.get(id);
        if (!g)
            return;
        // Directed to the registering owner only (never broadcast): another
        // session peer can neither receive nor spoof this hotkey.
        if (!Gio.DBus.session.emit_signal(g.owner, OBJ_PATH, IFACE, 'Activated',
                new GLib.Variant('(su)', [id, timestamp])))
            log(`[AHK-GS] Activated delivery to ${g.owner} failed`);
    }

    _onDeactivated(display, action, device, timestamp) {
        const id = this._byAction.get(action);
        if (id === undefined)
            return;
        const g = this._grabs.get(id);
        if (!g)
            return;
        Gio.DBus.session.emit_signal(g.owner, OBJ_PATH, IFACE, 'Deactivated',
            new GLib.Variant('(su)', [id, timestamp]));
    }

    // §4: broadcast a clipboard-owner change.  The AHK runtime subscribes and
    // reads the clipboard itself; this signal is only a trigger.
    _onSelectionChanged(selection, selectionType, owner) {
        // Meta.SelectionType.CLIPBOARD reads undefined on GNOME 49 (verified
        // on the VM: owner-changed type=1 for wl-copy), so pin the Mutter
        // enum order value (PRIMARY=0, CLIPBOARD=1, SECONDARY=2).
        const CLIPBOARD = 1;
        if (selectionType !== CLIPBOARD)
            return;
        // owner == null => the selection was cleared (Type 0).
        const type = owner ? 1 : 0;
        if (!Gio.DBus.session.emit_signal(null, OBJ_PATH, IFACE, 'ClipboardChanged',
                new GLib.Variant('(u)', [type])))
            log('[AHK-GS] ClipboardChanged broadcast failed');
    }

    // --- helpers -------------------------------------------------------------

    _ungrab(id, g) {
        if (!g)
            return;
        try {
            global.display.ungrab_accelerator(g.action);
            if (g.name)
                Main.wm.allowKeybinding(g.name, Shell.ActionMode.NONE);
        } catch (e) {
            log(`[AHK-GS] ungrab ${id}: ${e}`);
        }
        this._grabs.delete(id);
        this._byAction.delete(g.action);
        const ownerSet = this._owners.get(g.owner);
        if (ownerSet) {
            ownerSet.delete(id);
            if (ownerSet.size === 0)
                this._owners.delete(g.owner);
        }
    }

    _watchOwner(owner) {
        if (this._watches.has(owner))
            return;
        const watchId = Gio.bus_watch_name(Gio.BusType.SESSION, owner, 0,
            null /* acquired */,
            () => { // name_vanished -> the AHK process died: drop its grabs.
                this._watches.delete(owner);
                const set = this._owners.get(owner);
                if (set) {
                    for (const id of [...set])
                        this._ungrab(id, this._grabs.get(id));
                }
            });
        this._watches.set(owner, watchId);
    }
}
