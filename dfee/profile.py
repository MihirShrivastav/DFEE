import os
import yaml

class FilmStockProfile:
    def __init__(self, filepath):
        self.filepath = filepath
        if not os.path.exists(filepath):
            raise FileNotFoundError(f"Film stock profile not found: {filepath}")
            
        with open(filepath, 'r', encoding='utf-8') as f:
            try:
                self.data = yaml.safe_load(f)
            except Exception as e:
                raise ValueError(f"Failed to parse YAML file {filepath}: {e}")
        
        self._validate()

    def _validate(self):
        required_keys = ['stock_id', 'stock_name', 'stock_type', 'adaptation', 
                         'tone_response', 'color_response', 'hue_saturation_response', 
                         'grain', 'halation']
        for key in required_keys:
            if key not in self.data:
                raise ValueError(f"Profile {self.filepath} is missing required section: '{key}'")
                
        # Validate stock_type
        if self.data['stock_type'] not in ['color_negative', 'color_reversal', 'monochrome']:
            raise ValueError(f"Invalid stock_type: {self.data['stock_type']}")

    @property
    def stock_id(self):
        return self.data['stock_id']

    @property
    def stock_name(self):
        return self.data['stock_name']

    @property
    def stock_type(self):
        return self.data['stock_type']

    @property
    def adaptation(self):
        return self.data['adaptation']

    @property
    def tone_response(self):
        return self.data['tone_response']

    @property
    def color_response(self):
        return self.data['color_response']

    @property
    def hue_saturation_response(self):
        return self.data['hue_saturation_response']

    @property
    def grain(self):
        return self.data['grain']

    @property
    def halation(self):
        return self.data.get('halation', {})

    @property
    def chroma_coupling(self):
        """Luminance-dependent chroma compression parameters (optional, has defaults)."""
        return self.data.get('chroma_coupling', {})

    @property
    def dye_contamination(self):
        """Cross-channel dye contamination matrix (optional, has defaults)."""
        return self.data.get('dye_contamination', {})




class PrintStockProfile:
    """
    Profiles for theatrical print film stocks (Kodak 2383, 2393, Fuji 3510, Eastman 2302).
    These are applied as a final stage AFTER the camera negative emulation — mirroring the
    real photochemical pipeline where a negative is contact-printed onto a separate positive
    print stock for projection.
    """
    def __init__(self, filepath):
        self.filepath = filepath
        if not os.path.exists(filepath):
            raise FileNotFoundError(f"Print stock profile not found: {filepath}")
        with open(filepath, 'r', encoding='utf-8') as f:
            try:
                self.data = yaml.safe_load(f)
            except Exception as e:
                raise ValueError(f"Failed to parse YAML file {filepath}: {e}")
        self._validate()

    def _validate(self):
        required = ['print_stock_id', 'print_stock_name', 'tone', 'color', 'grain']
        for key in required:
            if key not in self.data:
                raise ValueError(f"Print stock profile {self.filepath} missing section: '{key}'")

    @property
    def print_stock_id(self):
        return self.data['print_stock_id']

    @property
    def print_stock_name(self):
        return self.data['print_stock_name']

    @property
    def tone(self):
        return self.data['tone']

    @property
    def color(self):
        return self.data['color']

    @property
    def grain(self):
        return self.data['grain']
