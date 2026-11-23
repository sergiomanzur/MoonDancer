local frame=0
local function log(s) emu.print_info(s) end
emu.register_frame_done(function()
 frame=frame+1
 if frame==1200 then
   local ok,err=pcall(function() manager.machine:save("mame1200.sta") end)
   log("save ok="..tostring(ok).." err="..tostring(err))
 end
end,"frame")
