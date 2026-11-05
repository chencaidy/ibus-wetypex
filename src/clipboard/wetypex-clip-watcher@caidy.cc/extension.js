import Gio from "gi://Gio";
import GLib from "gi://GLib";
import Meta from "gi://Meta";
import St from "gi://St";
import { Extension } from "resource:///org/gnome/shell/extensions/extension.js";
import * as Main from "resource:///org/gnome/shell/ui/main.js";

const NAME = "org.gnome.Shell.Extensions.WeTypeXClipWatcher";
const PATH = "/org/gnome/Shell/Extensions/WeTypeXClipWatcher";
const IFACE = "org.gnome.Shell.Extensions.WeTypeXClipWatcher";
const ALLOWED_EXE = ["/usr/bin/wetypex-clipboard"]; // Only these executables can subscribe

const TTL_SEC = 5; // Expiration time of the content
const READ_TIMEOUT_SEC = 5; // Give up on a stalled clipboard transfer
const MAX_TEXT_BYTES = 64 * 1024;
const MAX_IMAGE_BYTES = 8 * 1024 * 1024;

const TEXT_MIME = "text/plain;charset=utf-8";
// X11-style aliases are what St.Clipboard itself accepts for text
const X11_TEXT_MIMES = ["UTF8_STRING", "STRING"];
// Encoded image formats, in order of preference. The bytes are passed through
// untouched, so nothing is re-encoded here.
const IMAGE_MIMES = [
  "image/png",
  "image/jpeg",
  "image/webp",
  "image/gif",
  "image/bmp",
  "image/tiff",
];
const SENSITIVE_MIMES = ["x-kde-passwordManagerHint"]; // Set by password managers on copy

const ClipWatcherIface = `
<node>
  <interface name="${IFACE}">
    <method name="Subscribe"/>
    <method name="GetText">
      <arg type="u" direction="in" name="serial"/>
      <arg type="s" direction="out" name="text"/>
    </method>
    <method name="GetImage">
      <arg type="u" direction="in" name="serial"/>
      <arg type="ay" direction="out" name="data"/>
    </method>
    <method name="SetText">
      <arg type="s" direction="in" name="text"/>
    </method>
    <method name="SetImage">
      <arg type="ay" direction="in" name="data"/>
    </method>
    <signal name="Changed">
      <arg type="u" name="serial"/>
      <arg type="u" name="length"/>
      <arg type="s" name="mime"/>
    </signal>
  </interface>
</node>`;

const UTF8 = new TextEncoder();

const isTextMime = (mime) =>
  mime.startsWith("text/") || X11_TEXT_MIMES.includes(mime);

// Clients write whatever the peer copied, so the format is sniffed from the
// payload instead of trusting a mime string that could be wrong.
const startsWith = (data, signature, offset = 0) => {
  if (data.length < offset + signature.length) return false;
  for (let i = 0; i < signature.length; i++)
    if (data[offset + i] !== signature[i]) return false;
  return true;
};

const sniffImageMime = (data) => {
  if (startsWith(data, [0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]))
    return "image/png";
  if (startsWith(data, [0xff, 0xd8, 0xff])) return "image/jpeg";
  if (startsWith(data, [0x47, 0x49, 0x46, 0x38])) return "image/gif";
  if (
    startsWith(data, [0x52, 0x49, 0x46, 0x46]) &&
    startsWith(data, [0x57, 0x45, 0x42, 0x50], 8)
  )
    return "image/webp";
  if (startsWith(data, [0x42, 0x4d])) return "image/bmp";
  if (
    startsWith(data, [0x49, 0x49, 0x2a, 0x00]) ||
    startsWith(data, [0x4d, 0x4d, 0x00, 0x2a])
  )
    return "image/tiff";
  return null;
};

const digest = (data) =>
  GLib.compute_checksum_for_data(GLib.ChecksumType.SHA256, data);

class Service {
  constructor(writeText, writeImage) {
    this._writeText = writeText;
    this._writeImage = writeImage;
    this._subs = new Map(); // authorized client's unique bus name -> watch id
    this._serial = 0;
    this._mime = null;
    this._data = null; // string for text, Uint8Array for images
    this._clearId = 0;
    this._destroyed = false;

    this._dbus = null;
    this._ownId = 0;
    try {
      this._dbus = Gio.DBusExportedObject.wrapJSObject(ClipWatcherIface, this);
      this._dbus.export(Gio.DBus.session, PATH);
      this._ownId = Gio.bus_own_name_on_connection(
        Gio.DBus.session,
        NAME,
        Gio.BusNameOwnerFlags.DO_NOT_QUEUE,
        null,
        (_c, name) =>
          console.warn(`WeTypeXClipWatcher: cannot acquire bus name ${name}`),
      );
    } catch (e) {
      // Never leave a half-exported object behind when construction fails.
      this._destroyed = true;
      this._teardown();
      throw e;
    }
  }

  hasSubscribers() {
    return this._subs.size > 0;
  }

  _getCreds(sender, cb) {
    Gio.DBus.session.call(
      "org.freedesktop.DBus",
      "/org/freedesktop/DBus",
      "org.freedesktop.DBus",
      "GetConnectionCredentials",
      new GLib.Variant("(s)", [sender]),
      new GLib.VariantType("(a{sv})"),
      Gio.DBusCallFlags.NONE,
      2000,
      null,
      (conn, res) => {
        let creds = null;
        if (!this._destroyed) {
          try {
            [creds] = conn.call_finish(res).recursiveUnpack();
          } catch (e) {
            creds = null;
          }
        }
        cb(creds);
      },
    );
  }

  _authorized(creds) {
    if (!creds) return false;
    if (creds.UnixUserID !== Gio.Credentials.new().get_unix_user())
      return false;
    try {
      // A package upgrade leaves " (deleted)" on the path of a running binary.
      const exe = GLib.file_read_link(`/proc/${creds.ProcessID}/exe`).replace(
        / \(deleted\)$/,
        "",
      );
      return ALLOWED_EXE.includes(exe);
    } catch (e) {
      return false;
    }
  }

  _deny(invocation) {
    invocation.return_dbus_error(`${IFACE}.Error.AccessDenied`, "not allowed");
  }

  // The credential lookup itself failed (timeout, busy bus, shutting down).
  // That says nothing about the caller, so it is a retryable error and must
  // never cost the client its subscription.
  _unavailable(invocation) {
    invocation.return_dbus_error(
      `${IFACE}.Error.Unavailable`,
      "cannot verify caller, try again",
    );
  }

  // Common gate for every call after Subscribe. Re-checks credentials on every
  // call: a client can exec another program while keeping the same bus
  // connection. Only a caller that is positively not allowed is dropped.
  _guard(invocation, fn) {
    const sender = invocation.get_sender();
    if (!this._subs.has(sender)) {
      this._deny(invocation);
      return;
    }
    this._getCreds(sender, (creds) => {
      if (!creds) {
        this._unavailable(invocation);
        return;
      }
      if (!this._authorized(creds)) {
        this._drop(sender);
        this._deny(invocation);
        return;
      }
      // The subscription may have been dropped or the service torn down while
      // the lookup was in flight.
      if (this._destroyed || !this._subs.has(sender)) {
        this._deny(invocation);
        return;
      }
      fn();
    });
  }

  SubscribeAsync(_params, invocation) {
    const sender = invocation.get_sender();
    this._getCreds(sender, (creds) => {
      if (!creds) {
        this._unavailable(invocation);
        return;
      }
      if (!this._authorized(creds)) {
        this._deny(invocation);
        return;
      }
      // Idempotent: a client may safely subscribe again.
      if (!this._subs.has(sender)) {
        // removed automatically when the client exits
        const id = Gio.bus_watch_name_on_connection(
          Gio.DBus.session,
          sender,
          Gio.BusNameWatcherFlags.NONE,
          null,
          () => this._drop(sender),
        );
        this._subs.set(sender, id);
      }
      invocation.return_value(new GLib.Variant("()", []));
    });
  }

  GetTextAsync([serial], invocation) {
    this._guard(invocation, () => {
      if (this._data === null || serial !== this._serial) {
        invocation.return_dbus_error(
          `${IFACE}.Error.Expired`,
          "content expired",
        );
        return;
      }
      if (!this._mime.startsWith("text/")) {
        invocation.return_dbus_error(
          `${IFACE}.Error.WrongType`,
          `content is ${this._mime}, not text`,
        );
        return;
      }
      invocation.return_value(new GLib.Variant("(s)", [this._data]));
    });
  }

  GetImageAsync([serial], invocation) {
    this._guard(invocation, () => {
      if (this._data === null || serial !== this._serial) {
        invocation.return_dbus_error(
          `${IFACE}.Error.Expired`,
          "content expired",
        );
        return;
      }
      if (!this._mime.startsWith("image/")) {
        invocation.return_dbus_error(
          `${IFACE}.Error.WrongType`,
          `content is ${this._mime}, not an image`,
        );
        return;
      }
      invocation.return_value(new GLib.Variant("(ay)", [this._data]));
    });
  }

  SetTextAsync([text], invocation) {
    this._guard(invocation, () => {
      if (typeof text !== "string" || text.length === 0) {
        invocation.return_dbus_error(
          "org.freedesktop.DBus.Error.InvalidArgs",
          "text must be a non-empty string",
        );
        return;
      }
      if (UTF8.encode(text).length > MAX_TEXT_BYTES) {
        invocation.return_dbus_error(
          "org.freedesktop.DBus.Error.InvalidArgs",
          "text too long",
        );
        return;
      }
      if (!this._writeText(text)) {
        invocation.return_dbus_error(
          `${IFACE}.Error.WriteFailed`,
          "cannot write to clipboard",
        );
        return;
      }
      invocation.return_value(new GLib.Variant("()", []));
    });
  }

  SetImageAsync([data], invocation) {
    this._guard(invocation, () => {
      const bytes = data instanceof Uint8Array ? data : null;
      if (!bytes || bytes.length === 0) {
        invocation.return_dbus_error(
          "org.freedesktop.DBus.Error.InvalidArgs",
          "image must be a non-empty byte array",
        );
        return;
      }
      if (bytes.length > MAX_IMAGE_BYTES) {
        invocation.return_dbus_error(
          "org.freedesktop.DBus.Error.InvalidArgs",
          "image too large",
        );
        return;
      }
      const mime = sniffImageMime(bytes);
      if (!mime) {
        invocation.return_dbus_error(
          "org.freedesktop.DBus.Error.InvalidArgs",
          "unsupported image format",
        );
        return;
      }
      if (!this._writeImage(mime, bytes)) {
        invocation.return_dbus_error(
          `${IFACE}.Error.WriteFailed`,
          "cannot write to clipboard",
        );
        return;
      }
      invocation.return_value(new GLib.Variant("()", []));
    });
  }

  _drop(sender) {
    const id = this._subs.get(sender);
    if (id !== undefined) {
      Gio.bus_unwatch_name(id);
      this._subs.delete(sender);
    }
  }

  publish(mime, data) {
    this._serial++;
    this._mime = mime;
    this._data = data;
    if (this._clearId) GLib.source_remove(this._clearId);
    this._clearId = GLib.timeout_add_seconds(
      GLib.PRIORITY_DEFAULT,
      TTL_SEC,
      () => {
        this._data = null;
        this._mime = null;
        this._clearId = 0;
        return GLib.SOURCE_REMOVE;
      },
    );

    const length =
      typeof data === "string" ? UTF8.encode(data).length : data.length;

    // Unicast, metadata only, no content
    for (const name of this._subs.keys()) {
      try {
        Gio.DBus.session.emit_signal(
          name,
          PATH,
          IFACE,
          "Changed",
          new GLib.Variant("(uus)", [this._serial, length, mime]),
        );
      } catch (e) {
        // One unreachable subscriber must not stop the others.
        console.warn(`WeTypeXClipWatcher: cannot notify ${name}: ${e}`);
      }
    }
  }

  destroy() {
    this._destroyed = true;
    this._teardown();
  }

  _teardown() {
    if (this._clearId) {
      GLib.source_remove(this._clearId);
      this._clearId = 0;
    }
    for (const id of this._subs.values()) Gio.bus_unwatch_name(id);
    this._subs.clear();
    if (this._dbus) {
      try {
        this._dbus.unexport();
      } catch (e) {
        // The object may already be gone after a failed construction.
      }
      this._dbus = null;
    }
    if (this._ownId) {
      Gio.bus_unown_name(this._ownId);
      this._ownId = 0;
    }
    this._data = null;
    this._mime = null;
  }
}

export default class WeTypeXClipWatcher extends Extension {
  enable() {
    // A re-entrant enable, or one after a failed enable, must not orphan the
    // previous signal handler, D-Bus name and clipboard watches.
    if (this._service || this._id) this.disable();

    this._ownText = null; // text we wrote ourselves, used to drop its echo
    this._ownTextUntil = 0;
    this._ownImage = null; // digest of the image we wrote ourselves
    this._ownImageUntil = 0;
    this._readGen = 0; // bumped for every copy, see _readLatest()
    this._reading = false;
    this._clipboard = St.Clipboard.get_default();
    this._service = new Service(
      (text) => this._writeText(text),
      (mime, data) => this._writeImage(mime, data),
    );
    this._selection = global.display.get_selection();

    this._id = this._selection.connect("owner-changed", (_sel, type) => {
      if (type !== Meta.SelectionType.SELECTION_CLIPBOARD) return;
      // With no authorized subscriber, never read the clipboard at all
      if (!this._service?.hasSubscribers()) return;
      // Skip while the screen is locked
      if (Main.sessionMode.isLocked || Main.screenShield?.locked) return;
      this._readGen++;
      if (!this._reading) this._readLatest();
    });
  }

  // Transfers run one at a time and only the newest generation is published.
  // An image transfer takes longer than the gap between two quick copies, so
  // overlapping reads finished out of order: they published the previous
  // frame after the current one, or both of them.
  _readLatest() {
    const gen = this._readGen;
    this._reading = true;
    this._readClipboard(gen)
      .catch((e) =>
        console.warn(`WeTypeXClipWatcher: clipboard read failed: ${e}`),
      )
      .finally(() => {
        this._reading = false;
        // A newer copy arrived while reading: the clipboard already holds it,
        // so read again instead of publishing what we just fetched.
        if (this._service && gen !== this._readGen) this._readLatest();
      });
  }

  // A clipboard owner can stall a transfer forever. Bounding it here lets the
  // async body finish and release its reference to the extension.
  _readWithTimeout(start) {
    return new Promise((resolve) => {
      let timer = GLib.timeout_add_seconds(
        GLib.PRIORITY_DEFAULT,
        READ_TIMEOUT_SEC,
        () => {
          timer = 0;
          resolve(null);
          return GLib.SOURCE_REMOVE;
        },
      );
      try {
        start((value) => {
          if (timer) {
            GLib.source_remove(timer);
            timer = 0;
          }
          resolve(value);
        });
      } catch (e) {
        // A synchronous failure must not leave the timeout source pending.
        if (timer) {
          GLib.source_remove(timer);
          timer = 0;
        }
        throw e;
      }
    });
  }

  async _readClipboard(gen) {
    const clipboard = this._clipboard;
    if (!clipboard) return;

    const type = St.ClipboardType.CLIPBOARD;
    const mimes = clipboard.get_mimetypes(type);
    if (!mimes || !mimes.length) return;
    // Skip content flagged by password managers
    if (mimes.some((m) => SENSITIVE_MIMES.includes(m))) return;

    // Text wins over images: a copy offering both is meant as text
    if (mimes.some((m) => isTextMime(m))) {
      const text = await this._readWithTimeout((done) =>
        clipboard.get_text(type, (_c, value) => done(value)),
      );
      if (gen !== this._readGen) return;
      if (text) {
        if (UTF8.encode(text).length > MAX_TEXT_BYTES) return;
        if (this._isOwnText(text)) return;
        this._service?.publish(TEXT_MIME, text);
        return;
      }
    }

    const mime = IMAGE_MIMES.find((m) => mimes.includes(m));
    if (!mime) return;
    const bytes = await this._readWithTimeout((done) =>
      clipboard.get_content(type, mime, (_c, value) => {
        if (!value || value.get_size() > MAX_IMAGE_BYTES) {
          done(null);
          return;
        }
        done(new Uint8Array(value.toArray()));
      }),
    );
    if (!bytes || gen !== this._readGen) return;
    if (this._isOwnImage(bytes)) return;
    this._service?.publish(mime, bytes);
  }

  // Writing the clipboard changes its owner, which emits owner-changed and
  // would otherwise be published back to subscribers as a fresh copy.
  _writeText(text) {
    this._ownText = text;
    this._ownTextUntil = GLib.get_monotonic_time() + 1000000; // 1s
    this._ownImage = null; // an older image echo can no longer match
    try {
      this._clipboard.set_text(St.ClipboardType.CLIPBOARD, text);
    } catch (e) {
      this._ownText = null;
      console.warn(`WeTypeXClipWatcher: cannot write clipboard: ${e}`);
      return false;
    }
    return true;
  }

  _writeImage(mime, data) {
    this._ownImage = digest(data);
    this._ownImageUntil = GLib.get_monotonic_time() + 1000000; // 1s
    this._ownText = null; // an older text echo can no longer match
    try {
      this._clipboard.set_content(
        St.ClipboardType.CLIPBOARD,
        mime,
        GLib.Bytes.new(data),
      );
    } catch (e) {
      this._ownImage = null;
      console.warn(`WeTypeXClipWatcher: cannot write clipboard image: ${e}`);
      return false;
    }
    return true;
  }

  _isOwnText(text) {
    if (this._ownText === null) return false;
    if (GLib.get_monotonic_time() > this._ownTextUntil) {
      this._ownText = null;
      return false;
    }
    if (text !== this._ownText) return false;
    this._ownText = null;
    return true;
  }

  _isOwnImage(data) {
    if (this._ownImage === null) return false;
    if (GLib.get_monotonic_time() > this._ownImageUntil) {
      this._ownImage = null;
      return false;
    }
    if (digest(data) !== this._ownImage) return false;
    this._ownImage = null;
    return true;
  }

  disable() {
    if (this._id) {
      this._selection?.disconnect(this._id);
      this._id = null;
    }
    // In-flight reads resolve later; dropping the service and bumping the
    // generation keeps them from publishing anything.
    this._readGen++;
    this._reading = false;
    this._service?.destroy();
    this._service = null;
    this._selection = null;
    this._clipboard = null;
    this._ownText = null;
    this._ownImage = null;
  }
}
