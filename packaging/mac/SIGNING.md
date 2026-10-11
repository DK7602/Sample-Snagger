# Signing and notarising the Mac build

Without this, the Mac installer works but macOS warns that it "can't be opened" and the user
has to allow it by hand (README, *Install*). With it, the installer opens like any other.

It needs a paid **Apple Developer Program** membership (99 USD a year). Nothing in the code
changes: the build signs and notarises by itself as soon as five repository secrets exist. Until
then it is built exactly as before.

> This path has been written and syntax-checked but **not yet run with real certificates** -
> nobody but the certificate's owner can. Expect to look at the first signed run's log.

## 1. The two certificates

In [developer.apple.com](https://developer.apple.com/account/resources/certificates/list) ->
Certificates -> **+**, make both of these (each asks for a "certificate signing request", which
Keychain Access makes: *Keychain Access -> Certificate Assistant -> Request a Certificate From a
Certificate Authority -> Saved to disk*):

- **Developer ID Application** - signs the Audio Unit, the VST3 and the app
- **Developer ID Installer** - signs the `.pkg`

Download each and double-click it, so both are in your login keychain with their private keys.

## 2. Export them as one file

In Keychain Access -> *My Certificates*, select both certificates (Cmd-click), right-click ->
**Export 2 items...**, format *Personal Information Exchange (.p12)*, and give it a password.

Turn the file into text for a secret:

    base64 -i Certificates.p12 | pbcopy

## 3. An app-specific password

At [account.apple.com](https://account.apple.com) -> Sign-In and Security -> **App-Specific
Passwords**, make one (call it "notarytool"). It looks like `abcd-efgh-ijkl-mnop`.

Your **Team ID** is on [developer.apple.com/account](https://developer.apple.com/account) under
*Membership details* (ten letters and digits).

## 4. The five secrets

In the GitHub repository: *Settings -> Secrets and variables -> Actions -> New repository secret*.

| Secret | What goes in it |
|---|---|
| `MACOS_CERTIFICATE_P12` | the base64 text from step 2 |
| `MACOS_CERTIFICATE_PASSWORD` | the password you gave the .p12 |
| `APPLE_ID` | the e-mail address of your Apple developer account |
| `APPLE_TEAM_ID` | your Team ID |
| `APPLE_APP_PASSWORD` | the app-specific password from step 3 |

## What the build then does

1. `sign.sh` signs the three bundles with the Developer ID Application certificate, with the
   hardened runtime, a timestamp and `entitlements.plist` (audio input, for REC INPUT).
2. `build_pkg.sh` signs the installer with the Developer ID Installer certificate, sends it to
   Apple's notary service, waits for the answer and staples the result to the `.pkg`.
3. The zip of the raw bundles is notarised too (a zip can't carry the proof itself; macOS looks it
   up the first time the bundles are used).

Notarisation adds a few minutes to the macOS build. If Apple turns something down, the step
fails and the run's summary says so; `xcrun notarytool log <submission id>` (with the same
account options) shows the reason.

Once a signed release is out, the "not notarised yet" paragraph in the README's *Install*
section can go.

## Doing it by hand

    export MACOS_SIGN_IDENTITY="Developer ID Application: Your Name (TEAMID)"
    export MACOS_INSTALLER_IDENTITY="Developer ID Installer: Your Name (TEAMID)"
    export APPLE_ID=you@example.com APPLE_TEAM_ID=TEAMID APPLE_APP_PASSWORD=abcd-efgh-ijkl-mnop
    bash packaging/mac/sign.sh build/SampleSnagger_artefacts/Release
    bash packaging/mac/build_pkg.sh build/SampleSnagger_artefacts/Release dist
