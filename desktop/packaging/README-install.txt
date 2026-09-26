Film Lab — installation & Lightroom setup
==========================================

Film Lab works two ways:
  - On its own: open Film Lab and edit RAW files (and TIFFs) directly from its
    library. RAWs are developed to look like Lightroom's default rendering
    before the film treatment is applied.
  - From Lightroom: send a photo with Lightroom's "Edit In" menu and save the
    result back into your catalog.

1. INSTALL
   - Installer: run FilmLab-Setup.exe and follow the prompts, OR
   - Portable:  unzip the "FilmLab" folder anywhere (keep all files together)
                and note the path to FilmLab.exe.

   To update an existing installer-based copy, run the newer FilmLab-Setup.exe.
   It closes Film Lab if needed and replaces the app files in the same location.
   Your settings and logs are kept. To remove it completely, use "Uninstall Film
   Lab" from the Start menu or Windows Settings > Apps.

2. USE IT ON ITS OWN
   - Start Film Lab, add a folder of photos to the library (or use Open), and
     pick a photo. Supported: most camera RAW files (Sony, Nikon, Canon, Fujifilm,
     Panasonic, Olympus/OM, Pentax, Samsung, Hasselblad, DNG) and TIFF.
   - Colour matches Lightroom most closely when Lightroom Classic (or Adobe's
     free DNG Converter) is installed on the same computer: Film Lab then uses
     the camera colour profiles those apps install. Without them it falls back
     to generic camera colour.
   - Export saves next to the original file.
   - Not yet supported: Nikon "High Efficiency" RAW (HE / HE*). Send those
     photos from Lightroom instead (below), or shoot Lossless compressed RAW.

3. HOOK IT INTO LIGHTROOM CLASSIC (one time)
   Edit > Preferences > External Editing.
   Under "Additional External Editor" set:
       Application : <install folder>\FilmLab.exe
                     (installer default: C:\Program Files\Film Lab\FilmLab.exe)
       File Format : TIFF
       Color Space : sRGB
       Bit Depth   : 16 bits/component
       Compression : None
   (Use sRGB exactly. Film Lab reads the TIFF as sRGB; ProPhoto or Adobe RGB
    would come in with darker shadows and muted colour.)
   Then, from the "Preset" dropdown at the top of that panel, choose
   "Save Current Settings as New Preset..." and name it  Film Lab.
   Click OK.

4. USE IT FROM LIGHTROOM
   - Select a photo in Lightroom.
   - Photo > Edit In > Film Lab.
   - Choose "Edit a Copy with Lightroom Adjustments" and click Edit.
   - Film Lab opens on that image. Pick a film stock, adjust, then click
     "Save & Return to Lightroom".
   - The finished copy appears stacked next to your original in Lightroom.
