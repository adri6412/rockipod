from PIL import Image
import os

source_path = r"C:\Users\adri6\Pictures\ipod.jpg"
target_path = r"apps/bitmaps/native/rockboxlogo.240x320x16.bmp"

def convert_logo():
    print(f"Opening {source_path}...")
    try:
        img = Image.open(source_path)
    except Exception as e:
        print(f"Error opening image: {e}")
        return

    print(f"Original size: {img.size}")
    
    # Resize to 240x320 (Ignore aspect ratio to fill screen, or crop?)
    # User likely wants full screen.
    target_size = (240, 320)
    
    # High quality resize
    img = img.resize(target_size, Image.Resampling.LANCZOS)
    
    # Save as BMP
    print(f"Saving to {target_path}...")
    img.save(target_path, "BMP")
    print("Done.")

if __name__ == "__main__":
    convert_logo()
