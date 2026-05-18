# Phase 9 Known Issues And Retirement Recommendation

Status: interim.

## Recommendation

Do not retire the original login server yet. Keep the original login server as
the production fallback until the post-fix real-client localhost and LAN gates
are run and accepted.

The source2 automated parity evidence is now strong enough to proceed to real
client acceptance:

- full source2 CI passes,
- target DB-backed login gate passes,
- source2 login/world serve and probe passes with one advertised world,
- automated source2 probe through the host LAN address `192.168.1.41:9100`
  passes with one advertised world,
- target login account authentication passes,
- target world registration using `login_worldservers` credentials passes,
- target character-list reply is observed by the source2 probe,
- failed-login diagnostics emit redacted `login_rejected` evidence.

Those gates do not prove that the EQ2 client UI progresses through every legacy
scenario. The real client remains the acceptance authority for Phase 9.

## Known Issues / Gaps

No approved behavioral differences from the original login server are listed
yet.

Open validation gaps:

- A fresh post-fix real-client source2 packet capture has not been collected.
- The real client has not been observed progressing past `Trying login server
  #1` against the current source2 build.
- The real client has not been observed showing the source2 registered world
  list on localhost or LAN. The automated LAN-address probe passes, so the
  remaining LAN check is the real client UI/path.
- The real client has not been observed loading the target account character
  list through source2.
- Real-client create/delete/play flows have not been accepted.
- Real-client failed-login diagnostics have not been captured with
  `--diagnostic-events`.
- A workspace-local client sandbox avoids editing the installed client config,
  but launching it from this shell fails before login UI with
  `D3DERR_NOTAVAILABLE`; the remaining real-client gates need an interactive
  desktop with DirectX available.
- Phase commits have not been made because this process cannot write the Git
  index or object database.

## Retirement Gate

Change the recommendation to "retire legacy login for the covered deployment"
only after:

- both localhost and LAN real-client gates pass,
- a fresh post-fix source2 packet or diagnostic comparison is recorded,
- any source2/original differences are explicitly approved,
- Phase 0 through Phase 9 commits exist in Git.
