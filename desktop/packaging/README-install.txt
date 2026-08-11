Film Lab — installation & Lightroom setup
==========================================

Film Lab edits your photos FROM Lightroom. It is not a standalone editor —
you open it from Lightroom's "Edit In" menu. (Double-clicking FilmLab.exe on
its own just shows this reminder and closes.)

1. INSTALL
   - Installer: run FilmLab-Setup.exe and follow the prompts, OR
   - Portable:  unzip the "FilmLab" folder anywhere (keep all files together)
                and note the path to FilmLab.exe.

2. HOOK IT INTO LIGHTROOM CLASSIC (one time)
   Edit > Preferences > External Editing.
   Under "Additional External Editor" set:
       Application : <install folder>\FilmLab.exe
                     (installer default: C:\Program Files\Film Lab\FilmLab.exe)
       File Format : TIFF
       Color Space : ProPhoto RGB
       Bit Depth   : 16 bits/component
       Compression : None
   Then, from the "Preset" dropdown at the top of that panel, choose
   "Save Current Settings as New Preset..." and name it  Film Lab.
   Click OK.

3. USE IT
   - Select a photo in Lightroom.
   - Photo > Edit In > Film Lab.
   - Choose "Edit a Copy with Lightroom Adjustments" and click Edit.
   - Film Lab opens on that image. Pick a film stock, adjust, then click
     "Save & Return to Lightroom".
   - The finished copy appears stacked next to your original in Lightroom.

That's it. Every future edit is just: Edit In > Film Lab.
