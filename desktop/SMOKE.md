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
   Shadow saturation, Palette range, and Color compression. Open a monochrome stock
   and confirm those controls are visibly unavailable.
6. In Material Finish, turn off **Match grain to film speed**. Confirm the solver
   supplies editable Strength, Size, and Roughness values rather than generic defaults.
7. Adjust Halation Strength, Halation Threshold, and Bloom Amount. Confirm only the
   final combination is rendered after rapid changes.
8. Open a supported RAW file from `raw_files/` and repeat the stock/control checks.
9. While one large image is loading, request a different image. Confirm the final
   preview is for the most recently selected file.

## Export

1. With a loaded image, selected stock, and non-neutral Film Lab controls, click **Export TIFF** once.
2. Wait for the status line to show `Exported: <path>`.
3. Confirm the 16-bit TIFF exists beside the source image and opens in a viewer.
4. Confirm a non-writable source directory reports an export failure while leaving
   the application usable.

## Failure Handling

1. Attempt to open an unsupported or corrupt file.
2. Confirm the status line shows an `Open failed` message and the last valid preview
   remains available.
