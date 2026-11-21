# AeroHub site

Landing page, accounts, licence keys, download and admin panel. No dependencies: Node 24+.

```
node server.js                          # http://localhost:8080 (PORT to change)
node server.js make-admin <username>    # after registering on the site
node server.js gen-keys <count> <days>  # days 0 = forever
```

- Installer for `/download`: put `AeroHub-Setup.exe` in `files/`. Only accounts with an active key can download.
- Database: `data/aerohub.db` (SQLite). `data/` and `files/` are not in git.
- Behind HTTPS set `SECURE_COOKIE=1`.
- Prices on the landing page are placeholders (`—`) in `public/index.html`.

Licence check for the hub: `POST /api/license/check` with JSON `{"key": "...", "hwid": "..."}` returns
`{"ok": true, "expires": <ms or null>}` or `{"ok": false, "reason": "..."}`. The first successful check binds the key
to that hwid; the owner can reset it in the account page.
