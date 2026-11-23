local frame=0
local function log(s) emu.print_info(s) end
emu.register_frame_done(function()
 frame=frame+1
 if frame==947 then
   local sh=manager.machine.memory.shares[":workram"]
   log(string.format("NOIN E0F4 %02X%02X",sh:read_u8(0x20F4),sh:read_u8(0x20F5)))
 end
end,"frame")
