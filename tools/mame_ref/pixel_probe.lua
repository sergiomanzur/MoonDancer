local frame=0
local function log(s) emu.print_info("PROBE "..s) end
local scr=manager.machine.screens[":screen"]
log("pixel method "..tostring(scr.pixel).." frame_number "..tostring(scr.frame_number))
emu.register_frame_done(function()
 frame=frame+1
 if frame==1200 or frame==1210 or frame==1211 then
   local t={}
   for _,pt in ipairs({{160,100},{50,50},{250,50},{50,180},{250,180},{160,200}}) do
     local ok,v=pcall(function() return scr:pixel(pt[1],pt[2]) end)
     t[#t+1]=string.format("%d,%d=%s",pt[1],pt[2],ok and tostring(v) or "ERR")
   end
   log("F"..frame.." "..table.concat(t," "))
 end
end,"frame")
