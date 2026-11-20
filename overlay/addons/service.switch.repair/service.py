"""One-shot restore of the add-on enabled set after an Addons33.db rebuild.

Why this exists
---------------
The preinit repair hook in the port's platform layer has to discard a database it
cannot repair: SQLite's VACUUM needs write/temp support that the SD's FAT does not
provide (measured: `disk I/O error`, in the port and from Python on the same file).
Kodi then recreates the database empty, and every add-on comes back disabled - which
users read as "it broke my add-ons".

So the hook exports the old enabled ids to a text file before renaming the database,
and this service re-applies them once the add-on manager is up. It runs every boot
but does nothing unless that file exists, and removes it afterwards.
"""

import json
import os

import xbmc

SIDECAR = "/switch/kodi/addons-enabled.txt"
LOG_PREFIX = "service.switch.repair: "


def log(msg):
    xbmc.log(LOG_PREFIX + msg, xbmc.LOGINFO)


def main():
    if not os.path.exists(SIDECAR):
        return

    try:
        with open(SIDECAR, encoding="utf-8", errors="replace") as fh:
            ids = [line.strip() for line in fh if line.strip()]
    except Exception as exc:  # noqa: BLE001 - never let this break startup
        log("cannot read %s: %s" % (SIDECAR, exc))
        ids = []

    if not ids:
        log("list is empty, nothing to restore")
    else:
        # Give the add-on manager a moment; running this in the first seconds of
        # startup can race with its own load of the database.
        if xbmc.Monitor().waitForAbort(5):
            return

        ok = 0
        for addon_id in ids:
            request = json.dumps({
                "jsonrpc": "2.0",
                "id": 1,
                "method": "Addons.SetAddonEnabled",
                "params": {"addonid": addon_id, "enabled": True},
            })
            try:
                reply = json.loads(xbmc.executeJSONRPC(request))
                if "error" not in reply:
                    ok += 1
            except Exception:
                pass
        log("re-applied %d of %d enabled add-on(s)" % (ok, len(ids)))

    try:
        os.remove(SIDECAR)
    except Exception:
        pass


if __name__ == "__main__":
    main()
