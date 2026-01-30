from pathlib import Path, PurePath
from nitrogfx.convert import ncgr_to_img, ncer_to_img, ncer_to_json
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
		
		CLOSER_INSPECTION = [ 'if0391nor00', 'if0401nor00', 'ifbai1nor04', 'ifbai1nor05', 'wital1eve00', 'wital1eve01', 'wital1eve02', 'wital1eve03' ]
		
		for file in filenames:
			if file not in CLOSER_INSPECTION: continue
			
			nclr = NCLR.unpack(zip.read(f"{file}.nclr")) # Palette
			ncbr = NCGR.unpack(zip.read(f"{file}.ncbr")) # Pixels
			ncer = NCER.unpack(zip.read(f"{file}.ncer")) # Arrangement
			
			nclr.trans = 0
			
			ncer_to_json(ncer, ".temp.json")
			dest_zip.write(".temp.json", f"{file}.json")
			
			for index, cell in enumerate(ncer.cells):
				png_name = f"{file}_{index}.png" if len(ncer.cells) > 1 else f"{file}.png"
				with dest_zip.open(png_name, 'w') as dest_f:
					ncer_to_img(cell, ncbr, nclr).save(dest_f, "PNG")
				