# DFEE Lightroom Classic Round-Trip

Date: 2026-07-31  
Status: Phase A implemented (external-editor handoff)

## Goal

Let Lightroom Classic hand DFEE a rendered working TIFF, let the photographer edit it
in the native app, and save the result back into that exact file without ever modifying
the catalog's original RAW or DNG.

This intentionally uses Lightroom Classic's supported **Additional External Editor**
workflow first. Lightroom creates and catalogs the derivative TIFF itself, so DFEE does
not need catalog write access, a blocking Lua plug-in, or a second import path.

## Contract

Lightroom's standard Additional External Editor launch is:

```text
DFEE.exe "C:\\absolute\\path\\to\\Lightroom-working-file.tif"
```

DFEE also accepts `--lightroom-edit <tiff>` for deterministic automation and installer
integration. Both forms accept only an existing absolute `.tif` or `.tiff` path. The app:

1. enters `lightroomRoundTrip` mode and locks image selection and export format;
2. decodes the working TIFF on the serial engine worker;
3. labels its primary command **Save & Return to Lightroom**;
4. renders a 16-bit TIFF to a temporary sibling; and
5. atomically replaces the working TIFF only after encoding and metadata work succeed.

After a successful save, DFEE exits so Lightroom can complete its external-editor
session and refresh the derivative. If encoding, metadata patching, or replacement
fails, the original Lightroom working file remains intact and DFEE remains open. A
photographer can also close DFEE without saving; Lightroom receives no partial
derivative.

## Lightroom Setup (Phase A)

In Lightroom Classic, open **Edit > Preferences > External Editing** and configure an
Additional External Editor preset:

| Setting | Phase A value |
| --- | --- |
| Application | `DFEE.exe` from the Release build or installed application |
| File Format | TIFF |
| Color Space | sRGB |
| Bit Depth | 16 bits/component |
| Compression | None |
| Resolution | Photographer's normal print setting |

Then select a RAW/DNG, use **Photo > Edit In > DFEE**, edit normally, and press
**Save & Return to Lightroom**. Lightroom owns the resulting TIFF and its stack placement.

## Color-Space Scope

Adobe recommends 16-bit ProPhoto RGB for maximum retained color detail in external
editing. DFEE's current rendered-TIFF ingest converts only explicitly selected named
spaces into its linear-sRGB working representation, while its TIFF writer does not yet
embed an ICC profile. Therefore Phase A supports only the sRGB preset above. Do not
configure ProPhoto RGB, Adobe RGB, HDR, or a custom profile for this release.

The next color-management task must add embedded-ICC detection, a wide-gamut working
transform, and tagged TIFF output before a ProPhoto preset is supported.

## Validation

Automated smoke path:

```powershell
$env:QT_QPA_PLATFORM = "offscreen"
$env:DFEE_SELFTEST = "D:\\path\\to\\working-copy.tif"
$env:DFEE_SELFTEST_EXPORT = "D:\\path\\to\\result.log"
$env:DFEE_SELFTEST_EXPORT_FORMAT = "tiff"
.\\desktop\\out\\build\\Release\\DFEE.exe --lightroom-edit "D:\\path\\to\\working-copy.tif"
```

Acceptance criteria:

- app opens in Lightroom mode and the regular export format picker is absent;
- Save Back writes the same TIFF pathname, not a `_dfee` sibling;
- no temporary `*.dfee-writing.*` file remains after success or failure;
- a failed output replacement leaves the handed-off TIFF readable;
- a real Lightroom smoke test shows the updated derivative associated with the source.

## Follow-On Phases

1. **Color management:** ICC detection and tagged/wide-gamut TIFF output.
2. **Plugin convenience layer:** a `.lrplugin` command that invokes the same contract
   and reports actionable setup diagnostics. It must not duplicate Lightroom's file
   creation/import responsibility.
3. **Packaging:** installer, file association, code signing, and robust app-path
   discovery for the Lightroom preset.
