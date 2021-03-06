BASEDIR="$HOME/.local/share/Steam/steamapps/common/Portal"
HL2DIR="$BASEDIR/hl2"
PDIR="$BASEDIR/portal"

./build/desktop-rel-cc/OpenSource \
	-d "$PDIR" \
	-p "$PDIR/portal_pak_dir.vpk" \
	-p "$HL2DIR/hl2_misc_dir.vpk" \
	-p "$HL2DIR/hl2_textures_dir.vpk" \
	-n 128 \
	testchmb_a_00
