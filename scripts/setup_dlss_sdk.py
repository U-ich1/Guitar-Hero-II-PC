"""Optional pinned NVIDIA development SDK; no game or configuration changes."""
from pathlib import Path
import argparse,hashlib,shutil,sys,urllib.request,zipfile
from build_project import safe_extract
root=Path(__file__).resolve().parents[1]
URL='https://github.com/NVIDIA-RTX/Streamline/releases/download/v2.14.1/streamline-sdk-v2.14.1.zip'
SHA256='92c4d954631a1710da86ca3fa8d5034f2b9503838c95fc4ae977ae149319781b'
parser=argparse.ArgumentParser();parser.add_argument('--offline',action='store_true');args=parser.parse_args()
archive=root/'tools/downloads/streamline-sdk-v2.14.1.zip';archive.parent.mkdir(parents=True,exist_ok=True)
if not archive.exists():
    if args.offline:raise RuntimeError('Official SDK archive has not been downloaded')
    partial=archive.with_suffix('.partial')
    with urllib.request.urlopen(URL,timeout=120) as response,partial.open('wb') as output:shutil.copyfileobj(response,output)
    if hashlib.sha256(partial.read_bytes()).hexdigest()!=SHA256:raise RuntimeError('Official SDK checksum mismatch')
    partial.replace(archive)
if hashlib.sha256(archive.read_bytes()).hexdigest()!=SHA256:raise RuntimeError('SDK archive checksum mismatch')
destination=root/'tools/streamline-sdk-v2.14.1'
if not destination.exists():safe_extract(archive,destination)
print(destination)
print('Official SDK 2.14.1 downloaded and SHA256 verified; no DLLs installed into the game.')
