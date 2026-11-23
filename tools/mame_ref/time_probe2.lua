local frame=0
local function log(s) emu.print_info("PROBE "..s) end
local cpu=manager.machine.devices[":maincpu"]
local space=cpu.spaces["program"]
local tap=space:install_read_tap(0x1E54,0x1E55,"rd",function(offset,data,mask)
  log(string.format("READPC f=%d time=%s off=%X data=%04X",frame,tostring(manager.machine.time),offset,data))
end)
local tap2=space:install_read_tap(0xFFE030,0xFFE031,"rd2",function(offset,data,mask)
  if offset==1 then log(string.format("READ31 f=%d time=%s data=%02X",frame,tostring(manager.machine.time),data)) end
end)
emu.register_frame_done(function() frame=frame+1; tap:reinstall(); tap2:reinstall() end,"frame")
