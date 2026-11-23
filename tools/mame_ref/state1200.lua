local frame=0
local function log(s) emu.print_info(s) end
emu.register_frame_done(function()
 frame=frame+1
 if frame==1200 then
   local sh=manager.machine.memory.shares[":workram"]
   log(string.format("E036=%02X E000=%02X E02A=%02X E02B=%02X",sh:read_u8(0x2036),sh:read_u8(0x2000),sh:read_u8(0x202A),sh:read_u8(0x202B)))
 end
end,"frame")
