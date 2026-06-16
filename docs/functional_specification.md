# DFEE (Digital Film Emulation Engine)
## Functional Specification Documentation

### 1. Product Vision
DFEE is not a "do it yourself editing tool." It is a dedicated **Film Emulation Application**. The primary goal is to provide perceptually accurate, authentic analog aesthetics based on the actual physical properties of film stocks, rather than relying on simple LUTs (Look-Up Tables). The app models everything from silver halide crystal structure to dye layer crosstalk and theatrical print processing.

### 2. User Interface & Controls
The interface is designed to emulate the physical workflow of analog photography.

#### A. Global Development
- **Exposure:** Models pushing or pulling the film development (+/- EV).
- **Highlights & Shadows:** Non-linear structural recovery applied *before* the film curve.
- **Blacks & Whites / Midtones & Contrast:** Tonal shaping controls.
- **Temperature & Tint:** Standard optical color balancing applied *before* the emulsion.
- **Film Color Compensation (Push/Pull):** Dictates how much of the film's native color response is applied. Can be pulled to 0 for a neutral grade or pushed up to 200 for exaggerated stock characteristics.

#### B. The Profile Block
- **Film Stock:** Selects the core camera negative behavior (e.g., Kodak Portra 400, Fuji Pro 400H, Tri-X). This dictates the base S-curves, dye contamination, and hue/saturation responses.
- **Print Finish:** Selects the theatrical positive print stock applied after the negative (e.g., Kodak Vision 2383, Fuji 3510). This provides the final grading anchor, mimicking the optical printing process.

#### C. Print Controls (Color Head)
- **CMY Sliders (Cyan, Magenta, Yellow):** Mimics the subtractive optical color heads found on darkroom enlargers. Pushing cyan subtracts red, pushing magenta subtracts green, etc.
- **Print Contrast:** Controls the steepness of the print stock's S-curve.
- **Black Point (Lift):** Specifically shifts the D-min (Minimum Density) of the print, controlling how deep or faded the absolute blacks appear.

#### D. Material Effects
- **Grain:** A robust Boolean math model generating overlapping silver halide clumps. Exposes sliders for Strength, Size, and Roughness.
- **Halation:** Simulates light bouncing off the pressure plate back through the red emulsion layer, causing red/orange blooming around bright highlights. Exposes sliders for Strength, Radius, and Threshold.
- **Bloom / Clarity / Dehaze / Sharpness:** Additional optical lens and atmospheric effects.

### 3. Included Emulations

#### Film Stocks (Camera Negatives)
- **Kodak Portra 160 / 400 / 800:** Famed portrait films with incredibly open shadows, massive latitude, and warm golden skin tones.
- **Kodak Ektar 100:** High saturation, contrasty, fine-grained landscape film.
- **Kodak Gold 200:** Consumer stock with punchy, vibrant midtones.
- **Fuji Pro 400H:** Known for a cool pastel aesthetic and extreme highlight latitude.
- **Fuji Superia 400:** Consumer stock with cool, teal/magenta characteristics.
- **CineStill 800T:** Tungsten-balanced cinema stock (Vision3 500T) known for its distinctive red halation due to the removed remjet layer.
- **Kodachrome 64:** Legendary, complex reversal film with vivid reds and crushing, sudden blacks.
- **Ilford HP5 Plus / Delta 3200 / Kodak Tri-X 400:** Classic monochrome stocks with varying degrees of punchy contrast and distinct grain structures.

#### Print Stocks (Positive Finish)
- **Kodak Vision 2383:** The industry-standard "Hollywood" print stock. Very deep blacks, gentle highlight rolloff, and a characteristic warm amber/golden tint.
- **Kodak Vision Premier 2393:** A high-impact print stock with "OLED-like" deep blacks and vivid saturation.
- **Fuji Eterna CP 3510:** Flatter, softer print stock with a signature cool/teal shadow bias.
- **Eastman 5381 (2302):** Vintage 1970s print stock with a faded, warm-orange base.

### 4. Workflow Output
- **Real-Time Preview:** A lightweight, cached low-res preview image is streamed to the frontend instantly as sliders are moved.
- **High-Resolution Export:** When the user clicks "Export Image", the exact identical processing pipeline is run on the full resolution RAW data, writing an uncompressed JPEG/TIFF to the `raw_files/` directory.
