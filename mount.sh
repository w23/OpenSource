#!/bin/sh

VPKDIR=~/.local/share/Steam/steamapps/common/Half-Life\ 2/
VPK_FUSE=../vpk_fuse/vpk_fuse

vpk_mnt() {
	mkdir -p mnt/"$2"
	$VPK_FUSE "$VPKDIR/$1" mnt/"$2"
}

vpk_mnt hl1/hl1_pak_dir.vpk hl1_pak
vpk_mnt hl1_hd/hl1_hd_pak_dir.vpk hl1_hd_pak
vpk_mnt hl2/hl2_pak_dir.vpk hl2_pak
vpk_mnt hl2/hl2_misc_dir.vpk hl2_misc
vpk_mnt hl2/hl2_textures_dir.vpk hl2_textures
