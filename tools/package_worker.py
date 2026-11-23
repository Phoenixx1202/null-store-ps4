"""Build the download worker as an independently installable package."""
import os
import shutil
import tempfile
from pathlib import Path

import package as builder

root = builder.ROOT
tool = Path(os.environ['PKGTOOL']).resolve()
with tempfile.TemporaryDirectory(dir=root / 'build') as scratch:
    stage = Path(scratch)
    for source, destination in [
        ('build/packaged-worker.bin', 'eboot.bin'),
        ('LICENSE', 'assets/LICENSE'),
        ('assets/brand/service-icon.png', 'sce_sys/icon0.png'),
        ('sce_module/libc.prx', 'sce_module/libc.prx'),
        ('sce_module/libSceFios2.prx', 'sce_module/libSceFios2.prx'),
        ('assets/service/build-id.txt', 'assets/service/build-id.txt'),
        ('assets/service/cacert.pem', 'assets/service/cacert.pem'),
        ('assets/service/TLS-LICENSE.txt', 'assets/service/TLS-LICENSE.txt'),
        ('assets/service/TLS-PROVENANCE.txt', 'assets/service/TLS-PROVENANCE.txt'),
    ]:
        target = stage / destination
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(root / source, target)
    builder.ROOT = stage
    builder.CID = 'IV0000-NULL00003_00-NULLSTOREWORKER0'
    builder.sfo(title='NULL_store Download Worker (manage in Downloads)', title_id='NULL00003', category='gdd')
    builder.gp4()
    builder.build_package(stage / 'pkg.gp4', root / 'build', tool)
