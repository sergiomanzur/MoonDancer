local frame=0
local function log(s) emu.print_info(s) end
emu.register_frame_done(function()
 frame=frame+1
 if frame~=1200 then return end
 local sh=manager.machine.memory.shares[":workram"]
 log(string.format("workram size %d", sh.size))
 local nz=0; local first={}
 for i=0,sh.size-1 do
   local v=sh:read_u8(i)
   if v~=0 then nz=nz+1; if #first<12 then first[#first+1]=string.format("%04X=%02X",i,v) end end
 end
 log("nonzero "..nz.." first "..table.concat(first," "))
 for _,off in ipairs({0x2000,0x2002,0x2004,0x0000,0x0002,0x0004,0x3EFC}) do
   log(string.format("off %04X = %02X", off, sh:read_u8(off)))
 end
end,"frame")
