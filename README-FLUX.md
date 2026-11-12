# Flux (branch `flux`)

This branch carries the **Flux** source tree -- Deskflow 1.27.0 upstream sources merged with the Lingxi HarmonyOS file-transfer engine (hdc channel + KVM/DFTR channel), rebranded to **Flux** with an Apple-inspired UI on both Windows and HarmonyOS.

`main` keeps the original C# Lingxi WinForms source untouched.

## Layout

- `deskflow/` + `scripts/` + `README-dist.md` -- Windows Qt client/server   (Flux), including `src/lib/gui/AppleStyle.h` (global QSS) and the   `icons/flux-*/apps/64/org.flux.flux*.svg` app/tray icons.
- `harmony/` -- HarmonyOS client (`com.deskflow.ohospoc`): ETS UI,   app icons, and the DFTR/DDRG protocol C++ sources.

## Build

- Windows: `scripts/configure.cmd` then `scripts/build.cmd`, package with   `scripts/package.cmd`, install with `scripts/install.ps1`.
- HarmonyOS: DevEco Studio / hvigor `assembleHap --mode module   -p product=default`.

Prebuilt Windows portable zip and the signed HarmonyOS HAP are attached to Release `flux-1.27.0`.
