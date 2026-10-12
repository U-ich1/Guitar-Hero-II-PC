"""Export original venue-card textures from the supported local GH2 dump."""
from pathlib import Path
import struct,hashlib,json
from PIL import Image
from prepare_discord_presence import entries

def extract(root):
    index=entries(root);output=root/'build/discord-assets';output.mkdir(parents=True,exist_ok=True)
    sources={'battle':'battle','small1':'small1','small2':'small2','big':'bigclub','theatre':'theater','fest':'fest','arena':'arena','stone':'stonehenge'}
    report=[]
    with (root/'private/gen/main_0.ark').open('rb') as archive:
        for venue,texture in sources.items():
            path=f'ui/image/ng/gen/venue_{texture}.png_xbox'.encode()
            _,offset,size=index[path];archive.seek(offset);source=archive.read(size)
            # Supported RndBitmap: version 1, 8-bit block data, Xbox encoding
            # 24, five additional mip levels, 512-square base image, 32-byte header.
            if size!=349472 or source[:11]!=bytes.fromhex('0108180000000500020002'):
                raise ValueError('Unsupported venue texture: '+path.decode())
            blocks=bytearray(source[32:32+512*512])
            for i in range(0,len(blocks),2):blocks[i],blocks[i+1]=blocks[i+1],blocks[i]
            image=Image.frombytes('RGBA',(512,512),bytes(blocks),'bcn',(3,'DXT5'))
            filename=f'venue_{venue}.png';image.save(output/filename)
            # Round-trip the exported base image; preserve original dimensions.
            with Image.open(output/filename) as check:
                if check.size!=(512,512) or check.tobytes()!=image.tobytes():raise ValueError('PNG round trip failed')
            report.append(dict(asset=filename,source=path.decode(),sha256=hashlib.sha256(source).hexdigest()))
    # Original title icon is already a PNG; extract the ICO's preserved payload.
    ico=(root/'launcher/assets/gh2.ico').read_bytes();length,offset=struct.unpack_from('<II',ico,14)
    icon=ico[offset:offset+length]
    if not icon.startswith(b'\x89PNG\r\n\x1a\n'):raise ValueError('Unexpected icon payload')
    (output/'gh2_logo.png').write_bytes(icon)
    (output/'UPLOAD.txt').write_text('Upload these PNGs to your Discord application 1559050915195912242 > Rich Presence > Art Assets.\nUse each filename without .png as the asset key.\nVenue cards switch automatically during gameplay, pause and results. gh2_logo is used in menus.\n',encoding='utf-8')
    (root/'docs/discord_venue_assets_validation.json').write_text(json.dumps(dict(result='pass',assets=report,scope='Original venue textures decoded and PNG-round-tripped; visually inspect exports. No upload performed.'),indent=2))
    print(output)
    return output

if __name__=='__main__':extract(Path(__file__).resolve().parents[1])
