from pathlib import Path, PurePath
from nitrogfx.convert import ncer_to_img
from nitrogfx.nclr import NCLR
from nitrogfx.ncgr import NCGR
from nitrogfx.ncer import NCER
from zipfile import ZipFile

source_archive = Path(__file__).parent / 'AllThePortraits.zip'
dest_archive = source_archive.parent / 'AllThePNGPortraits.zip'
assert source_archive.is_file()
with ZipFile(dest_archive, 'w') as dest_zip:
	with ZipFile(source_archive, 'r') as zip:
		filenames: set[str] = set()
		for file in zip.filelist:
			filenames.add(PurePath(file.filename).stem)
		
		for file in filenames:
			nclr = NCLR.unpack(zip.read(f"{file}.nclr")) # Palette
			ncbr = NCGR.unpack(zip.read(f"{file}.ncbr")) # Pixels
			ncer = NCER.unpack(zip.read(f"{file}.ncer")) # Arrangement
			
			nclr.trans = 0
			
			for index, cell in enumerate(ncer.cells):
				png_name = f"{file}_{index}.png" if len(ncer.cells) > 1 else f"{file}.png"
				with dest_zip.open(png_name, 'w') as dest_f:
					ncer_to_img(cell, ncbr, nclr).save(dest_f, "PNG")