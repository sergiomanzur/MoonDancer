local frame=0
local function log(s) emu.print_info(s) end
emu.register_frame_done(function()
 frame=frame+1
 if frame~=120 then return end
 for _,tag in ipairs({":tileram",":textram",":sprites",":workram"}) do
   local sh=manager.machine.memory.shares[tag]
   if sh then
     local t={}
     for i=0,31 do t[#t+1]=string.format("%04X", sh:read_u16(i*2)) end
     log(tag.." +0: "..table.concat(t," "))
     t={}
     for i=0,31 do t[#t+1]=string.format("%04X", sh:read_u16(0x10000+i*2)) end
     log(tag.." +10000: "..table.concat(t," "))
   end
 end
end,"frame")
