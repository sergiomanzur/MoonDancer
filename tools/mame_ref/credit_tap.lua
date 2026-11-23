local frame=0
local function log(s) emu.print_info(s) end
local cpu=manager.machine.devices[":maincpu"]
local space=cpu.spaces["program"]
local tap=space:install_write_tap(0xFFE000,0xFFE00F,"credits",function(offset,data,mask)
  log(string.format("CW %06X=%04X m=%04X PC=%06X",offset,data,mask,cpu.state["CURPC"].value))
end)
local function svc() return manager.machine.ioport.ports[":SERVICE"] end
emu.register_frame_done(function()
 frame=frame+1
 tap:reinstall()
 if frame>=900 and frame<940 then
   pcall(function() svc().fields["Coin 1"]:set_value(0) end)
 end
 if frame==1200 then
   local sh=manager.machine.memory.shares[":workram"]
   log(string.format("frame 1200 credits %02X %02X %02X",sh:read_u8(0x2000),sh:read_u8(0x2002),sh:read_u8(0x2004)))
 end
end,"frame")
