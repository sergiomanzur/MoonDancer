local frame=0
local function log(s) emu.print_info(s) end
local function svc() return manager.machine.ioport.ports[":SERVICE"] end
emu.register_frame_done(function()
 frame=frame+1
 if frame>=900 and frame<940 then
   pcall(function() svc().fields["Service Mode"]:set_value(0) end)
 end
 if frame==1100 then
   local sh=manager.machine.memory.shares[":workram"]
   log(string.format("frame 1100 e02a=%02X e02b=%02X e000=%02X",sh:read_u8(0x202A),sh:read_u8(0x202B),sh:read_u8(0x2000)))
   manager.machine.video:snapshot()
 end
end,"frame")
