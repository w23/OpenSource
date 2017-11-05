HL2DIR="$HOME/.local/share/Steam/steamapps/common/Half-Life 2"
HL1DIR="$HL2DIR/hl1"

./build/desktop-rel-cc/OpenSource \
	-d "$HL1DIR" \
	-p "$HL1DIR"_hd/hl1_hd_pak_dir.vpk \
	-p "$HL1DIR/hl1_pak_dir.vpk" \
	-p "$HL2DIR/hl2/hl2_misc_dir.vpk" \
	-p "$HL2DIR/hl2/hl2_textures_dir.vpk" \
	-n 128 \
	c0a0a
