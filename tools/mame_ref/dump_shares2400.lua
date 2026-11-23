local frame=0
local function log(s) emu.print_info(s) end
emu.register_frame_done(function()
 frame=frame+1
 if frame~=2400 then return end
 local tags={tileram=":tileram",textram=":textram",spriteram=":sprites",palette=":paletteram",workram=":workram"}
 for name,tag in pairs(tags) do
   local sh=manager.machine.memory.shares[tag]
   local f=io.open("build/mame_"..name..".bin","wb")
   local t={}
   for i=0,sh.size-1 do
     t[#t+1]=string.char(sh:read_u8(i))
     if #t>=4096 then f:write(table.concat(t)); t={} end
   end
   if #t>0 then f:write(table.concat(t)) end
   f:close()
   log("wrote "..name.." "..sh.size)
 end
end,"frame")
