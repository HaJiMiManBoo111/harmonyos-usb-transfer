# Flux (branch `flux`)

This branch contains the **Flux** source tree: Deskflow 1.27.0 upstream sources merged with the Lingxi HarmonyOS file-transfer engine (hdc channel + KVM/DFTR channel), rebranded to Flux.

`main` keeps the original C# Lingxi WinForms source untouched.

- Build: `scripts/configure.cmd` then `scripts/build.cmd`
- Package: `scripts/package.cmd`
- Install: `scripts/install.ps1`
- Usage: see `README-dist.md`

Prebuilt portable zip is attached to Release `flux-1.27.0`.
