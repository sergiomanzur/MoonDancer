local frame=0
local function log(s) emu.print_info(s) end
emu.register_frame_done(function()
 frame=frame+1
 if frame==1210 then
   local sh=manager.machine.memory.shares[":textram"]
   for _,a in ipairs({0xE80,0xE82,0xE84,0xE86,0xE90,0xE92,0xE98,0xE9A}) do
     log(string.format("TR %04X=%04X",a,sh:read_u16(a)))
   end
 end
end,"frame")
