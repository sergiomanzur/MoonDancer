local frame=0
local n=0
local function log(s) emu.print_info("PROBE "..s) end
local cpu=manager.machine.devices[":maincpu"]
local space=cpu.spaces["program"]
local tap=space:install_write_tap(0xE40000,0xE4001F,"wr",function(offset,data,mask)
  if n<10 then log(string.format("W off=%X data=%04X mask=%04X f=%d time=%s",offset,data,mask,frame,tostring(manager.machine.time))) n=n+1 end
end)
emu.register_frame_done(function() frame=frame+1; tap:reinstall() end,"frame")
