local frame=0
local function log(s) emu.print_info(s) end
local function svc() return manager.machine.ioport.ports[":SERVICE"] end
emu.register_frame_done(function()
 frame=frame+1
 local v=nil
 if frame>=800 and frame<900 then v=0
 elseif frame>=900 and frame<1000 then v=1
 elseif frame>=1000 and frame<1100 then v=0
 elseif frame>=1100 and frame<1200 then v=1 end
 if v~=nil then
   pcall(function() svc().fields["Coin 1"]:set_value(v) end)
   pcall(function() svc().fields["Coin 3"]:set_value(v) end)
 end
 if frame%50==0 and frame>=800 and frame<=1300 then
   local sh=manager.machine.memory.shares[":workram"]
   log(string.format("frame %d v=%s credits %02X %02X %02X",frame,tostring(v),sh:read_u8(0x2000),sh:read_u8(0x2002),sh:read_u8(0x2004)))
 end
end,"frame")
