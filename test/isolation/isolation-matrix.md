# Isolation matrix (generated — P9-T2)

Generated with `--as-of 2026-09-10` (frozen-clock law). Do not hand-edit; regenerate with `tools/isolation_matrix.py`.

Cells: 160 total — EXCEPTION=20, NOT-RUN=110, PASS=30

| mechanism | pair | mode | verdict | detail |
|---|---|---|---|---|
| storage-scope | standard/shield | fake | PASS | standard: storage_scope=kIdentityScoped in_memory=False; shield: storage_scope=kIdentityScoped in_memory=False |
| storage-scope | standard/fortress | fake | PASS | standard: storage_scope=kIdentityScoped in_memory=False; fortress: storage_scope=kFortressPartition in_memory=False |
| storage-scope | standard/ephemeral | fake | PASS | standard: storage_scope=kIdentityScoped in_memory=False; ephemeral: storage_scope=kEphemeral in_memory=True |
| storage-scope | shield/fortress | fake | PASS | shield: storage_scope=kIdentityScoped in_memory=False; fortress: storage_scope=kFortressPartition in_memory=False |
| storage-scope | shield/ephemeral | fake | PASS | shield: storage_scope=kIdentityScoped in_memory=False; ephemeral: storage_scope=kEphemeral in_memory=True |
| vault-scope | standard/shield | fake | PASS | standard: autofill=False export=False; shield: autofill=False export=False |
| vault-scope | standard/fortress | fake | PASS | standard: autofill=False export=False; fortress: autofill=False export=False |
| vault-scope | standard/ephemeral | fake | PASS | standard: autofill=False export=False; ephemeral: autofill=False export=False |
| vault-scope | shield/fortress | fake | PASS | shield: autofill=False export=False; fortress: autofill=False export=False |
| vault-scope | shield/ephemeral | fake | PASS | shield: autofill=False export=False; ephemeral: autofill=False export=False |
| egress-route | standard/shield | fake | PASS | standard: route=kDirect block_3p=False; shield: route=kProxy block_3p=True |
| egress-route | standard/fortress | fake | PASS | standard: route=kDirect block_3p=False; fortress: route=kTor block_3p=True |
| egress-route | standard/ephemeral | fake | PASS | standard: route=kDirect block_3p=False; ephemeral: route=kDirect block_3p=False |
| egress-route | shield/fortress | fake | PASS | shield: route=kProxy block_3p=True; fortress: route=kTor block_3p=True |
| egress-route | shield/ephemeral | fake | PASS | shield: route=kProxy block_3p=True; ephemeral: route=kDirect block_3p=False |
| permission-overlay | standard/shield | fake | PASS | standard: perms={"camera":"kAsk","geolocation":"kAsk","microphone":"kAsk","notifications":"kAsk"}; shield: perms={"camera":"kDeny","geolocation":"kDeny","microphone":"kDeny","notifications":"kAsk"} |
| permission-overlay | standard/fortress | fake | PASS | standard: perms={"camera":"kAsk","geolocation":"kAsk","microphone":"kAsk","notifications":"kAsk"}; fortress: perms={"camera":"kDeny","geolocation":"kDeny","microphone":"kDeny","notifications":"kDeny"} |
| permission-overlay | standard/ephemeral | fake | PASS | standard: perms={"camera":"kAsk","geolocation":"kAsk","microphone":"kAsk","notifications":"kAsk"}; ephemeral: perms={"camera":"kAsk","geolocation":"kAsk","microphone":"kAsk","notifications":"kAsk"} |
| permission-overlay | shield/fortress | fake | PASS | shield: perms={"camera":"kDeny","geolocation":"kDeny","microphone":"kDeny","notifications":"kAsk"}; fortress: perms={"camera":"kDeny","geolocation":"kDeny","microphone":"kDeny","notifications":"kDeny"} |
| permission-overlay | shield/ephemeral | fake | PASS | shield: perms={"camera":"kDeny","geolocation":"kDeny","microphone":"kDeny","notifications":"kAsk"}; ephemeral: perms={"camera":"kAsk","geolocation":"kAsk","microphone":"kAsk","notifications":"kAsk"} |
| fingerprint-mode | standard/shield | fake | PASS | standard: mode=kReduce; shield: mode=kReduce |
| fingerprint-mode | standard/fortress | fake | PASS | standard: mode=kReduce; fortress: mode=kStrict |
| fingerprint-mode | standard/ephemeral | fake | PASS | standard: mode=kReduce; ephemeral: mode=kReduce |
| fingerprint-mode | shield/fortress | fake | PASS | shield: mode=kReduce; fortress: mode=kStrict |
| fingerprint-mode | shield/ephemeral | fake | PASS | shield: mode=kReduce; ephemeral: mode=kReduce |
| process-isolation | standard/shield | fake | PASS | standard: site_isolated=True dedicated=False; shield: site_isolated=True dedicated=False |
| process-isolation | standard/fortress | fake | PASS | standard: site_isolated=True dedicated=False; fortress: site_isolated=True dedicated=True |
| process-isolation | standard/ephemeral | fake | PASS | standard: site_isolated=True dedicated=False; ephemeral: site_isolated=True dedicated=False |
| process-isolation | shield/fortress | fake | PASS | shield: site_isolated=True dedicated=False; fortress: site_isolated=True dedicated=True |
| process-isolation | shield/ephemeral | fake | PASS | shield: site_isolated=True dedicated=False; ephemeral: site_isolated=True dedicated=False |
| cookies-1p | standard/shield | browser | NOT-RUN | 1P cookies partitioned per identity |
| cookies-1p | standard/fortress | browser | NOT-RUN | 1P cookies partitioned per identity |
| cookies-1p | standard/ephemeral | browser | NOT-RUN | 1P cookies partitioned per identity |
| cookies-1p | shield/fortress | browser | NOT-RUN | 1P cookies partitioned per identity |
| cookies-1p | shield/ephemeral | browser | NOT-RUN | 1P cookies partitioned per identity |
| cookies-3p-chips | standard/shield | browser | NOT-RUN | 3P cookies + CHIPS partitioned per identity |
| cookies-3p-chips | standard/fortress | browser | NOT-RUN | 3P cookies + CHIPS partitioned per identity |
| cookies-3p-chips | standard/ephemeral | browser | NOT-RUN | 3P cookies + CHIPS partitioned per identity |
| cookies-3p-chips | shield/fortress | browser | NOT-RUN | 3P cookies + CHIPS partitioned per identity |
| cookies-3p-chips | shield/ephemeral | browser | NOT-RUN | 3P cookies + CHIPS partitioned per identity |
| localstorage | standard/shield | browser | NOT-RUN | localStorage partitioned per identity |
| localstorage | standard/fortress | browser | NOT-RUN | localStorage partitioned per identity |
| localstorage | standard/ephemeral | browser | NOT-RUN | localStorage partitioned per identity |
| localstorage | shield/fortress | browser | NOT-RUN | localStorage partitioned per identity |
| localstorage | shield/ephemeral | browser | NOT-RUN | localStorage partitioned per identity |
| sessionstorage | standard/shield | browser | NOT-RUN | sessionStorage partitioned per identity |
| sessionstorage | standard/fortress | browser | NOT-RUN | sessionStorage partitioned per identity |
| sessionstorage | standard/ephemeral | browser | NOT-RUN | sessionStorage partitioned per identity |
| sessionstorage | shield/fortress | browser | NOT-RUN | sessionStorage partitioned per identity |
| sessionstorage | shield/ephemeral | browser | NOT-RUN | sessionStorage partitioned per identity |
| indexeddb | standard/shield | browser | NOT-RUN | IndexedDB partitioned per identity |
| indexeddb | standard/fortress | browser | NOT-RUN | IndexedDB partitioned per identity |
| indexeddb | standard/ephemeral | browser | NOT-RUN | IndexedDB partitioned per identity |
| indexeddb | shield/fortress | browser | NOT-RUN | IndexedDB partitioned per identity |
| indexeddb | shield/ephemeral | browser | NOT-RUN | IndexedDB partitioned per identity |
| cache-api | standard/shield | browser | NOT-RUN | Cache API partitioned per identity |
| cache-api | standard/fortress | browser | NOT-RUN | Cache API partitioned per identity |
| cache-api | standard/ephemeral | browser | NOT-RUN | Cache API partitioned per identity |
| cache-api | shield/fortress | browser | NOT-RUN | Cache API partitioned per identity |
| cache-api | shield/ephemeral | browser | NOT-RUN | Cache API partitioned per identity |
| serviceworker | standard/shield | browser | NOT-RUN | ServiceWorker registrations + push state partitioned |
| serviceworker | standard/fortress | browser | NOT-RUN | ServiceWorker registrations + push state partitioned |
| serviceworker | standard/ephemeral | browser | NOT-RUN | ServiceWorker registrations + push state partitioned |
| serviceworker | shield/fortress | browser | NOT-RUN | ServiceWorker registrations + push state partitioned |
| serviceworker | shield/ephemeral | browser | NOT-RUN | ServiceWorker registrations + push state partitioned |
| broadcastchannel | standard/shield | browser | NOT-RUN | BroadcastChannel not observable across identities |
| broadcastchannel | standard/fortress | browser | NOT-RUN | BroadcastChannel not observable across identities |
| broadcastchannel | standard/ephemeral | browser | NOT-RUN | BroadcastChannel not observable across identities |
| broadcastchannel | shield/fortress | browser | NOT-RUN | BroadcastChannel not observable across identities |
| broadcastchannel | shield/ephemeral | browser | NOT-RUN | BroadcastChannel not observable across identities |
| sharedworker | standard/shield | browser | NOT-RUN | SharedWorker not shared across identities |
| sharedworker | standard/fortress | browser | NOT-RUN | SharedWorker not shared across identities |
| sharedworker | standard/ephemeral | browser | NOT-RUN | SharedWorker not shared across identities |
| sharedworker | shield/fortress | browser | NOT-RUN | SharedWorker not shared across identities |
| sharedworker | shield/ephemeral | browser | NOT-RUN | SharedWorker not shared across identities |
| messagechannel-opener | standard/shield | browser | NOT-RUN | MessageChannel-to-opener not routable across identities |
| messagechannel-opener | standard/fortress | browser | NOT-RUN | MessageChannel-to-opener not routable across identities |
| messagechannel-opener | standard/ephemeral | browser | NOT-RUN | MessageChannel-to-opener not routable across identities |
| messagechannel-opener | shield/fortress | browser | NOT-RUN | MessageChannel-to-opener not routable across identities |
| messagechannel-opener | shield/ephemeral | browser | NOT-RUN | MessageChannel-to-opener not routable across identities |
| window-name | standard/shield | browser | NOT-RUN | window.name not carried across identity boundaries |
| window-name | standard/fortress | browser | NOT-RUN | window.name not carried across identity boundaries |
| window-name | standard/ephemeral | browser | NOT-RUN | window.name not carried across identity boundaries |
| window-name | shield/fortress | browser | NOT-RUN | window.name not carried across identity boundaries |
| window-name | shield/ephemeral | browser | NOT-RUN | window.name not carried across identity boundaries |
| downloads-metadata | standard/shield | browser | NOT-RUN | downloads metadata scoped per identity |
| downloads-metadata | standard/fortress | browser | NOT-RUN | downloads metadata scoped per identity |
| downloads-metadata | standard/ephemeral | browser | NOT-RUN | downloads metadata scoped per identity |
| downloads-metadata | shield/fortress | browser | NOT-RUN | downloads metadata scoped per identity |
| downloads-metadata | shield/ephemeral | browser | NOT-RUN | downloads metadata scoped per identity |
| notification-state | standard/shield | browser | NOT-RUN | notification prompts + granted-origin state per identity |
| notification-state | standard/fortress | browser | NOT-RUN | notification prompts + granted-origin state per identity |
| notification-state | standard/ephemeral | browser | NOT-RUN | notification prompts + granted-origin state per identity |
| notification-state | shield/fortress | browser | NOT-RUN | notification prompts + granted-origin state per identity |
| notification-state | shield/ephemeral | browser | NOT-RUN | notification prompts + granted-origin state per identity |
| clipboard | standard/shield | browser | NOT-RUN | clipboard permission-level isolation |
| clipboard | standard/fortress | browser | NOT-RUN | clipboard permission-level isolation |
| clipboard | standard/ephemeral | browser | NOT-RUN | clipboard permission-level isolation |
| clipboard | shield/fortress | browser | NOT-RUN | clipboard permission-level isolation |
| clipboard | shield/ephemeral | browser | NOT-RUN | clipboard permission-level isolation |
| hsts-expect-ct | standard/shield | browser | NOT-RUN | HSTS/expect-CT visibility per identity |
| hsts-expect-ct | standard/fortress | browser | NOT-RUN | HSTS/expect-CT visibility per identity |
| hsts-expect-ct | standard/ephemeral | browser | NOT-RUN | HSTS/expect-CT visibility per identity |
| hsts-expect-ct | shield/fortress | browser | NOT-RUN | HSTS/expect-CT visibility per identity |
| hsts-expect-ct | shield/ephemeral | browser | NOT-RUN | HSTS/expect-CT visibility per identity |
| tls-resumption | standard/shield | browser | NOT-RUN | TLS session resumption ticket reuse per identity |
| tls-resumption | standard/fortress | browser | NOT-RUN | TLS session resumption ticket reuse per identity |
| tls-resumption | standard/ephemeral | browser | NOT-RUN | TLS session resumption ticket reuse per identity |
| tls-resumption | shield/fortress | browser | NOT-RUN | TLS session resumption ticket reuse per identity |
| tls-resumption | shield/ephemeral | browser | NOT-RUN | TLS session resumption ticket reuse per identity |
| devtools-attach | standard/shield | browser | NOT-RUN | DevTools attach cannot cross identity boundaries |
| devtools-attach | standard/fortress | browser | NOT-RUN | DevTools attach cannot cross identity boundaries |
| devtools-attach | standard/ephemeral | browser | NOT-RUN | DevTools attach cannot cross identity boundaries |
| devtools-attach | shield/fortress | browser | NOT-RUN | DevTools attach cannot cross identity boundaries |
| devtools-attach | shield/ephemeral | browser | NOT-RUN | DevTools attach cannot cross identity boundaries |
| omnibox | standard/shield | browser | NOT-RUN | omnibox history/suggest surface per identity |
| omnibox | standard/fortress | browser | NOT-RUN | omnibox history/suggest surface per identity |
| omnibox | standard/ephemeral | browser | NOT-RUN | omnibox history/suggest surface per identity |
| omnibox | shield/fortress | browser | NOT-RUN | omnibox history/suggest surface per identity |
| omnibox | shield/ephemeral | browser | NOT-RUN | omnibox history/suggest surface per identity |
| print | standard/shield | browser | NOT-RUN | print path cannot exfiltrate another identity's document |
| print | standard/fortress | browser | NOT-RUN | print path cannot exfiltrate another identity's document |
| print | standard/ephemeral | browser | NOT-RUN | print path cannot exfiltrate another identity's document |
| print | shield/fortress | browser | NOT-RUN | print path cannot exfiltrate another identity's document |
| print | shield/ephemeral | browser | NOT-RUN | print path cannot exfiltrate another identity's document |
| drag-across-identity | standard/shield | browser | NOT-RUN | tab-join (drag) between identities refused |
| drag-across-identity | standard/fortress | browser | NOT-RUN | tab-join (drag) between identities refused |
| drag-across-identity | standard/ephemeral | browser | NOT-RUN | tab-join (drag) between identities refused |
| drag-across-identity | shield/fortress | browser | NOT-RUN | tab-join (drag) between identities refused |
| drag-across-identity | shield/ephemeral | browser | NOT-RUN | tab-join (drag) between identities refused |
| dns-cache-observable | standard/shield | exception | EXCEPTION | per-context host cache is partition-local but the OS resolver cache is shared |
| dns-cache-observable | standard/fortress | exception | EXCEPTION | per-context host cache is partition-local but the OS resolver cache is shared |
| dns-cache-observable | standard/ephemeral | exception | EXCEPTION | per-context host cache is partition-local but the OS resolver cache is shared |
| dns-cache-observable | shield/fortress | exception | EXCEPTION | per-context host cache is partition-local but the OS resolver cache is shared |
| dns-cache-observable | shield/ephemeral | exception | EXCEPTION | per-context host cache is partition-local but the OS resolver cache is shared |
| gpu-texture-side-channel | standard/shield | exception | EXCEPTION | GPU process texture state is shared (documented, not asserted-closed) |
| gpu-texture-side-channel | standard/fortress | exception | EXCEPTION | GPU process texture state is shared (documented, not asserted-closed) |
| gpu-texture-side-channel | standard/ephemeral | exception | EXCEPTION | GPU process texture state is shared (documented, not asserted-closed) |
| gpu-texture-side-channel | shield/fortress | exception | EXCEPTION | GPU process texture state is shared (documented, not asserted-closed) |
| gpu-texture-side-channel | shield/ephemeral | exception | EXCEPTION | GPU process texture state is shared (documented, not asserted-closed) |
| favicon-cache | standard/shield | exception | EXCEPTION | shared favicon cache discloses visit history across identities |
| favicon-cache | standard/fortress | exception | EXCEPTION | shared favicon cache discloses visit history across identities |
| favicon-cache | standard/ephemeral | exception | EXCEPTION | shared favicon cache discloses visit history across identities |
| favicon-cache | shield/fortress | exception | EXCEPTION | shared favicon cache discloses visit history across identities |
| favicon-cache | shield/ephemeral | exception | EXCEPTION | shared favicon cache discloses visit history across identities |
| http-auth-cache | standard/shield | exception | EXCEPTION | HTTP-auth cache is Profile-level (shared across identities) |
| http-auth-cache | standard/fortress | exception | EXCEPTION | HTTP-auth cache is Profile-level (shared across identities) |
| http-auth-cache | standard/ephemeral | exception | EXCEPTION | HTTP-auth cache is Profile-level (shared across identities) |
| http-auth-cache | shield/fortress | exception | EXCEPTION | HTTP-auth cache is Profile-level (shared across identities) |
| http-auth-cache | shield/ephemeral | exception | EXCEPTION | HTTP-auth cache is Profile-level (shared across identities) |
| extension-availability | standard/shield | not-yet | NOT-RUN | not-yet (owner phase P13) |
| extension-availability | standard/fortress | not-yet | NOT-RUN | not-yet (owner phase P13) |
| extension-availability | standard/ephemeral | not-yet | NOT-RUN | not-yet (owner phase P13) |
| extension-availability | shield/fortress | not-yet | NOT-RUN | not-yet (owner phase P13) |
| extension-availability | shield/ephemeral | not-yet | NOT-RUN | not-yet (owner phase P13) |
| blob-storage | standard/shield | not-yet | NOT-RUN | not-yet (owner phase P11) |
| blob-storage | standard/fortress | not-yet | NOT-RUN | not-yet (owner phase P11) |
| blob-storage | standard/ephemeral | not-yet | NOT-RUN | not-yet (owner phase P11) |
| blob-storage | shield/fortress | not-yet | NOT-RUN | not-yet (owner phase P11) |
| blob-storage | shield/ephemeral | not-yet | NOT-RUN | not-yet (owner phase P11) |
