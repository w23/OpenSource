HL2DIR="$HOME/.local/share/Steam/steamapps/common/Half-Life 2"
EPDIR="$HL2DIR/episodic"

./build/desktop-rel-cc/OpenSource \
	-d "$EPDIR" \
	-p "$EPDIR/ep1_pak_dir.vpk" \
	-p "$HL2DIR/hl2/hl2_pak_dir.vpk" \
	-p "$HL2DIR/hl2/hl2_misc_dir.vpk" \
	-p "$HL2DIR/hl2/hl2_textures_dir.vpk" \
	-n 128 \
	ep1_c17_00
