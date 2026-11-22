#!/usr/bin/env python3
"""Build a small, separately downloadable optional service setup PKG."""
from pathlib import Path
import shutil, os
import package as builder
ROOT=Path(__file__).resolve().parents[1]
stage=ROOT/'build/service-installer';shutil.rmtree(stage,ignore_errors=True);stage.mkdir(parents=True)
for name in ('assets/worker','sce_module','sce_sys'):
    shutil.copytree(ROOT/name,stage/name,dirs_exist_ok=True,ignore=shutil.ignore_patterns('._*','param.sfo','COMPAT-PROVENANCE.txt'))
for name in ('font.ttf','FONT-LICENSE.txt','font-fallback.ttf','LICENSE-font-fallback.txt','brand/nullstore-bag.png','service/cacert.pem','service/TLS-LICENSE.txt','service/TLS-PROVENANCE.txt'):
    target=stage/'assets'/name
    target.parent.mkdir(parents=True,exist_ok=True)
    shutil.copyfile(ROOT/'assets'/name,target)
shutil.copyfile(ROOT/'build/service-installer.bin',stage/'eboot.bin')
shutil.copyfile(ROOT/'assets/brand/service-icon.png',stage/'sce_sys/icon0.png')
builder.ROOT=stage;builder.CID='IV0000-NULL00002_00-NULLSTORSERVICE0';builder.sfo(title='NULL_store Background Service',title_id='NULL00002');builder.gp4()
# The Makefile passes the packager of the host platform.
tool=os.environ.get('PKGTOOL') or Path(os.environ.get('OO_PS4_TOOLCHAIN', ROOT.parent/'toolchain/OpenOrbis/PS4Toolchain'))/'bin/macos/PkgTool.Core'
builder.build_package(stage/'pkg.gp4',ROOT/'build',tool)
