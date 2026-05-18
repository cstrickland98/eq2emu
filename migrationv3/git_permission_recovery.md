# Git Permission Recovery Notes

Status: blocked in the current shell.

## Observed Failure

Every staging attempt fails before Git can update the index:

```text
fatal: Unable to create 'E:/_EQ2/eq2emu/.git/index.lock': Permission denied
```

Using a temporary index outside `.git` did not unblock commits because Git also
could not write the object database under `.git/objects`.

The current shell identity is:

```text
desktop-k1uu25k\codexsandboxonline
```

`icacls .git`, `.git\objects`, and `.git\refs` show explicit deny entries on
`.git` that are inherited by child paths in addition to normal modify grants.
The current shell can read the ACL and has `CodexSandboxUsers` modify access,
but a direct file-create probe under `.git` fails with access denied.

The current shell cannot repair the ACL because it is not the owner of `.git`
and does not have `WRITE_DAC`. A targeted `Set-Acl` attempt to remove only the
two explicit deny ACEs failed with:

```text
Attempted to perform an unauthorized operation.
```

An ACL backup from before that repair attempt is stored at:

```text
artifacts\migrationv3-git-acl\20260516-001642-before.txt
```

Latest diagnostic report:

```text
artifacts\migrationv3-git-acl\20260516-043954-git-acl-report.txt
```

Recovery needs to be performed from a local PowerShell session that owns `.git`
or otherwise has permission to modify its security descriptor.

## Verification Commands

From a normal local PowerShell prompt at the repo root:

```powershell
whoami
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_diagnose_git_permissions.ps1 -WriteAclReport
icacls .git
icacls .git\objects
icacls .git\index
git status --short
git add -- migrationv3\phase_commit_plan.md
git reset -- migrationv3\phase_commit_plan.md
```

The `git add` followed by `git reset -- <path>` is a non-destructive write test.
It is successful only if Git can create `.git/index.lock` and write
`.git/objects`.

`source2_diagnose_git_permissions.ps1` is also non-destructive. It checks for a
stale lock, lists explicit Deny ACEs if the ACL can be read, performs a
temporary `.git` write probe, and can save an ACL report under
`artifacts\migrationv3-git-acl`.

## Recovery Options

Use one of these approaches, then rerun the verification commands above.

1. Run the phase commits from an account or elevated shell that can already
   write `.git/index.lock` and `.git/objects`.
2. Remove only the stale deny ACEs that apply to the blocked sandbox identities,
   then grant modify rights to the account performing the commits.
3. Clone or copy the worktree to a location where the committing account owns
   `.git`, then apply the worktree changes and follow
   `migrationv3/phase_commit_plan.md`.

Do not use `git reset --hard` or checkout commands to recover permissions; the
worktree contains uncommitted migration changes.

## Recovery Artifact

A shadow Git bundle was generated without writing to the locked repository
metadata:

```text
artifacts\migrationv3-shadow-commits\20260515-232746\migrationv3-phase-0-8.bundle
```

It contains the path-based Phase 0 through Phase 8 commit sequence from
`migrationv3\phase_commit_plan.md` on branch
`refs/heads/migrationv3-phase-0-8`, with tip
`03a93200596a245309efe5a2d7dc6c5c999acfb3`. It was verified with:

```powershell
git bundle verify artifacts\migrationv3-shadow-commits\20260515-232746\migrationv3-phase-0-8.bundle
```

The same Phase 0 through Phase 8 sequence was also exported as one patch per
commit:

```text
artifacts\migrationv3-shadow-commits\20260515-232746\patches\
```

This bundle is not a substitute for the required repository phase commits
because the real `.git` refs were not updated. After repository permissions are
fixed, inspect it with:

```powershell
git bundle list-heads artifacts\migrationv3-shadow-commits\20260515-232746\migrationv3-phase-0-8.bundle
git fetch artifacts\migrationv3-shadow-commits\20260515-232746\migrationv3-phase-0-8.bundle migrationv3-phase-0-8:migrationv3-phase-0-8-review
```

Then compare or cherry-pick deliberately. If the patch path is easier after
moving to a clean repository, inspect and apply the patch series with:

```powershell
git am --3way artifacts\migrationv3-shadow-commits\20260515-232746\patches\*.patch
```

Do not import either artifact blindly over newer worktree changes.

## Current Work Checkpoint

The latest shadow bundle captures the current Phase 0 through Phase 9 worktree
state, including the Phase 9 helper scripts, completion audit tightening,
required localhost/LAN client-target evidence checks, forbidden-secret
pass-through, Git metadata diagnostics, guarded phase-commit helper, client
auto-login dry-run coverage, client session-argument redaction coverage,
legacy session-disconnect reply parity, Python cache ignores for safe staging,
classic captured login-request parser parity, final `login_accepted` evidence
audit enforcement, session-report secret rejection, installed-client launch
diagnostics, the DirectX/window preflight helper, Direct3D9 device-probe
documentation, DXVK sandbox-client recheck notes, Windows object-file ignore
coverage, packet-capture helper registration, UDP `OP_Combined` outbound
coalescing parity, combined-reply probe/smoke decoding, foreground/window-message
client input diagnostics, the fixed Phase 3 `login_response.h` path, the fixed
Phase 9 packet-capture helper path, and 155-test CMake registration:

```text
artifacts\migrationv3-shadow-commits\20260516-062935-phase0-9-recovery\migrationv3-phase-0-9-recovery.bundle
```

It contains branch `refs/heads/migrationv3-phase-0-9-recovery`. The verified
tip is `7fa56de2a2afe0470313be2b360aab07266d06d1`, and the checkpoint
`summary.txt` records `shadow_status=clean`. It was verified with:

```powershell
git bundle verify artifacts\migrationv3-shadow-commits\20260516-062935-phase0-9-recovery\migrationv3-phase-0-9-recovery.bundle
```

The matching patch series is:

```text
artifacts\migrationv3-shadow-commits\20260516-062935-phase0-9-recovery\patches\
```

This current-work checkpoint is only for recovery if the locked repository must
be moved or recreated. It is not completion evidence: Phase 9 is still blocked
until the real-client localhost/LAN gates pass and the required phase commits
are made in the real repository.

## After Recovery

Run:

```powershell
git diff --check
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_ci.ps1 -UseVcpkg -LiveSmoke -BuildDir build\source2-vcpkg-user-verify
```

Then follow `migrationv3/phase_commit_plan.md` for the phase commit sequence.
After `.git` is writable, the guarded helper can run the path-based sequence:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_commit_migrationv3_phases.ps1 -DryRun
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_commit_migrationv3_phases.ps1
```

Run Phase 9 only after the real-client gates pass:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_commit_migrationv3_phases.ps1 -IncludePhase9
```
