local frame=0
local function log(s) emu.print_info(s) end
emu.register_frame_done(function()
 frame=frame+1
 if frame~=120 then return end
 for _,tag in ipairs({":sprites",":tileram",":textram",":workram",":paletteram"}) do
   local sh=manager.machine.memory.shares[tag]
   local t={}; for i=0,15 do t[#t+1]=string.format("%04X",sh:read_u16(0x20+i*2)) end
   log(tag.." +20: "..table.concat(t," "))
 end
end,"frame")
