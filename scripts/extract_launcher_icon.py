"""Wrap GH2's original XDBF title icon as a Windows ICO, without altering it."""
from pathlib import Path
import struct

root=Path(__file__).resolve().parents[1]
image=(root/'private/default.exe').read_bytes()
start=image.find(b'XDBF')
if start<0:raise RuntimeError('Game metadata not found')
magic,version,capacity,count,free_capacity,free_count=struct.unpack_from('>6I',image,start)
data=start+24+capacity*18+free_capacity*8
for index in range(count):
    kind,identifier,offset,size=struct.unpack_from('>HQII',image,start+24+index*18)
    if kind==2 and identifier==0x8000:
        png=image[data+offset:data+offset+size]
        break
else:raise RuntimeError('Original GH2 title icon not found')
if not png.startswith(b'\x89PNG\r\n\x1a\n'):raise RuntimeError('Title icon is not a PNG')
width,height=struct.unpack_from('>II',png,16)
if not(0<width<=256 and 0<height<=256):raise RuntimeError('Unsupported title icon dimensions')
assets=root/'launcher/assets';assets.mkdir(parents=True,exist_ok=True)
ico=struct.pack('<HHH',0,1,1)+struct.pack('<BBBBHHII',width%256,height%256,0,0,1,32,len(png),22)+png
(assets/'gh2.ico').write_bytes(ico)
(root/'build/gh2-title-icon.png').write_bytes(png)
print(f'Extracted original GH2 title icon: {width}x{height}, XDBF image 0x8000. PNG bytes preserved.')
