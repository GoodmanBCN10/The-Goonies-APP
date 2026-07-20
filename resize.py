from PIL import Image
import os

img = Image.open('icon.jpg')
img = img.resize((256, 256), Image.LANCZOS)
img.save('icon.jpg', format='JPEG')
img.save('romfs/logo.png', format='PNG')
print("Resized icon.jpg and saved to romfs/logo.png")
