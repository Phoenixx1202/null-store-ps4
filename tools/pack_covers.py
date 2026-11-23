#!/usr/bin/env python3
"""Build the cover archive from the bundled thumbnails and optional local JPEGs."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
assets = root / 'assets'
bundled = root / 'resources' / 'catalog'
extras = {int(path.stem): path for path in (assets / 'covers').glob('*.jpg') if path.stem.isdigit()}
rows = []
with (bundled / 'covers.pack').open('rb') as original, (assets / 'covers.pack.tmp').open('wb') as output:
    def append(game, data):
        if not data:
            raise ValueError(f'Empty cover for game {game}')
        rows.append(f'{game}\t{output.tell()}\t{len(data)}\n')
        output.write(data)

    for line in (bundled / 'covers.idx').read_text().splitlines():
        game, offset, length = map(int, line.split())
        if game in extras:
            append(game, extras.pop(game).read_bytes())
        else:
            original.seek(offset)
            data = original.read(length)
            if len(data) != length:
                raise ValueError(f'Truncated bundled cover for game {game}')
            append(game, data)
    for game, path in sorted(extras.items()):
        append(game, path.read_bytes())
(assets / 'covers.idx.tmp').write_text(''.join(rows))
(assets / 'covers.pack.tmp').replace(assets / 'covers.pack')
(assets / 'covers.idx.tmp').replace(assets / 'covers.idx')
print(f'Packed {len(rows)} covers.')
