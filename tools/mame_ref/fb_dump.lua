local frame=0
local function log(s) emu.print_info("PROBE "..s) end
local scr=manager.machine.screens[":screen"]
emu.register_frame_done(function()
 frame=frame+1
 if frame==1210 then
   local f=io.open("build/mame_fb1210.bin","wb")
   for y=0,223 do
     for x=0,319 do
       local v=scr:pixel(x,y) & 0xFFFFFFFF
       f:write(string.char(v & 0xff, (v>>8)&0xff, (v>>16)&0xff, (v>>24)&0xff))
     end
   end
   f:close(); log("fb1210 written")
 end
end,"frame")
