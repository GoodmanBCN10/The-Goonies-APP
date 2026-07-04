import struct
import sys

def get_image_info(filepath):
    try:
        with open(filepath, 'rb') as f:
            data = f.read()
            if data[:2] == b'\xff\xd8':
                # JPEG
                i = 2
                while i < len(data):
                    marker, length = struct.unpack('>HH', data[i:i+4])
                    if 0xFFC0 <= marker <= 0xFFC3:
                        height, width = struct.unpack('>HH', data[i+5:i+9])
                        print(f"JPEG Width: {width}, Height: {height}")
                        return
                    i += length + 2
            elif data[:8] == b'\x89PNG\r\n\x1a\n':
                # PNG
                width, height = struct.unpack('>II', data[16:24])
                print(f"PNG Width: {width}, Height: {height}")
                return
    except Exception as e:
        print(e)

get_image_info('assets/icon.jpg')
