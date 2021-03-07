HL2DIR="$HOME/.local/share/Steam/steamapps/common/Half-Life 2"
EP2DIR="$HL2DIR/ep2"

./build/desktop-rel-cc/OpenSource \
	-d "$EP2DIR" \
	-p "$EP2DIR/ep2_pak_dir.vpk" \
	-p "$HL2DIR/hl2/hl2_pak_dir.vpk" \
	-p "$HL2DIR/hl2/hl2_misc_dir.vpk" \
	-p "$HL2DIR/hl2/hl2_textures_dir.vpk" \
	-n 128 \
	ep2_outland_01
