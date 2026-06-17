# Startup Instructions - DFEE Web App

Follow these steps to run the interactive Deterministic Film Emulation Workspace on your Windows 11 machine.

---

## Prerequisites
Ensure you have the following installed:
1. **Python 3.10+** (with `pip`)
2. **Node.js 18+** (with `npm`)

---

## Setup & Run Instructions

### Step 1: Place RAW files
Copy your Sony `.ARW` raw photos into the `raw_files/` directory located at the root of the project:
`d:\Codebases\DFEE\raw_files\`

### Step 2: Install Dependencies (If not already done)
If starting on a fresh environment, run:
```powershell
# Install Python packages
pip install rawpy numpy scipy opencv-python PyYAML fastapi uvicorn pillow tifffile exifread pytest

# Install Frontend dependencies
cd frontend
npm install
npm run build
cd ..
```

### Step 3: Run the Application
You can run the application in two ways:

#### Option A: One-Click Startup (Recommended)
Double-click the **[start.bat](file:///d:/Codebases/DFEE/start.bat)** script in the root directory. 
This script will:
1. Start the FastAPI backend server on `localhost:8000`.
2. Automatically launch your default web browser and navigate to `http://127.0.0.1:8000`.

#### Option B: Terminal Command
Run the server using Python in your terminal:
```powershell
python server.py
```
Then manually open your browser to:
`http://127.0.0.1:8000`

To route `/api/profiles` through the native C++ engine while leaving the rest
of the backend on the Python pipeline, enable the feature flag before starting
the server:
```powershell
$env:DFEE_USE_NATIVE_PROFILES="1"
python server.py
```

---

## Using the Workspace Interface
1. **Load Image**: Click on any of the RAW files listed in the **Left Panel**. The server will take a second to load the image and calculate its exposure zone masks.
2. **Compare Slider**: Click and drag the vertical yellow divider bar in the **Center Workspace** left and right to inspect the details.
3. **Adjust Lightroom Controls**: Use the sliders in the **Right Panel** to adjust exposure offset, highlights/shadows recovery, white balance color casts, grain density, and halations.
4. **Export TIFF**: Once you like the look, click **Export 16-Bit TIFF**. The high-res output TIFF and dynamic JSON render report will be saved adjacent to your original raw file under `raw_files/`.
