local frame=0
local function log(s) emu.print_info(s) end
local function svc() return manager.machine.ioport.ports[":SERVICE"] end
emu.register_frame_done(function()
 frame=frame+1
 local v=nil
 if frame>=900 and frame<920 then v=0
 elseif frame>=920 and frame<940 then v=1
 elseif frame>=940 and frame<960 then v=255 end
 if v~=nil then pcall(function() svc().fields["Coin 1"]:set_value(v) end) end
 if frame>=900 and frame<965 and frame%5==0 then
   local ok,rv=pcall(function() return svc():read() end)
   log(string.format("frame %d set=%s read=%s",frame,tostring(v),tostring(rv)))
 end
end,"frame")
