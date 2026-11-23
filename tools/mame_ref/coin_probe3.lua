local frame=0
local function log(s) emu.print_info(s) end
local function svc() return manager.machine.ioport.ports[":SERVICE"] end
emu.register_frame_done(function()
 frame=frame+1
 if frame==900 then
   local f=svc().fields["Coin 1"]
   log("coin value before "..tostring(f.value))
   local ok,err=pcall(function() f.value=0 end)
   log("assign coin .value=0 ok="..tostring(ok).." err="..tostring(err).." after="..tostring(f.value))
   local ok2,err2=pcall(function() f:set_value(0) end)
   log("set_value coin ok="..tostring(ok2).." err="..tostring(err2).." after="..tostring(f.value))
 end
 if frame>=900 and frame<940 then
   pcall(function() svc().fields["Coin 1"].value=0 end)
   pcall(function() svc().fields["1 Player Start"].value=0 end)
 end
 if frame%60==0 and frame>=900 and frame<=1300 then
   local sh=manager.machine.memory.shares[":workram"]
   log(string.format("frame %d credits %02X %02X %02X",frame,sh:read_u8(0x2000),sh:read_u8(0x2002),sh:read_u8(0x2004)))
 end
end,"frame")
