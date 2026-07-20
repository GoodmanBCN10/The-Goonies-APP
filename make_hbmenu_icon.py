import sys
from PIL import Image

input_path = "C:/Users/josel/.gemini/antigravity/brain/2c1c6a08-6435-4752-bfbc-bd19c9dacce7/media__1783455152344.jpg"
output_path = "C:/Users/josel/Downloads/Antigravity/TheGooniesInstaller/icon.jpg"

try:
    img = Image.open(input_path)
    img = img.resize((256, 256), Image.Resampling.LANCZOS)
    img = img.convert('RGB')
    img.save(output_path, 'JPEG', quality=95)
    print("Icon successfully created at", output_path)
except Exception as e:
    print("Error:", e)
