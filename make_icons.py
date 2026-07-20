import os
from PIL import Image, ImageDraw, ImageFont

def create_icon(filename, draw_func):
    img = Image.new('RGBA', (64, 64), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    draw_func(draw)
    img.save(filename)

def draw_mtp(draw):
    # USB icon: simple plug shape
    draw.rectangle([24, 40, 40, 60], fill=(240, 240, 240, 255))
    draw.rectangle([20, 20, 44, 40], fill=(240, 240, 240, 255))
    draw.rectangle([24, 10, 28, 20], fill=(240, 240, 240, 255))
    draw.rectangle([36, 10, 40, 20], fill=(240, 240, 240, 255))
    # wire
    draw.line([32, 60, 32, 64], fill=(240, 240, 240, 255), width=4)

def draw_games(draw):
    # Gamepad icon
    draw.rounded_rectangle([8, 16, 56, 48], radius=16, fill=(240, 240, 240, 255))
    # D-pad
    draw.rectangle([18, 28, 22, 36], fill=(40, 40, 40, 255))
    draw.rectangle([14, 30, 26, 34], fill=(40, 40, 40, 255))
    # Buttons
    draw.ellipse([42, 24, 48, 30], fill=(40, 40, 40, 255))
    draw.ellipse([36, 32, 42, 38], fill=(40, 40, 40, 255))

def draw_saves(draw):
    # Floppy disk / save icon
    draw.rounded_rectangle([12, 12, 52, 52], radius=4, fill=(240, 240, 240, 255))
    draw.rectangle([20, 12, 44, 28], fill=(40, 40, 40, 255))
    draw.rectangle([18, 36, 46, 52], fill=(40, 40, 40, 255))
    draw.rectangle([34, 16, 40, 24], fill=(240, 240, 240, 255))

def draw_explorer(draw):
    # Folder icon
    draw.rounded_rectangle([8, 20, 56, 52], radius=4, fill=(240, 240, 240, 255))
    draw.polygon([(8, 20), (8, 16), (24, 16), (30, 20)], fill=(240, 240, 240, 255))

def draw_settings(draw):
    # Gear icon
    draw.ellipse([16, 16, 48, 48], outline=(240, 240, 240, 255), width=6)
    draw.ellipse([26, 26, 38, 38], fill=(240, 240, 240, 255))
    # Teeth
    draw.line([32, 8, 32, 16], fill=(240, 240, 240, 255), width=8)
    draw.line([32, 48, 32, 56], fill=(240, 240, 240, 255), width=8)
    draw.line([8, 32, 16, 32], fill=(240, 240, 240, 255), width=8)
    draw.line([48, 32, 56, 32], fill=(240, 240, 240, 255), width=8)
    draw.line([16, 16, 22, 22], fill=(240, 240, 240, 255), width=6)
    draw.line([42, 42, 48, 48], fill=(240, 240, 240, 255), width=6)
    draw.line([16, 48, 22, 42], fill=(240, 240, 240, 255), width=6)
    draw.line([42, 22, 48, 16], fill=(240, 240, 240, 255), width=6)

romfs_path = "C:/Users/josel/Downloads/Antigravity/TheGooniesInstaller/romfs"
os.makedirs(romfs_path, exist_ok=True)

create_icon(os.path.join(romfs_path, "icon_mtp.png"), draw_mtp)
create_icon(os.path.join(romfs_path, "icon_games.png"), draw_games)
create_icon(os.path.join(romfs_path, "icon_saves.png"), draw_saves)
create_icon(os.path.join(romfs_path, "icon_explorer.png"), draw_explorer)
create_icon(os.path.join(romfs_path, "icon_settings.png"), draw_settings)

print("Icons created successfully.")
