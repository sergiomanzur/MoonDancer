local frame=0
local function log(s) emu.print_info(s) end
emu.register_frame_done(function()
 frame=frame+1
 if frame~=120 then return end
 for _,tag in ipairs({":tileram",":textram"}) do
   local sh=manager.machine.memory.shares[tag]
   local nz=0; local first=nil
   for i=0,sh.size-2,2 do
     local v=sh:read_u16(i)
     if v~=0 then nz=nz+1; if not first then first=i end end
   end
   log(string.format("%s size=%d nonzero_words=%d first=%s",tag,sh.size,nz,first and string.format("%X",first) or "none"))
   if first then
     local t={}; for i=0,15 do t[#t+1]=string.format("%04X",sh:read_u16(first+i*2)) end
     log("  at "..string.format("%X",first)..": "..table.concat(t," "))
   end
 end
end,"frame")
