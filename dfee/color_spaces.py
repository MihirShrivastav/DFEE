import numpy as np

def rgb_to_oklab(rgb):
    """
    Converts linear RGB (range [0, 1]) to OKLab.
    Input array shape: (H, W, 3)
    Output array shape: (H, W, 3) (L, a, b)
    """
    # Clamp input to avoid negative / zero values before cube-root — but do NOT
    # clip the upper end. Exposure-boosted pixels can exceed 1.0 and should reach
    # the film S-curve shoulder for graceful roll-off, not be digitally clipped here.
    rgb = np.clip(rgb, 1e-12, None)
    
    # 1. Linear RGB to LMS
    # Matrix shape: (3, 3)
    m1 = np.array([
        [0.4122214708, 0.5363325363, 0.0514459929],
        [0.2119034982, 0.6806995451, 0.1073969566],
        [0.0883024619, 0.2817188376, 0.6299787005]
    ], dtype=np.float32)
    
    # Reshape image for matrix multiplication
    h, w, c = rgb.shape
    flat_rgb = rgb.reshape(-1, 3).T
    flat_lms = np.dot(m1, flat_rgb)
    
    # 2. Non-linear LMS (cube root)
    flat_lms_prime = np.cbrt(flat_lms)
    
    # 3. LMS' to OKLab
    m2 = np.array([
        [0.2104542553, 0.7936177850, -0.0040720468],
        [1.9779984951, -2.4285922050, 0.4505937099],
        [0.0259040371, 0.7827717612, -0.8086757983]
    ], dtype=np.float32)
    
    flat_oklab = np.dot(m2, flat_lms_prime)
    
    return flat_oklab.T.reshape(h, w, 3)


def oklab_to_rgb(oklab):
    """
    Converts OKLab (L, a, b) to linear RGB.
    Input array shape: (H, W, 3)
    Output array shape: (H, W, 3)
    """
    h, w, c = oklab.shape
    flat_oklab = oklab.reshape(-1, 3).T
    
    # 1. OKLab to LMS'
    m2_inv = np.array([
        [1.0, 0.3963377774, 0.2158017574],
        [1.0, -0.1055613458, -0.0638541728],
        [1.0, -0.0894841775, -1.2914855480]
    ], dtype=np.float32)
    
    flat_lms_prime = np.dot(m2_inv, flat_oklab)
    
    # 2. LMS' to LMS (cube)
    flat_lms = flat_lms_prime ** 3
    
    # 3. LMS to linear RGB
    m1_inv = np.array([
        [4.0767416621, -3.3077115913, 0.2309699292],
        [-1.2684380046, 2.6097574011, -0.3413193965],
        [-0.0041960863, -0.7034186147, 1.7076147010]
    ], dtype=np.float32)
    
    flat_rgb = np.dot(m1_inv, flat_lms)
    
    # Reshape and clip to linear RGB boundaries
    rgb = flat_rgb.T.reshape(h, w, 3)
    return np.clip(rgb, 0.0, 1.0)


def oklab_to_oklch(oklab):
    """
    Converts OKLab to OKLCH.
    Output: Lightness L, Chroma C, Hue H (in radians [0, 2*pi])
    """
    lch = np.zeros_like(oklab)
    lch[:, :, 0] = oklab[:, :, 0] # Lightness L
    
    a = oklab[:, :, 1]
    b = oklab[:, :, 2]
    
    # Chroma C
    lch[:, :, 1] = np.sqrt(a**2 + b**2)
    
    # Hue H in radians
    h_rad = np.arctan2(b, a)
    lch[:, :, 2] = np.where(h_rad < 0, h_rad + 2 * np.pi, h_rad)
    
    return lch


def oklch_to_oklab(oklch):
    """
    Converts OKLCH to OKLab.
    """
    oklab = np.zeros_like(oklch)
    oklab[:, :, 0] = oklch[:, :, 0] # Lightness L
    
    c = oklch[:, :, 1]
    h = oklch[:, :, 2]
    
    oklab[:, :, 1] = c * np.cos(h)
    oklab[:, :, 2] = c * np.sin(h)
    
    return oklab
