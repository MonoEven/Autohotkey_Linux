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
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import Meta from 'gi://Meta';
import Shell from 'gi://Shell';
import * as Main from 'resource:///org/gnome/shell/ui/main.js';
import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';

const BUS_NAME = 'io.github.autohotkey.GlobalHotkeys1';
const OBJ_PATH = '/io/github/autohotkey/GlobalHotkeys1';
const IFACE = 'io.github.autohotkey.GlobalHotkeys1';

// Interface (protocol root): string ids, accelerators, booleans.
// Register(s id, s accelerator, u flags) -> b   (flags reserved; 0 = exclusive)
// Unregister(s id) -> b
// ClearOwner(s owner)
// signals: Activated(s id, u timestamp), Deactivated(s id, u timestamp)
const ifaceXml = `
<node>
  <interface name="${IFACE}">
    <method name="Register">
      <arg type="s" name="id" direction="in"/>
      <arg type="s" name="accelerator" direction="in"/>
      <arg type="u" name="flags" direction="in"/>
      <arg type="b" name="success" direction="out"/>
    </method>
    <method name="Unregister">
      <arg type="s" name="id" direction="in"/>
      <arg type="b" name="success" direction="out"/>
    </method>
    <method name="ClearOwner">
      <arg type="s" name="owner" direction="in"/>
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
  </interface>
</node>`;

export default class AhkGlobalHotkeysExtension extends Extension {
    enable() {
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

        // auto-reload-safe: if the shell reloaded while we were enabled the
        // runtime will re-sync anyway (state truth lives in the AHK runtime).
    }

    disable() {
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
        if (!owner || this._grabs.has(id)) {
            invocation.return_value(new GLib.Variant('(b)', [false]));
            return;
        }
        // v1: only exclusive (suppress) registrations, flags=0.
        if (flags !== 0) {
            invocation.return_value(new GLib.Variant('(b)', [false]));
            return;
        }
        const action = global.display.grab_accelerator(
            accelerator, Meta.KeyBindingFlags.TRIGGER_RELEASE);
        if (action === Meta.KeyBindingAction.NONE) {
            invocation.return_value(new GLib.Variant('(b)', [false]));
            return;
        }
        // Whitelist the binding in gnome-shell, exactly like the portal path.
        const name = Meta.external_binding_name_for_action(action);
        Main.wm.allowKeybinding(name, Shell.ActionMode.NORMAL);

        this._grabs.set(id, {action, owner, name});
        this._byAction.set(action, id);
        if (!this._owners.has(owner))
            this._owners.set(owner, new Set());
        this._owners.get(owner).add(id);
        this._watchOwner(owner);
        invocation.return_value(new GLib.Variant('(b)', [true]));
    }

    UnregisterAsync([id], invocation) {
        const owner = invocation.get_sender();
        const g = this._grabs.get(id);
        if (!g || g.owner !== owner) {
            invocation.return_value(new GLib.Variant('(b)', [false]));
            return;
        }
        this._ungrab(id, g);
        invocation.return_value(new GLib.Variant('(b)', [true]));
    }

    ClearOwnerAsync([owner], invocation) {
        const set = this._owners.get(owner);
        if (set) {
            for (const id of [...set])
                this._ungrab(id, this._grabs.get(id));
        }
        invocation.return_value(new GLib.Variant('(b)', [true]));
    }

    // --- signal handlers -----------------------------------------------------

    _onActivated(display, action, device, timestamp) {
        const id = this._byAction.get(action);
        if (id === undefined)
            return;
        this._dbus.emit_signal('Activated', new GLib.Variant('(su)',
            [id, timestamp]));
    }

    _onDeactivated(display, action, device, timestamp) {
        const id = this._byAction.get(action);
        if (id === undefined)
            return;
        this._dbus.emit_signal('Deactivated', new GLib.Variant('(su)',
            [id, timestamp]));
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
