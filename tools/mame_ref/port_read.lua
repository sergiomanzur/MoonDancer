local frame=0
local function log(s) emu.print_info(s) end
local function svc() return manager.machine.ioport.ports[":SERVICE"] end
emu.register_frame_done(function()
 frame=frame+1
 if frame==100 then
   local ok,v=pcall(function() return svc():read() end)
   log("port read 100 ok="..tostring(ok).." v="..tostring(v))
 end
 if frame>=900 and frame<910 then
   pcall(function() svc().fields["Coin 1"]:set_value(0) end)
 end
 if frame>=900 and frame<915 then
   local ok,v=pcall(function() return svc():read() end)
   log("port read "..frame.." ok="..tostring(ok).." v="..tostring(v))
 end
end,"frame")
