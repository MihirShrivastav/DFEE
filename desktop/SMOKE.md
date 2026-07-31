# DFEE Desktop Smoke Test

Run this checklist from a Release build of `DFEE.exe`. The app must use the native
`dfee_core` path; the React and Python development harness is not involved.

## Launch

1. Start `desktop/out/build/Release/DFEE.exe`.
2. Confirm the Graphite window opens without a console or error dialog.
3. Confirm the preview canvas remains usable while no image is loaded.

## Preview And Controls

1. Select **Open image** and load a TIFF from `comparision/`.
2. Confirm a fitted preview appears and the UI remains responsive during the decode.
3. Select a film stock, for example **Kodak Portra 400**. Confirm the preview updates.
4. Change Scene placement, Film exposure, Film contrast, and a Color Character control.
   Confirm the final slider positions render without a backlog of obsolete preview frames.
5. For a colour stock, adjust Color density, Color boost, Highlight saturation,
   and Shadow saturation. Open a monochrome stock and confirm those controls are
   visibly unavailable.
6. In Material Finish, turn off **Match grain to film speed**. Confirm the solver
   supplies editable Strength, Size, and Roughness values rather than generic defaults.
7. Adjust Halation Strength, Halation Threshold, and Bloom Amount. Confirm only the
   final combination is rendered after rapid changes.
8. Open a supported RAW file from `raw_files/` and repeat the stock/control checks.
9. While one large image is loading, request a different image. Confirm the final
   preview is for the most recently selected file.

## Export

1. Select each available format: 8-bit PNG, 16-bit PNG, 16-bit TIFF, and JPEG.
   Confirm JPEG quality and TIFF DPI are only shown for their applicable formats.
2. With a loaded image, selected stock, and non-neutral Film Lab controls, export once.
3. Confirm the full-resolution export overlay appears, duplicate export requests
   are unavailable, and it clears when the operation finishes.
4. Wait for the status line to show `Exported: <path>` and confirm the matching
   file exists beside the source image and opens in a viewer.
5. Confirm a non-writable source directory reports an export failure while leaving
   the application usable.

## Failure Handling

1. Attempt to open an unsupported or corrupt file.
2. Confirm the status line shows an `Open failed` message and the last valid preview
   remains available.

## Lightroom Classic Round-Trip

1. In Lightroom Classic, configure DFEE as an Additional External Editor using the
   Phase A 16-bit sRGB TIFF settings documented in `desktop/README.md`.
2. Use **Photo > Edit In > DFEE** on a RAW/DNG. Confirm DFEE opens with the subtitle
   `Lightroom round-trip TIFF`, image opening is unavailable, and export format options
   are hidden.
3. Apply a stock and one Film Lab control, then select **Save Back to Lightroom**.
4. Confirm the status reports the original handed-off TIFF path, no `*_dfee.*` sibling
   is created, and Lightroom shows the edited TIFF beside/stacked with the source.
5. Repeat with DFEE closed without saving. Confirm Lightroom's working TIFF remains
   unchanged and readable.
