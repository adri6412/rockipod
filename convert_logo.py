from PIL import Image, ImageOps

def convert_logo():
    try:
        # Load the uploaded image
        input_path = "apple_logo.png"
        output_path = "apps/bitmaps/native/rockboxlogo.320x320x16.bmp"
        
        # Target dimensions
        target_width = 320
        target_height = 320
        
        # Open source image
        img = Image.open(input_path)
        
        # Convert to RGB (in case of RGBA)
        if img.mode == 'RGBA':
            background = Image.new("RGB", img.size, (0, 0, 0))
            background.paste(img, mask=img.split()[3])
            img = background
        else:
             img = img.convert("RGB")

        # Calculate aspect ratio preserving resize
        img.thumbnail((target_width, target_height), Image.Resampling.LANCZOS)
        
        # Create a black canvs
        new_img = Image.new("RGB", (target_width, target_height), (0, 0, 0))
        
        # Center the image
        left = (target_width - img.width) // 2
        top = (target_height - img.height) // 2
        new_img.paste(img, (left, top))
        
        # Save as BMP (24-bit RGB) - Rockbox often expects 24-bit bitmaps which it converts if needed, 
        # or specific bit depths. The filename implies 16-bit color depth but standard PIL saves as 24-bit.
        # Let's save as standard BMP first. Rockbox build tools usually handle conversion or standard BMPs.
        new_img.save(output_path)
        print(f"Successfully converted {input_path} to {output_path}")
        
    except Exception as e:
        print(f"Error: {e}")

if __name__ == "__main__":
    convert_logo()
