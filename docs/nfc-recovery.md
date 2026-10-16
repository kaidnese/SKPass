# NFC failure recovery (2026-10-10)

USB heartbeat is produced independently of the NFC task; it does not prove RF Discovery is active. Authentication status 03 is a failed authentication response, not proof of a blank card or a particular wrong key. RF/session failures must be considered too.

Fixed recovery paths:

- Failed Classic reads and failed write operations report their existing result, abandon the RF session, and enter the normal two-second initialization retry. A pending write is not automatically resent.
- Each initialization attempt now pulses PN7150 VEN low for 20 ms then waits 100 ms after raising it. This resets the NFCC and credit state without rebooting ESP32 or USB.
- Discovery selection has a bounded activation wait. A timeout after target discovery/selection is an invalid session, not ordinary no-card silence.
- RF_DEACTIVATE response 06 (REJECTED) no longer counts as proof of Discovery. RF_DISCOVER is explicitly requested; failure enters reset/reinitialization.
- CDC debug messages expose NFC_RECOVERY and NFC_READY so the host can distinguish USB liveness from RF readiness.

No ESP32 build or hardware validation was performed. Rebuild/flash the firmware to apply these changes; changing the Qt EXE or AimeIO DLL alone cannot fix them. Test weak coupling and card removal during authentication, then present a known-good card. Recovery should show NFC_RECOVERY followed by NFC_READY without USB replugging. Persistent weak RF can still cause repeated read failures and recovery; this change does not compensate for antenna detuning caused by the enclosure.
